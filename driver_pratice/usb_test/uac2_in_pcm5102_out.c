#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <alsa/asoundlib.h>

/* 声卡号、设备号以板端 aplay -l / arecord -l 输出为准。 */
#define UAC2_DEVICE "hw:1,0"
#define ALSA_DEVICE "hw:0,0"
#define OUTPUT_RATE 48000
#define OUTPUT_CHANNELS 2
#define OUTPUT_LATENCY 100000 /* 各端 ALSA 缓冲延迟请求，单位为微秒。 */

/* 每次请求读取的帧数；实际 ALSA period 由 set_params 协商。 */
#define PERIOD_FRAMES 1024
#define BYTES_PER_FRAME (OUTPUT_CHANNELS * sizeof(int16_t))
#define PCM_WAIT_MS 100

static volatile sig_atomic_t stop_requested;

static void request_stop(int signo)
{
    (void)signo;
    stop_requested = 1;
}

static int capture_sw_params(snd_pcm_t *pcm_handle)
{
    snd_pcm_sw_params_t *params = NULL;
    int ret;

    ret = snd_pcm_sw_params_malloc(&params);
    if (ret < 0)
        return ret;

    ret = snd_pcm_sw_params_current(pcm_handle, params);
    if (ret >= 0)
        ret = snd_pcm_sw_params_set_start_threshold(pcm_handle, params, 1);
    if (ret >= 0)
        ret = snd_pcm_sw_params_set_avail_min(pcm_handle, params,
                                            PERIOD_FRAMES);
    if (ret >= 0)
        ret = snd_pcm_sw_params(pcm_handle, params);

    snd_pcm_sw_params_free(params);
    return ret;
}

static int pcm_init(snd_pcm_t **pcm_handle, const char *device,
                    snd_pcm_stream_t stream)
{
    int ret;
    int mode = SND_PCM_EINTR;

    /* 主机未开启 USB 音频流时没有数据，不能让 capture 阻塞到 EIO。 */
    if (stream == SND_PCM_STREAM_CAPTURE)
        mode |= SND_PCM_NONBLOCK;

    *pcm_handle = NULL;
    ret = snd_pcm_open(pcm_handle, device, stream, mode);
    if (ret < 0) {
        fprintf(stderr, "%s: snd_pcm_open failed: %s\n",
                device, snd_strerror(ret));
        *pcm_handle = NULL;
        return ret;
    }

    /* 两端直接使用相同 PCM 格式，不依赖软件格式转换。 */
    ret = snd_pcm_set_params(*pcm_handle, SND_PCM_FORMAT_S16_LE,
                             SND_PCM_ACCESS_RW_INTERLEAVED,
                             OUTPUT_CHANNELS, OUTPUT_RATE, 0,
                             OUTPUT_LATENCY);
    if (ret < 0) {
        fprintf(stderr, "%s: snd_pcm_set_params failed: %s\n",
                device, snd_strerror(ret));
        goto fail;
    }

    if (stream == SND_PCM_STREAM_CAPTURE) {
        /* set_params 的默认启动阈值接近整个缓冲区，可能大于读取块。
         * 内核要求 readi 请求帧数达到阈值才启动 capture；设为 1，
         * 让首次 readi 及 xrun 恢复后的 readi 都能启动采集。
         */
        ret = capture_sw_params(*pcm_handle);
        if (ret < 0) {
            fprintf(stderr, "%s: capture software params failed: %s\n",
                    device, snd_strerror(ret));
            goto fail;
        }
    }

    return 0;

fail:
    snd_pcm_close(*pcm_handle);
    *pcm_handle = NULL;
    return ret;
}

static int uac2_init(snd_pcm_t **pcm_handle)
{
    /* PC 播放的数据，在板端从 UAC2 capture 读取。 */
    return pcm_init(pcm_handle, UAC2_DEVICE, SND_PCM_STREAM_CAPTURE);
}

static int alsa_init(snd_pcm_t **pcm_handle)
{
    return pcm_init(pcm_handle, ALSA_DEVICE, SND_PCM_STREAM_PLAYBACK);
}

static int recover_pcm(snd_pcm_t *pcm_handle, int error, const char *device)
{
    int ret;

    if (stop_requested)
        return 0;

    ret = snd_pcm_recover(pcm_handle, error, 1);
    if (ret < 0)
        fprintf(stderr, "%s: PCM error %s; recovery failed: %s\n",
                device, snd_strerror(error), snd_strerror(ret));
    else if (error != -EINTR)
        fprintf(stderr, "%s: recovered from %s\n",
                device, snd_strerror(error));

    return ret;
}

static int wait_pcm(snd_pcm_t *pcm_handle, const char *device)
{
    int ret;

    if (stop_requested)
        return 0;

    /* 无数据或暂时无空间时等待，避免零帧/EAGAIN 导致忙循环。 */
    ret = snd_pcm_wait(pcm_handle, PCM_WAIT_MS);
    if (ret < 0)
        return recover_pcm(pcm_handle, ret, device);

    return ret; /* 0 为等待超时，1 为就绪。 */
}

int main(void)
{
    snd_pcm_t *pcm_handle = NULL;
    snd_pcm_t *uac2_handle = NULL;
    uint8_t buffer[PERIOD_FRAMES * BYTES_PER_FRAME];
    struct sigaction action = {0};
    int status = EXIT_FAILURE;
    int input_active = 0;
    int output_active = 0;
    int ret;

    action.sa_handler = request_stop;
    sigemptyset(&action.sa_mask);
    /* 不设置 SA_RESTART，使阻塞读写能被退出信号打断。 */
    if (sigaction(SIGINT, &action, NULL) < 0 ||
        sigaction(SIGTERM, &action, NULL) < 0) {
        perror("sigaction");
        goto out;
    }

    ret = uac2_init(&uac2_handle);
    if (ret < 0){
        fprintf(stderr, "UAC2 device %s init failed: %s\n",
                UAC2_DEVICE, snd_strerror(ret));
        goto out;        
    }


    ret = alsa_init(&pcm_handle);
    if (ret < 0){
        fprintf(stderr, "ALSA device %s init failed: %s\n",
                ALSA_DEVICE, snd_strerror(ret));
        goto out;
    }

    fprintf(stderr, "Bridge %s capture -> %s playback: "
            "%d Hz, %d channels, S16_LE. Ctrl+C to stop.\n",
            UAC2_DEVICE, ALSA_DEVICE, OUTPUT_RATE, OUTPUT_CHANNELS);
    fprintf(stderr, "Waiting for PCM from PC playback on the UAC2 device...\n");

    while (!stop_requested) {
        snd_pcm_sframes_t frames_read;
        snd_pcm_uframes_t frames_to_write;
        uint8_t *data_ptr = buffer;

        frames_read = snd_pcm_readi(uac2_handle, buffer, PERIOD_FRAMES);
        if (stop_requested)
            break;

        if (frames_read <= 0) {
            if (frames_read == 0 || frames_read == -EAGAIN)
                ret = wait_pcm(uac2_handle, UAC2_DEVICE);
            else
                ret = recover_pcm(uac2_handle, (int)frames_read,
                                  UAC2_DEVICE);
            if (ret < 0)
                goto out;
            if (stop_requested)
                break;
            if ((frames_read == 0 || frames_read == -EAGAIN) &&
                ret == 0 && input_active) {
                /* 停流时丢弃播放端积压数据，恢复输入后再开始播放。 */
                ret = snd_pcm_drop(pcm_handle);
                if (ret >= 0)
                    ret = snd_pcm_prepare(pcm_handle);
                if (ret < 0) {
                    fprintf(stderr, "%s: stop playback failed: %s\n",
                            ALSA_DEVICE, snd_strerror(ret));
                    goto out;
                }
                input_active = 0;
                output_active = 0;
                fprintf(stderr, "UAC2 input idle; playback stopped, "
                        "waiting for PC audio...\n");
            }
            continue;
        }

        if (!input_active) {
            fprintf(stderr, "UAC2 received PCM: %ld frames.\n",
                    (long)frames_read);
            input_active = 1;
        }

        /* 返回值是帧数，只写实际读到的数据，不发送缓冲区旧内容。 */
        frames_to_write = (snd_pcm_uframes_t)frames_read;
        while (frames_to_write > 0 && !stop_requested) {
            snd_pcm_sframes_t frames_written;

            frames_written = snd_pcm_writei(pcm_handle, data_ptr,
                                             frames_to_write);
            if (stop_requested)
                break;

            if (frames_written <= 0) {
                if (frames_written == 0 || frames_written == -EAGAIN)
                    ret = wait_pcm(pcm_handle, ALSA_DEVICE);
                else
                    ret = recover_pcm(pcm_handle, (int)frames_written,
                                      ALSA_DEVICE);
                if (ret < 0)
                    goto out;
                /* 恢复后重试本批剩余数据，不直接读取下一批。 */
                continue;
            }

            frames_to_write -= (snd_pcm_uframes_t)frames_written;
            data_ptr += (size_t)frames_written * BYTES_PER_FRAME;
            if (!output_active) {
                fprintf(stderr, "%s: playback accepted %ld frames.\n",
                        ALSA_DEVICE, (long)frames_written);
                output_active = 1;
            }
        }
    }

    status = EXIT_SUCCESS;

out:
    /* 退出时停止 PCM，不等待主机继续送流或 drain 完成。 */
    if (pcm_handle) {
        snd_pcm_drop(pcm_handle);
        snd_pcm_close(pcm_handle);
    }
    if (uac2_handle) {
        snd_pcm_drop(uac2_handle);
        snd_pcm_close(uac2_handle);
    }

    return status;
}
