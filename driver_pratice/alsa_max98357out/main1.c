#include <stdio.h>
#include <stdlib.h>
#include <string.h>
 #include <errno.h>
#include <stdint.h>

#include <alsa/asoundlib.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
 
#include <libavcodec/avcodec.h>

#define AUDIO_INBUF_SIZE 20480
#define AUDIO_FILE "/oem/yexilijiangta.mp3"

#define ALSA_DEVICE "hw:0,0" // ALSA设备名称,第一个0是声卡号，第二个0是设备号，声卡号从/proc/asound/cards中查看，设备号从/proc/asound/pcm中查看
#define OUTPUT_RATE 48000 // 输出采样率
#define OUTPUT_CHANNELS 2 // 输出声道数
#define OUTPUT_LATENCY 100000 // 输出延迟，单位为微秒

#define PERIOD_FRAMES 1024 // 每次写入的帧数
#define BUFFER_PERIODS 4 // 缓冲区包含的周期数
/*
AVFormatContext   整个输入文件/网络流
AVStream          文件中的某一路流
AVCodecParameters 从文件中读取到的编码参数
AVCodec           具体的解码器实现
AVCodecContext    正在工作的解码器实例及状态
AVPacket          等待解码的压缩数据
AVFrame           解码得到的原始 PCM 数据
*/

int alsa_init(snd_pcm_t **pcm_handle) {
    //打开ALSA设备
    int ret = snd_pcm_open(pcm_handle, ALSA_DEVICE, SND_PCM_STREAM_PLAYBACK, 0);
    if (ret < 0) {
        fprintf(stderr, "snd_pcm_open failed: %s\n", snd_strerror(ret));
        return -1;
    }

    ret = snd_pcm_set_params(*pcm_handle,
                             SND_PCM_FORMAT_S16_LE, // PCM格式
                             SND_PCM_ACCESS_RW_INTERLEAVED, // 交错访问
                             OUTPUT_CHANNELS, // 声道数
                             OUTPUT_RATE, // 采样率
                             1, // 是否允许软件重采样
                             OUTPUT_LATENCY); // 延迟
    if (ret < 0) {
        fprintf(stderr, "snd_pcm_set_params failed: %s\n", snd_strerror(ret));
        snd_pcm_close(*pcm_handle);
        return -1;
    }
    return 0;
}

int play_audio(snd_pcm_t *pcm_handle, const AVFrame *frame, SwrContext *swr_ctx) {
    int ret;
    int output_samples;//输出的样本数
    int converted;//转换后的样本数
    int output_linesize;//输出的行大小
    uint8_t *output_data = NULL;//输出的数据缓冲区

    //计算输出缓冲区所需要的样本数
    //第一个参数：计算重采样器内部需要缓存的大小，其中frame->nb_samples代表原始帧数据中样本数量，swr_get_delay(swr_ctx, frame->sample_rate)代表重采样器内部尚未输出的延迟样本数量
    //重采样滤波器需要缓存的原因是输入和输出的样本频率可能不同，因此需要上下文来确定将要输出的样本
    //第二个参数：输出采样率，代表最后交给ALSA经过转换的PCM数据的采样率
    //第三个参数：原始帧数据的采样率
    //第四个参数：向上取整，样本空间必须大于实际的样本数量
    if(frame){
        output_samples = av_rescale_rnd(swr_get_delay(swr_ctx, frame->sample_rate) + frame->nb_samples, OUTPUT_RATE, frame->sample_rate, AV_ROUND_UP);

        //分配输出缓冲区
        ret = av_samples_alloc(&output_data, &output_linesize, OUTPUT_CHANNELS, output_samples, AV_SAMPLE_FMT_S16, 0);
        if (ret < 0) {
            fprintf(stderr, "av_samples_alloc failed: %s\n", av_err2str(ret));
            return ret;
        }

        //重采样，将frame中的数据转换为输出格式
        converted = swr_convert(swr_ctx, &output_data, output_samples, (const uint8_t **)frame->extended_data, frame->nb_samples);

    }
    else{
        ret = av_samples_alloc(&output_data, &output_linesize, OUTPUT_CHANNELS, 1024, AV_SAMPLE_FMT_S16, 0);
        if (ret < 0) {
            fprintf(stderr, "av_samples_alloc failed (in frame null time): %s\n", av_err2str(ret));
            return ret;
        }
        converted = swr_convert(swr_ctx, &output_data, 1024, NULL, 0);
    }
        if(converted < 0) {
            fprintf(stderr, "swr_convert failed: %d\n", converted);
            av_freep(&output_data);
            return converted;
        }   
    snd_pcm_uframes_t frames_to_write = (snd_pcm_uframes_t)converted; //需要写入的帧数
    uint8_t *data_ptr = output_data; //指向输出数据的指针
    //之前指定的转换格式为S16_LE，每个样本占用2个字节，单声道，所以每帧占用2个字节
    const size_t bytes_per_frame = 2 * OUTPUT_CHANNELS; //每帧的字节数

    while(frames_to_write > 0) {
        //写入PCM数据到ALSA设备
        ret = snd_pcm_writei(pcm_handle, data_ptr, frames_to_write);
        if (ret < 0) {
            fprintf(stderr, "snd_pcm_writei failed: %s\n", snd_strerror(ret));
            //尝试恢复ALSA设备
            ret = snd_pcm_recover(pcm_handle, ret, 0);
            if (ret < 0) {
                fprintf(stderr, "snd_pcm_recover failed: %s\n", snd_strerror(ret));
                av_freep(&output_data);
                return ret;
            }
            continue; //恢复后继续写入
        }
        //更新剩余帧数和数据指针
        frames_to_write -= ret;
        data_ptr += ret * bytes_per_frame;
    }
    av_freep(&output_data); //释放输出缓冲区
    return 0;
}


int main(int argc, char **argv)
{
    const char *filename;//输入的文件

    filename = argv[1];

    if(argc <= 1) {

        fprintf(stderr, "will decode audio file: %s\n", AUDIO_FILE);
        filename    = AUDIO_FILE;
    }


    //打开输入文件，读取文件信息
    AVFormatContext *format_context = NULL;
    int ret = avformat_open_input(&format_context, filename, NULL, NULL);
    if(ret < 0) {
        fprintf(stderr, "Could not open source file %s\n", filename);
        exit(1);
    }

    ret = avformat_find_stream_info(format_context, NULL);
    if(ret < 0) {
        fprintf(stderr, "Could not find stream information\n");
        exit(1);
    }

    //找到最合适的音频流
    int audio_index = av_find_best_stream(format_context, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if(audio_index < 0) {
        fprintf(stderr, "Could not find audio stream in input file\n");
        exit(1);
    }

    //创建并打开解码器
    AVCodecContext *codec_context = NULL;
    const AVCodec *codec = avcodec_find_decoder(format_context->streams[audio_index]->codecpar->codec_id);
    if(!codec) {
        fprintf(stderr, "Unsupported codec\n");
        exit(1);
    }
    codec_context = avcodec_alloc_context3(codec);
    if(!codec_context) {
        fprintf(stderr, "Could not allocate audio codec context\n");
        exit(1);
    }
    ret = avcodec_parameters_to_context(codec_context, format_context->streams[audio_index]->codecpar);
    if(ret < 0) {
        fprintf(stderr, "Could not copy codec parameters to decoder context\n");
        exit(1);
    }
    ret = avcodec_open2(codec_context, codec, NULL);
    if(ret < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }

    //读取压缩数据包并解码
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();//解码后的PCM数据
    if(!packet || !frame) {
        fprintf(stderr, "Could not allocate packet or frame\n");
        goto end;
    }


    snd_pcm_t *pcm_handle;
    SwrContext *swr_ctx = NULL;//正在使用的音频转换器实例
    int64_t output_layout = av_get_default_channel_layout(OUTPUT_CHANNELS);


    ret = alsa_init(&pcm_handle);
    if (ret < 0) {
        fprintf(stderr, "alsa_init failed\n");
        exit(1);
    }
    while (av_read_frame(format_context, packet) >= 0) {
        if (packet->stream_index == audio_index) {
            ret = avcodec_send_packet(codec_context, packet);
            if (ret < 0) {
                fprintf(stderr, "Error sending a packet for decoding\n");
                exit(1);
            }
            while (ret >= 0) {
                ret = avcodec_receive_frame(codec_context, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    break;
                else if (ret < 0) {
                    fprintf(stderr, "Error during decoding\n");
                    exit(1);
                }
                if(!swr_ctx){
                    //swr_alloc参数：现有SwrContext上下文的指针， 输出通道布局， 输出通道样本， 输出采样率， 输入通道布局， 输入样本格式，输入采样率，日志偏移，父日志上下文
                    int64_t input_layout = frame->channel_layout;
                    if (!input_layout)
                        input_layout = av_get_default_channel_layout(frame->channels);
                    if (!input_layout) {
                        fprintf(stderr, "Could not determine input channel layout\n");
                        goto end;
                    }
                    swr_ctx = swr_alloc_set_opts(NULL, output_layout,
                                               AV_SAMPLE_FMT_S16, OUTPUT_RATE,
                                               input_layout,
                                               (enum AVSampleFormat)frame->format,
                                               frame->sample_rate, 0, NULL);
                    if (!swr_ctx) {
                        fprintf(stderr, "swr_alloc_set_opts failed\n");
                        exit(1);
                    }
                    ret = swr_init(swr_ctx);
                    if (ret < 0) {
                        fprintf(stderr, "swr_init failed\n");
                        exit(1);
                    }
                }

                //处理解码后的PCM数据
                printf("Decoded frame: %d samples\n", frame->nb_samples);

                //调用play_audio函数将解码后的PCM数据写入ALSA设备
                ret = play_audio(pcm_handle, frame, swr_ctx);
                if(ret < 0) {
                    fprintf(stderr, "play_audio failed\n");
                    goto end;
                }
                av_frame_unref(frame);

            }
        }
        av_packet_unref(packet);
    }

end:
    swr_free(&swr_ctx);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec_context);
    avformat_close_input(&format_context);

    if(pcm_handle) snd_pcm_close(pcm_handle);



}
    
