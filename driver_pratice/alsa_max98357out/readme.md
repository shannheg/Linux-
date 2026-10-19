# RK3506 + FFmpeg 4.4 + ALSA + PCM5102 播放与排错记录（含 MAX98357A 历史）

本文整理本次对话中的接口解释、程序检查及板端测试。文中的示例表示推荐流程，不代表 `main1.c` 已完整实现全部收尾和错误处理。

2026-10-19 更新：当前 Codec 已由 MAX98357A 更换为 PCM5102，Linux 使用 `ti,pcm5102a` 驱动匹配。用户确认，在声卡的 `simple-audio-card,codec` 子节点加入 `clocks = <&mclkout_sai0>;` 后恢复正常播放。本文保留在原程序目录，当前硬件配置以 [PCM5102 设备树](../alsa_pcm5102out/dp-rk3506g-mini.dts) 为准；MAX98357A 内容标为历史记录。

## 1. 环境、文件与当前状态

- 用户确认 FFmpeg 版本为 **4.4**；接口应以该版本头文件和文档为准。
- `main.c`：直接读取 PCM WAV 并通过 ALSA 播放，编译为 `saiwav`。
- `main1.c`：FFmpeg 解封装、解码、重采样，再通过 ALSA 播放，编译为 `sai_ffmpeg`。
- 当前源码输出为 S16、48000 Hz、双声道；早期板端日志来自单声道配置。
- 当前使用 PCM5102 模块：RK3506 提供 MCLK/SCK、BCLK/BCK、LRCLK/LRCK 和 DIN，具体接线与时钟配置见第9节。
- 早期 MAX98357A 接线反馈：3.3V 供电、SD_MODE 拉高、GAIN 接 GND；当时声卡已注册、PCM 已进入 RUNNING，但 `main.c`、`main1.c`、`speaker-test` 都出现杂音。这些属于历史现象，不代表当前 PCM5102 方案仍未解决。
- 当前修复结果来自用户板端反馈：PCM5102 方案补充外部 MCLK 输出时钟引用后可正常播放。本次仅更新文档并核对源码，未重新编译、部署或进行示波器/声学测试，也未据此确认原 MAX98357A 硬件的根因。

```sh
make app        # main.c -> saiwav，make 默认也是这个目标
make ffmpeg     # main1.c -> sai_ffmpeg
make check-env  # 检查工具链、sysroot、头文件及库
```

FFmpeg 播放程序链接 `-lavformat -lavcodec -lswresample -lavutil -lasound -lm`。这些是用户态库，不是内核模块。

## 2. 文档与版本查询

- [FFmpeg 4.4 API](https://ffmpeg.org/doxygen/4.4/index.html)
- [FFmpeg 4.4 官方示例](https://ffmpeg.org/doxygen/4.4/examples.html)
- [FFmpeg 4.4 libswresample](https://ffmpeg.org/doxygen/4.4/group__lswr.html)
- [ALSA PCM API](https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m.html)
- [PCM5102 数据手册](https://www.ti.com/lit/ds/symlink/pcm5102.pdf)
- [PCM5102A 数据手册](https://www.ti.com/lit/ds/symlink/pcm5102a.pdf)
- [MAX98357A 数据手册](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf)

先阅读官方 demuxing/decoding、decode_audio、resampling 示例，再按函数名查参数和返回值。不要将最新 trunk 接口直接套到 4.4。

```sh
ffmpeg -version
ffmpeg -buildconf
ffprobe -version
ldd ./sai_ffmpeg
```

没有命令行工具时，可在程序中调用 `av_version_info()`、`avcodec_version()`、`avformat_version()`、`swresample_version()`。库版本整数按 `v >> 16`、`(v >> 8) & 255`、`v & 255` 拆为主、次、微版本。`avcodec_configuration()` 返回编译配置。

`LIBAVCODEC_VERSION_*` 宏反映编译头文件版本；`avcodec_version()` 反映运行库版本。`libavcodec.so` 的 ABI 主版本不能单独精确确定 FFmpeg 发布版本。

## 3. 解码对象与数据关系

| 对象 | 含义 |
|---|---|
| `AVFormatContext` | 整个文件/输入的解封装上下文，管理格式、流、读取状态 |
| `AVStream` | 文件中的一条音频、视频或字幕流及其时间信息 |
| `AVCodecParameters` | 编码格式、采样率、声道布局、附加编码信息等描述参数 |
| `AVCodec` | 某个具体解码器的实现 |
| `AVCodecContext` | 一个解码器实例的配置、缓存和连续解码状态 |
| `AVPacket` | 解封装得到的数据包，通常为压缩数据；PCM WAV 中可以已是 PCM |
| `AVFrame` | 解码后的 PCM 数据及其格式描述 |
| `SwrContext` | libswresample 转换实例，保存转换参数、滤波状态和内部缓存 |

`AVCodecContext` 不是待处理的文本或音频数据；输入数据在 Packet 中，结果在 Frame 中。不同独立音频流通常使用各自的解码上下文。

```text
输入文件 -> AVFormatContext -> AVPacket
                              |
                              v
                       AVCodecContext
                              |
                              v
                           AVFrame
                              |
                              v
                         SwrContext
                              |
                              v
                      固定格式PCM -> ALSA
```

普通 WAV 通常只有 `streams[0]` 音频；MP4 可能同时有视频、多个音轨和字幕。Stream 保存描述，并不存储整条音频的数据。

`audio_index` 是 `format_context->streams[]` 的下标；`packet->stream_index == audio_index` 用于筛选该流的数据包，不是比较帧序号，也不是比较 codec ID。同一条流的 Packet 通常具有相同 stream_index。

## 4. 解封装与解码函数

| 函数 | 作用 |
|---|---|
| `avformat_open_input(&fmt, path, NULL, NULL)` | 打开输入；输入格式传 NULL 时自动探测 |
| `avformat_find_stream_info(fmt, NULL)` | 分析媒体流信息，可能读取部分数据 |
| `av_dump_format(fmt, 0, path, 0)` | 打印容器、流、编码、采样率等信息 |
| `av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, ..., 0)` | 选择音频流，返回 streams 数组下标 |
| `avcodec_find_decoder(stream->codecpar->codec_id)` | 按编码 ID 查找解码器 |
| `avcodec_alloc_context3(codec)` | 分配解码器上下文 |
| `avcodec_parameters_to_context(ctx, stream->codecpar)` | 复制编码参数 |
| `avcodec_open2(ctx, codec, NULL)` | 初始化解码器 |
| `av_read_frame(fmt, packet)` | 读取一个 Packet，虽然名字包含 frame，但不是返回 AVFrame |
| `avcodec_send_packet(ctx, packet)` | 向解码器提交输入 |
| `avcodec_receive_frame(ctx, frame)` | 取得一帧解码结果，需要循环接收 |

多格式支持靠自动探测容器和 `codec_id`，不按文件扩展名分别写解码流程。M4A 可包含 AAC/ALAC，WAV 也不一定是普通 PCM。构建时还必须包含对应 demuxer、decoder、parser 和输入协议。

一个 Packet 可能输出零个或多个 Frame。receive 返回 `AVERROR(EAGAIN)` 表示需要更多输入，`AVERROR_EOF` 表示解码器已经排空；其他负值需要处理。send 若返回 EAGAIN，先接收已有输出，再重试同一个 Packet，不能直接丢包。

输入 Packet 的时间基建议在打开解码器前设置：

```c
codec_context->pkt_timebase = audio_stream->time_base;
```

`codecpar` 不包含 Stream 的 time_base，因此参数复制不会自动完成这一步。

## 5. PCM 精度、平面和交错

位深表示幅度精度；采样率表示每秒采样次数；声道数表示每个采样时刻有几路样本。它们是独立参数。

| 交错格式 | 平面格式 | 每声道每样本字节数 |
|---|---|---:|
| U8 | U8P | 1 |
| S16 | S16P | 2 |
| S32 | S32P | 4 |
| S64 | S64P | 8 |
| FLT | FLTP | 4 |
| DBL | DBLP | 8 |

上述名称对应 `AV_SAMPLE_FMT_*`。整数格式使用本机字节序；RK3506 小端平台的 S16 对应 ALSA 的 S16_LE。浮点满幅通常以 -1.0～1.0 表示。NONE 表示未指定，不是可播放格式。

```text
双声道S16： 一个平面 L0 R0 L1 R1 ...
双声道S16P：平面0 L0 L1 ...；平面1 R0 R1 ...
单声道：   两种排列都是 M0 M1 M2 ...
```

单声道用平面格式没有额外排列优势。当前 `snd_pcm_writei()` + RW_INTERLEAVED 选择 S16 更直接，多声道平面输入则要转换或改用匹配的非交错接口。

16bit PCM 每样本2字节，幅度范围 -32768～32767。U8 只有256个幅度等级，静音中心是128；S16 静音中心是0。降低位深增加量化误差，提高存储位数不能恢复音源缺失的细节。24bit PCM 在 FFmpeg 中通常用 S32 容器表示；4字节存储不意味着音源一定有32位有效精度。MP3 的128/320kbps是压缩码率，不是样本位深。

```text
PCM字节数 = 每声道样本数 × 声道数 × 每样本字节数
48kHz、S16、双声道：每秒192000字节
```

采样格式应匹配硬件能力和精度需求。S16适合当前普通播放；需要保留24bit音源时考虑S32。修改格式时，重采样输出、缓冲分配、ALSA格式和数据指针步长必须同时匹配。

### AVFrame 的数据成员

- `nb_samples`：每声道样本数，不是全部声道样本数总和。
- `format`、`sample_rate`、`channel_layout`：4.4中的实际输出格式信息。
- `data[]`：有限数量的数据平面指针，通常为8个。
- `extended_data`：访问全部音频平面的统一入口。
- `buf[]`、`extended_buf`：管理底层缓冲区引用，不是需要应用手工填充的PCM数组。

超过8个平面声道时继续通过 `extended_data[ch]` 访问，不能将“转成S16”理解为“把多余通道搬到extended_buf”。

## 6. SwrContext 与 FFmpeg 4.4 初始化

名称可理解为软件重采样上下文，官方定义为 libswresample context。它同时负责采样率、样本格式、平面/交错排列和声道混合转换。

第一帧解码成功后初始化，是为了取得真实 PCM 参数，不是强制要求；若解码前参数已经完整且可靠，也可提前初始化。正常情况下重复使用同一个上下文；输入参数发生变化时需处理旧缓存并重新配置。

4.4使用旧版整数声道布局，不能使用 `AVChannelLayout`、`frame->ch_layout`、`swr_alloc_set_opts2()`：

```c
int64_t output_layout = av_get_default_channel_layout(OUTPUT_CHANNELS);
int64_t input_layout = frame->channel_layout;
if (!input_layout)
    input_layout = av_get_default_channel_layout(frame->channels);

swr_ctx = swr_alloc_set_opts(
    NULL,                                /* 新建上下文 */
    output_layout,                       /* 输出声道布局掩码 */
    AV_SAMPLE_FMT_S16,                   /* 输出采样格式 */
    OUTPUT_RATE,                         /* 输出采样率 */
    input_layout,                        /* 输入布局掩码 */
    (enum AVSampleFormat)frame->format,  /* 输入实际采样格式 */
    frame->sample_rate,                  /* 输入实际采样率 */
    0,                                   /* 日志级别偏移 */
    NULL);                               /* 日志关联对象 */
if (!swr_ctx)
    /* 错误处理 */;
/* 检查swr_init(swr_ctx)的返回值 */
```

旧版函数返回指针，不是整数错误码；布局参数传整数值，不取地址。整数布局不需要 `av_channel_layout_uninit()`。

输出mono默认是按混音矩阵下混，不是自动只保留左或右。立体声输入、单声道输出若要只保留左，可以在 swr_init 前设置 `double matrix[2] = {1.0, 0.0}; swr_set_matrix(swr_ctx, matrix, 2);`；只保留右则用 `{0.0, 1.0}`。

## 7. play_audio 的参数与处理流程

```c
int play_audio(snd_pcm_t *pcm_handle,
               const AVFrame *frame,
               SwrContext *swr_ctx);
```

- pcm_handle：已打开并配置的ALSA播放句柄。
- frame：解码PCM帧；约定NULL表示不再提供输入、取出重采样尾部。
- swr_ctx：已配置且初始化的重采样器；空帧处理也必须有有效上下文。

### 输出容量计算

```c
output_samples = av_rescale_rnd(
    swr_get_delay(swr_ctx, frame->sample_rate) + frame->nb_samples,
    OUTPUT_RATE, frame->sample_rate, AV_ROUND_UP);
```

`av_rescale_rnd(a,b,c,rnd)` 计算 a×b/c 并按规则取整，同时避免直接乘法溢出。a是内部已有延迟对应的输入样本量加当前帧样本量，b是输出采样率，c是输入采样率，AV_ROUND_UP向上取整，以免空间不足。容量可以恰好等于需要量，并非必须严格大于。

`swr_get_delay(ctx, base)` 查询下一输入样本相对下一输出样本的延迟，包含内部缓存及滤波延迟；base指定计量单位。base=输入采样率时，以输入采样点为单位。它不读取当前Frame，也不设置人为等待时间。

### 分配、转换、写入

```c
av_samples_alloc(&output_data, &output_linesize,
                 OUTPUT_CHANNELS, output_samples,
                 AV_SAMPLE_FMT_S16, 0);
```

参数依次为输出平面指针数组、每平面分配字节数、声道数、每声道样本容量、格式、对齐。align=0使用默认对齐；linesize可能含填充，实际播放长度应按转换样本数计算。S16是packed，因此一个输出平面足够，当前 `&output_data` 用法适合S16；不能不调整指针数组就随意改成多声道平面输出。

分配函数只分配空间，不转换数据。实际转换由以下调用完成：

```c
converted = swr_convert(swr_ctx,
                        &output_data, output_samples,
                        (const uint8_t **)frame->extended_data,
                        frame->nb_samples);
```

参数依次为上下文、输出平面地址、每声道输出容量、输入平面地址、每声道输入样本数。返回值是实际输出的每声道样本数；小于0是错误，0可能只是暂时没有输出。

`snd_pcm_uframes_t` 是帧数量的无符号整数类型，不是PCM数组。实际数据在uint8_t/int16_t缓冲区中。`snd_pcm_writei(pcm, data, frames)` 的frames表示采样时刻数量，一个ALSA frame包含该时刻所有声道的样本，正好对应converted。

```text
S16单声道：每ALSA frame为2字节
S16双声道：每ALSA frame为4字节
指针步长：written × OUTPUT_CHANNELS × 2
```

writei返回实际成功写入的帧数，类型为 `snd_pcm_sframes_t`。需要处理部分写入、负值和0；负值可用 `snd_pcm_recover()` 恢复，恢复成功后重试尚未写入的数据。返回0时可等待，不应无限忙循环。

### frame=NULL 的收尾

未输出样本存放在SwrContext内部，不在上次调用已释放的output_data里。每次分配临时输出缓冲区是正确做法；复用内存是可选性能优化。

空帧也要先分配输出空间，例如1024帧，再调用：

```c
converted = swr_convert(swr_ctx, &output_data, 1024, NULL, 0);
```

播放本次结果，重复空输入转换直到返回0。一次只取1024帧不能保证清空；停止条件不是“少于1024”，而是返回0。不能直接goto写入循环的指针更新位置：那会跳过分配、转换和变量初始化。

### 升采样与降采样

24kHz的一秒音频有24000个样本，重采样到48kHz后约有48000个样本，仍为一秒。新增采样点通过滤波插值计算，不能恢复原本没有录到的细节。降采样要滤掉目标采样率无法表示的高频，避免混叠。

仅改变播放采样率而不重采样才会改变时长和音调，例如24000个样本直接按48kHz播放只持续0.5秒。

## 8. ALSA接口、延迟与资源生命周期

`/sys/class/sound/card0` 是sysfs信息目录，不能直接写PCM。程序打开 `hw:0,0`，通常对应 `/dev/snd/pcmC0D0p`。plughw允许插件适配，但排查硬件时hw更容易知道实际参数。

- `snd_pcm_open(&handle, device, SND_PCM_STREAM_PLAYBACK, 0)`：以阻塞方式打开播放设备。
- `alsa_init` 需要 `snd_pcm_t **`，内部set_params和close使用 `*pcm_handle`。
- `snd_pcm_set_params`：指定格式、访问方式、声道数、采样率、是否允许软件重采样、目标延迟（微秒）。
- `snd_pcm_get_params`：查询实际buffer和period大小。
- `snd_pcm_avail`：查询当前可写帧数；avail_update轻量，但未主动同步最新硬件位置。
- `snd_pcm_avail_delay`：同步取得可写空间avail和播放延迟delay。
- `snd_pcm_delay`：查询新写入样本距离可听见的延迟，单位ALSA frame；不应在所有设备/插件上直接等同环形缓冲占用。
- `snd_pcm_drain`：等待已排队音频播放完成。
- `snd_pcm_close`：关闭句柄，不能替代正常收尾的drain。

`OUTPUT_LATENCY=100000`是ALSA目标100ms配置，不是每帧等待100ms，也不影响swr_get_delay。实际buffer会按硬件能力取值，播放过程中占用持续变化。

正常运行的直接硬件PCM可近似计算 `queued=buffer_size-avail`，时间为 `frames×1000/rate` 毫秒。若扬声器播放到200ms且实际排队100ms，队尾约对应300ms。

要主动维持目标占用：target=rate×latency_us/1000000；补充量=target-queued，并限制为不超过avail及应用已有PCM。查询和写入期间硬件继续消耗，无法保证每次精确固定100ms；通常按period周期补充，并使用应用PCM FIFO（例如AVAudioFifo）保存未写完的数据。

`av_packet_unref()`、`av_frame_unref()` 释放当前缓冲引用并重置对象，保留对象供下一次复用；`av_packet_free()`、`av_frame_free()` 连对象一起释放并置NULL。解码器成功接收Packet后可解除调用方引用。同步转换、写入完成后可解除Frame引用。

`end:` 是函数内标签，正常顺序执行或goto end会进入它；`exit(1)`直接终止整个进程，不执行标签后的清理。若统一goto清理，所有资源指针必须在可能跳转之前初始化NULL，避免跳过初始化后释放不确定地址。被关闭的句柄应置NULL，避免重复关闭。

正常文件收尾顺序：确认EOF -> send_packet(NULL) -> 接收并播放剩余Frame -> play_audio(NULL)冲刷重采样器 -> snd_pcm_drain -> 释放对象。读取错误与EOF要区分，函数错误返回值要检查，main应返回明确成功/失败状态。

当前源码仍需单独核对完整收尾：空帧分支已有分配，但只有一次转换；主流程尚未见完整解码器冲刷、空帧调用及drain。分配失败goto end还需确认不会跳过pcm_handle/swr_ctx初始化。本文不将这些流程描述为已完成。

## 9. 当前 PCM5102 设备树、I²S 时钟与 MCLK 输出修复

### 9.1 时钟名称与当前接线

PCM5102 模块上的 **SCK 是系统主时钟 MCLK**，**BCK 才是 RK3506 的 SCLK/BCLK 位时钟**，不能因为名字相近而混接。当前 PCM5102 设备树的有效配置为：

| RK3506 引脚 | 配置功能 | PCM5102 引脚 | 48kHz、双声道32bit时隙下的目标值 |
|---|---|---|---|
| RM_IO8 / GPIO0_B0 | MCLK | SCK | 12.288MHz |
| RM_IO9 / GPIO0_B1 | SCLK / BCLK | BCK | 3.072MHz |
| RM_IO10 / GPIO0_B2 | SDO | DIN | 串行PCM数据 |
| RM_IO11 / GPIO0_B3 | LRCK / LRCLK | LCK / LRCK | 48kHz |
| GND | 公共地 | GND | 共地 |

RM_IO 编号不是排针脚号，物理位置需核对板卡引脚表。PCM5102 文件中残留的旧三线注释不能代替实际 `pinctrl-0`；原 MAX98357A 设备树的 RM_IO8=BCLK、RM_IO9=LRCLK 接法也不能直接沿用。

```text
LRCLK = fs = 48000Hz
MCLK  = fs × 256 = 12.288MHz
BCLK  = fs × 2个时隙 × 32bit = 3.072MHz
MCLK / BCLK = 4
```

`OUTPUT_RATE` 是 PCM 采样率，标准 I²S 下对应 LRCLK。`mclk-fs = <256>` 表示 MCLK 为采样率的256倍，不是 BCLK 的256倍。S16 是有效数据位宽，不保证时隙也是16bit；BCLK 由 SAI 的时隙宽度、每lane声道数及 Frame Width Ratio 决定。I²S 数据 MSB 位于 LRCLK 切换后一个 BCLK 处。

PCM5102 支持外部 SCK 的四线模式，也支持 SCK 接地、由 BCK 经内部 PLL 生成时钟的三线模式；48kHz 三线模式支持 BCK 为1.536或3.072MHz。当前采用外部 MCLK 的四线接法时，模块 SCK 不能同时被接地焊桥短接到 GND。FMT 应为低电平以选择 I²S，XSMT 应为高电平解除静音。具体要求见 TI 手册的 Audio Data Interface 和 System Clock PLL Mode。

PCM5102 的 OUTL、OUTR 是分别对 GND 的左右声道线路输出，需要后级功放驱动喇叭。播放440Hz左声道正弦时，可用示波器直接观察 OUTL 对 GND，目标周期约2.27ms；不能把 OUTL/OUTR 当成 MAX98357A 的 OUTP/OUTN 差分功率输出。

### 9.2 本次有效设备树配置

下面对应 `driver_pratice/alsa_pcm5102out/dp-rk3506g-mini.dts` 的音频部分；关键新增项在**声卡的 codec 子节点**内，不是只给根目录的 `pcm5102` 节点添加属性：

```dts
/ {
    pcm5102: pcm5102 {
        compatible = "ti,pcm5102a";
        #sound-dai-cells = <0>;
        status = "okay";
    };

    sai0_sound: sai0-sound {
        compatible = "simple-audio-card";
        simple-audio-card,name = "rk3506-pcm5102";
        simple-audio-card,format = "i2s";
        simple-audio-card,mclk-fs = <256>;
        simple-audio-card,bitclock-master = <&sai0_cpu>;
        simple-audio-card,frame-master = <&sai0_cpu>;

        sai0_cpu: simple-audio-card,cpu {
            sound-dai = <&sai0>;
        };

        simple-audio-card,codec {
            sound-dai = <&pcm5102>;
            clocks = <&mclkout_sai0>; /* 本次恢复播放的关键补充 */
        };
    };
};

&sai0 {
    pinctrl-names = "default";
    pinctrl-0 = <&rm_io8_sai0_mclk &rm_io9_sai0_sclk
                 &rm_io10_sai0_sdo &rm_io11_sai0_lrck>;
    status = "okay";
};
```

PCM5102 通过 FMT 等硬件引脚选择格式；本内核的 `pcm5102a.c` 只注册 DAI/Component，不通过 I²C/SPI 写寄存器。`clocks` 放在 simple-card 的 codec 子节点中，由 simple-card 管理，无须给这个 Codec 驱动增加寄存器初始化或时钟使能代码。

### 9.3 为什么加入 mclkout 才能正常播放

RK3506 的内部 SAI 时钟与对外 MCLK 输出是同一时钟链中的不同环节：

```text
CRU MCLK_SAI0 / mclk_sai0
    ├─ SAI 内部使用，并分频生成 BCLK / LRCLK
    └─ CRU MCLK_OUT_SAI0 / mclk_out_sai0 输出门控
         └─ mclkout_sai0 / mclk_sai0_to_io（GRF输出门控）
              └─ RM_IO8 引脚复用 → PCM5102 SCK
```

三类设备树配置各自负责不同的事：

| 配置 | 作用 | 单独配置不能保证的内容 |
|---|---|---|
| `simple-audio-card,mclk-fs = <256>` | 按采样率计算并请求时钟频率 | 对外输出门控已使能 |
| `rm_io8_sai0_mclk` | 将引脚复用为 MCLK 功能 | 时钟源及输出门控已持续开启 |
| codec 子节点 `clocks = <&mclkout_sai0>` | 声明外部 MCLK 的消费者，让 simple-card 管理使能、频率和释放 | 模块接线、电平和所有 I²S 数据都正确 |

本次源码核对的调用链为：

1. `simple-card.c` 调用 `asoc_simple_parse_clk()`，从 codec 子节点取得 `mclkout_sai0` 并保存时钟句柄。
2. 打开播放 PCM 时，`asoc_simple_startup()` 经 `asoc_simple_clk_enable()` 调用 `clk_prepare_enable()`，使能该输出时钟及其父时钟链。
3. 配置播放参数时，`asoc_simple_hw_params()` 计算 `mclk = params_rate(params) * mclk_fs`；48kHz下目标为12.288MHz，并配置时钟及 CPU DAI 的 sysclk。
4. `rockchip_sai_set_sysclk()` 设置内部 `MCLK_SAI0`，输出时钟通过父时钟关系使用该时钟。仅设置内部时钟不能代替使能子级输出门控。
5. 关闭 PCM 时，`asoc_simple_shutdown()` 释放对应时钟引用；若无其他消费者维持，时钟框架可以关闭输出。

因此，这一行补齐的是**外部 MCLK 输出的驱动管理关系**。加入后，正常播放流程会主动开启并持有这条时钟链，而不再依赖之前遗留的门控状态。用户反馈加入该项后恢复正常播放，源码路径与这一结果相符；它不是通过修改 PCM 样本或 FFmpeg 解码算法解决的。

源码依据（相对本 README）：

- [simple-card-utils.c](../../sound/soc/generic/simple-card-utils.c)：`asoc_simple_parse_clk()`、`asoc_simple_startup()`、`asoc_simple_hw_params()`、`asoc_simple_shutdown()`。
- [rockchip_sai.c](../../sound/soc/rockchip/rockchip_sai.c)：`rockchip_sai_set_sysclk()`、`rockchip_sai_hw_params()`、runtime suspend/resume。
- [rk3502.dtsi](../../arch/arm/boot/dts/rk3502.dtsi)：SAI0 使用 `MCLK_SAI0`，`mclkout_sai0` 引用 `MCLK_OUT_SAI0`。
- [clk-rk3506.c](../../drivers/clk/rockchip/clk-rk3506.c)：`mclk_sai0` 与 `mclk_out_sai0` 的父子关系。
- [clk-out.c](../../drivers/clk/rockchip/clk-out.c)：注册 GRF 输出门控，注册本身不等于启用。
- [clk.c](../../drivers/clk/clk.c)：`clk_disable_unused()` 在初始化后清理未使用的时钟。

### 9.4 为什么示波器曾测到正确 MCLK，仍需要 mclkout

**测得某一时刻、某个位置的 MCLK 频率正确，只能证明那次采集窗口内存在相应周期，不能证明 PCM5102 在整个播放期间都收到了满足条件的外部时钟。**

缺少 `mclkout_sai0` 消费者时，内部 MCLK 仍可被 SAI 设置为正确频率，BCLK/LRCLK 也可能正常；对外输出门控却没有被声卡持有。若 U-Boot 或此前配置留下了开启状态，可能暂时测到正确的外部频率，但后续未使用时钟清理、父时钟电源管理或播放启停后，不保证继续保持。以上是基于驱动路径的可能解释，尚未用修复前的连续采集或门控寄存器记录逐项证实，不能写成已确定发生的时序。

还应排除测量条件的差别：

- 应测 PCM5102 模块 SCK 引脚，而不只测开发板端；两端接线、接地和电平必须正确。
- 应从启动到播放期间连续观察，或用单次/超时等触发捕获停钟；示波器停止后的旧波形和局部频率读数不能说明当前仍有时钟。
- 外部 SCK 与 LRCK 必须保持同步，不能只分别读到12.288MHz和48kHz就认定满足要求；还需检查丢脉冲、幅度及连续性。芯片不要求固定的两者边沿相位，不能把“同步”误解为必须对齐某个边沿。
- 三线 PLL 模式要求 SCK 保持低电平；若外部 SCK 时有时无或处于不明确电平，不能直接假定芯片会自动可靠地退回三线模式。

当前可确定的事实是“用户曾测到 MCLK 频率正确”以及“加入输出时钟引用后恢复正常播放”。若修复前已确认同一次播放全过程中，芯片端 SCK/BCK/LRCK 的频率、电平、连续性和同步关系全部正确，则不能再简单解释为“当时完全没有 MCLK”，还需核对前后固件、DTB及启动状态。本文不把未取得的全过程波形当作已验证证据。

复核时可在停止、播放和关闭 PCM 后分别查看时钟状态（debugfs 已挂载时）：

```sh
grep -E 'mclk_sai0|mclk_out_sai0|mclk_sai0_to_io' /sys/kernel/debug/clk/clk_summary
```

同时在模块端测 SCK/BCK/LRCK。`clk_summary` 的频率是框架中的配置读数，即使频率正确，也要结合 enable/prepare 计数和实际引脚波形；未播放时不应要求输出一直开启。

### 9.5 MAX98357A 历史配置

原 MAX98357A 在标准 I²S 下，SD_MODE 直接为3.3V选择左声道，GAIN 接 GND 为12dB；芯片不需要外接 MCLK。MAX98357B 使用左对齐格式，不要混淆型号。48kHz下 BCLK/LRCLK 可为32、48或64，对应1.536、2.304、3.072MHz。

原目录设备树使用 RM_IO8=BCLK、RM_IO9=LRCLK、RM_IO10=SDO，区别于当前 PCM5102 的四线映射。MAX98357A 只播放所选时隙；mono PCM 在线上位于哪个时隙取决于 SAI 配置，不能仅凭 `OUTPUT_CHANNELS=1` 推断。软件下混为 M 后若需要两边相同，应明确复制为 `M0 M0 M1 M1 ...`，仅改为双声道不会自动实现。

MAX98357A 是 D 类功放，扬声器接 OUTP 与 OUTN 之间，两端都不能接 GND。其原始输出为高频开关脉冲；普通示波器做两路相减时，两路地夹都只能接模块 GND。这些输出接法不能套用到 PCM5102 的 OUTL/OUTR。

## 10. 板端测试记录

早期 MAX98357A 声卡输出（历史日志，不是当前 PCM5102 的完整日志）：

```text
0 [rk3506max98357]: simple-card - rk3506-max98357
00-00: ff300000.sai-HiFi HiFi-0 : ... : playback 1
```

确认card0/device0与hw:0,0匹配。playback 1表示一个播放子流，不是单声道。声卡注册不证明芯片实际接线正确。

当前 PCM5102 设备树中的声卡名已为 `rk3506-pcm5102`。板端实际绑定应通过 `/proc/asound/pcm` 中的 `pcm5102a-hifi` 以及运行中的设备树共同核对，不能只按历史声卡名称判断 Codec。

早期单声道播放参数及状态：

```text
access: RW_INTERLEAVED
format: S16_LE
channels: 1
rate: 48000
period_size: 1200
buffer_size: 4800
state: RUNNING
delay: 3992
avail: 808
hw_ptr: 569880
appl_ptr: 574310
```

period约25ms，buffer约100ms，delay约83.2ms，avail约16.8ms。说明PCM已启动并有排队数据，不证明物理DIN正确或PCM非静音。指针与delay/avail可能来自不同更新时刻，不能要求所有数值严格互相相减相等。

```sh
cat /proc/asound/cards
cat /proc/asound/pcm
ls -l /dev/snd
# 播放时在另一个终端执行，两条命令分开输入
cat /proc/asound/card0/pcm0p/sub0/hw_params
cat /proc/asound/card0/pcm0p/sub0/status
sleep 1
cat /proc/asound/card0/pcm0p/sub0/status
```

持续播放时，48kHz下hw_ptr每秒约推进48000帧。若一直PREPARED，检查启动阈值/写入量；RUNNING且指针推进则继续查数据、总线及功放。

2026-10-19 修复记录：用户确认当前已换用 PCM5102，在声卡 codec 子节点补充 `clocks = <&mclkout_sai0>;` 后恢复正常播放。此前示波器观察到 MCLK 频率正确，但本文未取得能证明修复前全过程状态的连续时钟波形和门控计数；机制与证据范围见9.3、9.4节。

构建核对仍需区分本地配置与板端镜像：实际构建配置应启用 `CONFIG_SND_SOC_PCM5102A`、`CONFIG_SND_SOC_ROCKCHIP_SAI`、`CONFIG_SND_SIMPLE_CARD` 和 `CONFIG_ROCKCHIP_CLK_OUT`（内建或正确加载的模块）。练习目录的 DTS 只有进入实际编译的 DTB 并部署到板端才会生效。前次本地检查曾发现 PCM5102A 驱动未启用、常规入口 DTS 未引用练习配置；这些属于当时本地快照，不能用来否定用户反馈的板端播放结果，也不能据此认定当前运行镜像与该快照一致。

## 11. alsa-utils安装与测试音

最初aplay和speaker-test提示command not found，说明缺少工具，不说明驱动失败。libasound与alsa-utils是不同软件包。

在SDK根目录配置：

```sh
./build.sh buildroot-config
```

进入Target packages -> Audio and video applications -> alsa-utils，启用APLAY和SPEAKER_TEST；可按/搜索：

```text
BR2_PACKAGE_ALSA_UTILS
BR2_PACKAGE_ALSA_UTILS_APLAY
BR2_PACKAGE_ALSA_UTILS_SPEAKER_TEST
```

保存退出后 `./build.sh buildroot`，部署新rootfs。SDK入口会更新板级defconfig；直接修改另一份Buildroot .config可能被构建时加载的defconfig覆盖。kernel目录的make menuconfig修改的是内核配置。依赖不满足时手工写y也不能强制生效；当前alsa-utils要求MMU和线程支持。

speaker-test实时生成正弦或粉红噪声，无须测试文件；aplay播放已有文件。语音WAV是否安装取决于打包配置。测试时停止其他播放器，避免hw:0,0被占用。

```sh
aplay -l
speaker-test -D hw:0,0 -r 48000 -F S16_LE -c 2 -t sine -f 440 -s 1
# 正常应听见稳定440Hz左声道音调，Ctrl+C停止
aplay -D hw:0,0 /oem/test_stereo.wav
```

test_stereo.wav需要自行准备为已知正常的双声道PCM WAV。speaker-test默认测试声音可能是噪声，必须明确-t sine才能把“杂音”作为异常判断。降低幅度前先查看speaker-test --help；只有当前版本支持-S时才用-S 10。

可用FFmpeg生成已知低幅度、双声道测试文件：

```sh
ffmpeg -f lavfi -i 'sine=frequency=440:sample_rate=48000:duration=5' \
    -ac 2 -c:a pcm_s16le /oem/test_stereo.wav
```

该命令需要构建包含lavfi和sine滤镜；默认sine幅度较低。生成后再用aplay直接验证。

## 12. 日志含义与排错顺序

### MP3警告

- `filesize and duration do not match (growing file?)`：头部记录与实际文件大小不一致，可能截断、复制不完整、拼接编辑后头部未更新；growing file只是猜测。可能影响时长和seek。
- `Could not update timestamps for skipped samples`：解码器跳过起始延迟样本时缺少适当时间基，常见原因是未设置pkt_timebase；不等于PCM解码失败。
- 首帧47 samples、后续1152 samples：与起始样本裁剪和常见MP3帧长度相符，不应仅凭短首帧判断故障。
- `invalid new backstep -1`：MP3位储存池更新时，剩余压缩字节计算为负，提示码流边界不一致；可能文件损坏、不规范或输入连续性异常。它来自解码器，不是ALSA错误。后续输出Frame不保证该位置完全无失真。

```sh
ffmpeg -v warning -i /oem/yexilijiangta.mp3 -f null -
```

独立解码也出现相同错误，说明不依赖ALSA路径；结合大小警告优先检查原文件、复制前后校验值和重新获取文件，但不能仅凭日志确定唯一根因。

### 无声/杂音

早期 `main.c`、`main1.c`、`speaker-test` 都出现杂音，因此优先排查公共的 ALSA -> SAI/I²S -> Codec 链路。当前 PCM5102 方案已在补充 `mclkout_sai0` 后恢复播放；若后续再次出现无声或杂音，按以下顺序验证：

1. 明确双声道、S16_LE、48kHz、左声道440Hz正弦，排除speaker-test默认噪声。
2. 降低测试幅度：仍有规律音调但破裂，关注 PCM5102 线路输出与后级功放的幅度匹配、削顶、供电能力和负载；12dB增益是旧 MAX98357A 的配置。
3. 当前四线 PCM5102 在芯片引脚测 SCK/MCLK、LRCLK、BCLK 的频率、比例和连续性，结合 `mclkout_sai0` 输出使能检查；不能只测开发板端或只读一次频率。
4. 逻辑分析仪按标准I²S解码，检查一位延迟、边沿、有效位宽及左时隙非零数据。严重随机杂音优先检查时序/数据线。
5. 核对SDO到DIN、RM_IO映射、实际DTB、共地和短线；检查振铃。
6. 测播放负载下 PCM5102 供电、FMT、XSMT，检查去耦及 OUTL/OUTR 对 GND 的音频输出，再排查后级功放。SD_MODE、GAIN、OUTP/OUTN 属于旧 MAX98357A 路径。
7. 程序打印converted、累计实际写入帧数和S16样本峰值，确认不是静音；保存PCM再用aplay播放以区分生成与写入问题。

当前修复依据为用户确认 PCM5102 方案加入外部 MCLK 输出时钟引用后正常播放，以及源码确认该引用参与输出门控管理。原 MAX98357A 故障、修复前是否及何时发生停钟或门控变化、PCM 样本全过程和声学指标未完成独立验证，不将这些进一步结论写成已证实事实。

## 13. 在另一台设备运行

编译环境不必完全相同，但CPU架构/ABI、浮点ABI、C库/动态加载器及FFmpeg/ALSA共享库必须兼容；hw:0,0也必须指向目标声卡。

```sh
uname -m
file ./sai_ffmpeg
ldd ./sai_ffmpeg
# 可在编译主机检查
readelf -l sai_ffmpeg
readelf -d sai_ffmpeg
```

Exec format error通常是架构不匹配；文件存在却not found可能缺加载器；库not found、GLIBC版本或undefined symbol指向运行库兼容问题。另一台正常播放只能证明那台环境可运行，不能证明当前板接线及I²S正常。
