# RK3506 可视化 HTTP / 网页与流媒体程序技术调研

调研日期：2026-10-15。约束：Linux 6.1 SDK，媒体库使用 FFmpeg 4.4 系列。

本文是实现方案，不是已完成的程序。已核对本地源码、Buildroot 包定义和官方资料；未进行交叉编译、镜像生成、部署或板端性能测试。原 readme.md 为空，本次仅补充文档。

## 1. 结论与目标边界

“像手机网页浏览器”至少包含两种不同的目标：

| 目标 | 所需核心组件 | FFmpeg 能负责什么 |
| --- | --- | --- |
| 输入网址，浏览 HTML/CSS/JavaScript 网页，并播放网页里的视频 | 浏览器引擎、网络与安全组件、页面渲染、媒体管线、窗口与输入系统 | 媒体解封装、解码、格式转换；不能代替浏览器引擎 |
| 有地址栏、列表、播放按钮，访问指定服务器并播放网络媒体 | LVGL 等 GUI、业务 HTTP 客户端、FFmpeg、显示与音频输出 | 可直接承担网络媒体读取及解码 |

**建议先在 RK3506 上实现第二种，验证软解码和显示预算；如果任意网页浏览是硬性要求，则完整浏览器必须作为独立阶段验证，不能把第二种称为已实现浏览器。**

完整浏览器候选为 Qt WebEngine 或 WPE WebKit。现有 SDK 的这两个包均有图形栈依赖；目前没有取得目标板可用 EGL/OpenGL ES、浏览器内存余量或视频硬解的证据。因此不能承诺当前板卡达到手机浏览器的兼容性、流畅度或高清视频能力。

播放通常只需“解码”，不需要“转码”。转码是解码后再编码，增加 CPU、内存和延迟。若源视频过重，优先在服务器预先转为低分辨率、低帧率 H.264 + AAC，再由板端播放。

## 2. 当前工程可复用基础

以下路径以 `kernel-6.1` 为基准，结论来自本次本地检查。

| 文件 | 已确认的作用 | 使用时的限制 |
| --- | --- | --- |
| `driver_pratice/lcd_test/main.c` | FFmpeg 视频软解码，`sws_scale()` 转 XRGB8888，libdrm dumb buffer + page flip 输出；文件结束后 seek 回起点 | 属于本地循环播放器；不能直接当成完整网络播放器。需要补充网络取消、超时、音频与同步、直播结束及重连处理 |
| `driver_pratice/lcd_test/Makefile` | 链接 avformat、avcodec、swscale、avutil、drm、m | 当前有 all/app/clean；不能假设存在 `check-env` 目标 |
| `driver_pratice/alsa_max98357out/main1.c` | FFmpeg 音频解码，`swr_alloc_set_opts()` / `swr_convert()`，ALSA 写出 | 可参考音频处理流程；与视频合并后仍需时钟同步及资源生命周期管理，源码存在不等于声音已验证 |
| `../buildroot/package/ffmpeg/ffmpeg.mk` | 包版本为 **4.4.4**，配置中启用 network；按选项配置 TLS、协议、解码器、解封装器 | 包版本不代表板上运行版本，也不代表所有协议已编入 |
| `../buildroot/configs/rockchip_rk3506_defconfig` | 引用 LVGL 配置片段，包含 Rockchip RGA 包选项 | 只是配置入口；不能据此认定 LVGL/RGA 已在实际 rootfs 中可用 |
| `../buildroot/package/qt5/qt5webengine/Config.in`、`qt5webengine.mk` | 要求 glibc、线程、动态库、OpenGL/EGL 等；依赖 FFmpeg，并带 `-webengine-ffmpeg` 配置 | 有系统 FFmpeg 集成路径，但具体 Qt/Chromium 与 4.4.4 的编译和运行兼容性仍需验证 |
| `../buildroot/package/wpewebkit/Config.in` | 当前包要求 GLES、EGL、Wayland 等 | 不能仅安装 FFmpeg 就获得网页视频支持 |

`arch/arm/configs/rk3506_defconfig` 中本次未找到 MPP/VDPU/RKVDEC 相关配置项。这里只能说明该配置文件没有给出硬解支持证据，不能据此证明芯片绝对没有某项能力。硬解需同时确认芯片规格、驱动、设备节点、用户态后端及真实样片；RGA 图像处理与 DRM 显示均不等于 H.264/H.265 解码。

本次没有取得有效 Buildroot 输出 `.config`、目标 sysroot 或板端状态，RAM、Flash 剩余空间、运行频率、显示模式、音频设备名均待实测。已有显示示例以 800×480 为设计背景，实际产品仍须以板端模式为准。

## 3. 方案比较与选择

### 3.1 路线 A：LVGL + FFmpeg 4.4 + DRM/KMS + ALSA（建议先落地）

```text
触摸/按键 → LVGL 地址栏、媒体列表、播放控制
                   │ 播放命令 / 状态事件
                   ▼
HTTP(S) 媒体 URL → libavformat → 音视频压缩包队列
                                  ├─ libavcodec → 视频帧 → libswscale → 统一合成 → DRM/KMS
                                  └─ libavcodec → 音频帧 → libswresample → ALSA
业务 API / JSON → libcurl（可选）→ 媒体列表与实际播放 URL
```

范围：受控媒体列表、HTTP/HTTPS MP4、HLS、网络音频；后续按需求增加 RTSP。界面可提供地址栏、返回、收藏、播放暂停、音量和进度，但 LVGL 本身不提供通用 HTML/CSS/JavaScript 浏览器能力。

普通媒体 URL 直接交给 libavformat。不要为了“使用 HTTP”先用 curl 下载整段视频再播放，也不必自行重写 HTTP/HLS。libcurl 适合业务登录、JSON API、封面等；仅在确有自定义鉴权/传输需求时考虑 `AVIOContext`，并承担 seek、取消及子资源访问问题。

### 3.2 路线 B：Qt WebEngine 完整浏览器

Qt 提供地址栏、导航、软键盘和浏览器窗口，WebEngine 负责 DOM/CSS/JavaScript 与网页媒体。网页内 `<video>` 由浏览器媒体管线处理，不是自动调用现有 `lcd_play`。

本 SDK 的包配方依赖系统 FFmpeg，值得作为“保留 4.4”约束下的首个完整浏览器候选。但不能将任意新版 WebEngine 的 FFmpeg 换成 4.4；需固定 SDK 内 Qt/Chromium/FFmpeg 配套版本，检查构建日志、链接库及实际媒体功能。Qt Multimedia 的播放器后端与 Qt WebEngine 网页媒体管线也不是同一件事。

H.264/MP4 等支持受构建选项影响，当前 SDK 有 `BR2_PACKAGE_QT5WEBENGINE_PROPRIETARY_CODECS`。官方也明确说明专有编解码器需在构建时启用。[Qt WebEngine 媒体功能说明](https://doc.qt.io/qt-6.10/qtwebengine-features.html)用于说明概念；具体配置以本 SDK 的 Qt5 配方为准，不能直接套用 Qt6 命令。

实施前必须先通过：工具链依赖检查 → EGL/GL 最小渲染测试 → 空白页 → 简单中文网页及触摸输入 → 自有视频页面 → 目标网站。纯软件图形后端可能有移植空间，但会与视频软解竞争 CPU，不能把“关闭 GPU”视为已解决依赖。

如果简单网页和视频同时运行已无资源余量，应停止扩展任意网站功能，选择更合适的带图形/视频加速及更大内存的平台，或收敛为路线 A。

### 3.3 路线 C：WPE WebKit + Cog 等外壳

WPE 面向嵌入式 Web 场景，网页媒体通常接入 GStreamer；本 SDK 包还要求 GLES/EGL/Wayland。因此它不是“FFmpeg 4.4 加一个 GUI”即可完成的方案。引入 GStreamer 后，解码插件及其所用 FFmpeg 版本也要重新核对，不能认为它会自动使用系统 4.4。[WPE 官方介绍](https://wpewebkit.org/about/a-good-choice.html)、[WPE FAQ](https://wpewebkit.org/about/faq.html)。

若“目前可用库只有 FFmpeg 4.4”意味着不能引入其他媒体框架，则暂不选择路线 C。轻量 HTML 引擎也可用于受控文档页面，但现代 JavaScript、MSE、复杂网站兼容性必须逐项验证，不能据“能显示 HTML”承诺手机浏览器体验。

## 4. 网页地址与真正媒体地址

| 输入或场景 | 正确处理方式 |
| --- | --- |
| `https://host/video.mp4` | libavformat 打开媒体资源；HTTP Range/seek 能力影响拖动与部分 MP4 的访问 |
| `https://host/live/index.m3u8` | HLS 解封装器处理播放列表、分片和相对 URL；先验证固定清晰度子列表 |
| `https://host/watch?id=123` | 通常是 HTML 页面，不是视频；由浏览器处理，或由自有服务 API 返回媒体 URL |
| `<video src="...">` / `<source>` | 受控页面可通过约定接口移交真实媒体 URL；还需解析相对路径、会话和错误 |
| `blob:` + MSE | 浏览器内部对象，不能作为普通 HTTP URL 传给 FFmpeg；JS 可能分别获取并拼接音视频片段 |
| DASH `.mpd` | FFmpeg 有对应解封装路径，但需要核实当前构建及流特征；本项目放在 HLS 后验证 |
| WebRTC | 需要信令、ICE、DTLS/SRTP 等，不属于普通 HTTP 文件/HLS 播放；4.4 不能直接代替浏览器 WebRTC 栈 |
| DRM 加密网站 | 需要浏览器 EME、相应 CDM 及合法授权；普通 FFmpeg 解码不等于可播放受保护内容 |

MSE 是浏览器中由 JavaScript 管理媒体缓冲的 API，解释了为什么“查找网页里一个 mp4 地址”无法覆盖所有站点。[MDN Media Source API](https://developer.mozilla.org/en-US/docs/Web/API/Media_Source_Extensions_API)。

HLS 在 FFmpeg 4.4.4 中有实际实现，但不应据此承诺所有现代 HLS 扩展、低延迟模式和加密方式。首版建议限制为未加密 H.264/AAC 的 MPEG-TS 分片 HLS，点播和直播分别验收。[FFmpeg n4.4.4 HLS 源码](https://raw.githubusercontent.com/FFmpeg/FFmpeg/n4.4.4/libavformat/hls.c)。

对自有网页推荐定义 `play(url, headers, title)` 等受控桥接消息，转交本地播放器，并回传 buffering/playing/error 状态。仅向可信页面开放桥接、验证协议与目标地址，不把 URL 拼接到 shell。对于第三方网站，不能保证这种桥接兼容其登录、清晰度切换、广告和 DRM。

## 5. FFmpeg 4.4 播放器实现路径

### 5.1 基本调用链

```text
avformat_network_init()
avformat_alloc_context() → 设置 interrupt_callback
avformat_open_input(url, options)
avformat_find_stream_info()
av_find_best_stream(VIDEO/AUDIO)
每条选中流：avcodec_find_decoder(codecpar->codec_id)
            → avcodec_alloc_context3()
            → avcodec_parameters_to_context()
            → avcodec_open2()
循环 av_read_frame() → 按 packet->stream_index 分发
avcodec_send_packet() → 循环 avcodec_receive_frame()
视频：sws_getContext()/sws_scale() → 显示帧队列
音频：swr_alloc_set_opts()/swr_init()/swr_convert() → ALSA
停止：取消网络读取、唤醒队列、等待线程退出、释放 FFmpeg/ALSA/显示资源
```

`send_packet()` 返回 EAGAIN 时，先接收输出再重送未被接受的包，不能丢弃；一个包不保证只产生一帧。正常点播 EOF 应发送 NULL 包排空解码器；seek 后清空队列并 `avcodec_flush_buffers()`。参见 [FFmpeg 4.4 send/receive API](https://ffmpeg.org/doxygen/4.4/group__lavc__encdec.html)。

音频沿用 4.4 的 `channel_layout`、`channels`、`av_get_default_channel_layout()`、`swr_alloc_set_opts()`；不要复制新版 `AVChannelLayout` / `swr_alloc_set_opts2()` 示例。ALSA 参数与重采样输出匹配，处理部分写入和 underrun，正常结束排空重采样器及音频输出。

### 5.2 网络与状态机

建议状态：`IDLE → OPENING → BUFFERING → PLAYING ↔ PAUSED → STOPPING → IDLE`，失败进入 ERROR；重连次数有限且可取消。

- GUI 线程只处理输入和绘制；媒体打开、读取、解码放工作线程，不能阻塞触摸响应。
- 在 open 之前设置 `AVFormatContext.interrupt_callback`，以取消标记和单调时钟截止时间终止阻塞调用；每次操作更新截止时间，避免把整个播放时长当作超时。
- `rw_timeout` 单位为微秒，可从 5 秒开始调试；各协议的选项和中断覆盖仍需实测。不要把当前新版文档的所有 reconnect 选项照抄到 4.4。
- HTTP 头、Cookie、Referer、User-Agent 仅按服务要求传入；跨域重定向和 HLS 子请求不得无条件泄露登录凭据。
- HTTPS 需要实际启用的 TLS 后端、CA 证书、正确系统时间及证书/主机名验证；以无效证书测试确认拒绝行为，不以关闭验证解决上线问题。
- 点播结束可停止或按用户选择循环；直播断流走重连，不能复用当前 `lcd_test` 的 EOF seek-to-zero 逻辑。
- 暂停点播可以背压；直播应选择“丢弃旧缓存后回直播边缘”或“有限时移”，必须明确，不允许无限缓存。

4.4 的网络选项以 [FFmpeg n4.4.4 protocols 文档](https://raw.githubusercontent.com/FFmpeg/FFmpeg/n4.4.4/doc/protocols.texi)及实际 `ffmpeg -h protocol=http` 为准。

### 5.3 音画同步与缓存

读取线程分别投递到有上限的音频/视频压缩包队列，解码线程输出短帧队列。队列必须同时限制字节数和时长；满队列时背压或按直播策略丢弃，不能随播放时长持续增长。

有音频时用实际音频播放时钟作主时钟：已提交样本对应的时间减去 ALSA 尚未播放的延迟；不能把 `snd_pcm_writei()` 返回当作已播放，也不能把 `swr_get_delay()` 当作扬声器延迟。视频以 `best_effort_timestamp × time_base` 对齐，过早等待、过晚适度丢帧；无音频时采用单调时钟。seek、重连、时间戳跳变后重建时钟基准。

缓存建议先从点播 1～3 秒、普通直播 0.5～2 秒进行实验；这是应用队列起点，不是端到端延迟承诺，HLS 分片长度和服务器生产延迟还会叠加。以实测内存和卡顿率调整。

### 5.4 GUI 和视频如何同时上屏

首版推荐一个进程统一持有 DRM 显示资源：把视频转为目标矩形像素，与 GUI 控件统一合成到后台缓冲，再 page flip。LVGL 刷新和视频更新由显示线程协调，并处理 pitch、像素格式、宽高比、黑边及缓冲生命周期，避免覆盖和撕裂。

不要同时运行一个独立 LVGL DRM 程序和现有 `lcd_play` 去抢同一个显示控制权。若采用完整浏览器，则通常通过其窗口系统/合成器输出；外置播放器需要走同一个合成系统，或明确切换显示所有权。

后续可评估 DRM overlay plane、RGA 缩放/色彩转换和 DMA-BUF，以减少拷贝，但需先验证 plane 支持的格式、缩放、层级和同步。800×480 的 XRGB8888 单帧约 1.46 MiB，双缓冲约 2.93 MiB；30 fps 每次完整写帧约 46 MB/s，尚不包含解码、额外拷贝、GUI 和扫描输出开销。

## 6. Buildroot 与构建部署

在 SDK 对应 Linux 构建环境中操作，先确认实际 `RK_BUILDROOT_CFG` / output 配置，不能只凭 defconfig 文件名判断当前产品。

路线 A 的依赖建议：

```text
BR2_PACKAGE_FFMPEG=y
BR2_PACKAGE_FFMPEG_FFMPEG=y       # 调试命令行
BR2_PACKAGE_FFMPEG_FFPROBE=y      # 检查媒体与流
BR2_PACKAGE_FFMPEG_SWSCALE=y
BR2_PACKAGE_LIBDRM=y
BR2_PACKAGE_ALSA_LIB=y
BR2_PACKAGE_ALSA_UTILS=y          # 独立检查声音
BR2_PACKAGE_CA_CERTIFICATES=y
# 按实际配方选择 GnuTLS 或 OpenSSL，核实最终 FFmpeg configure 结果
# GUI 使用 SDK 已有 LVGL 包；业务 HTTP/JSON 可另加 libcurl 和 JSON 库
```

这些是候选配置，不表示已经启用。`libswresample` 也必须在 sysroot 和 target 中存在；不要臆造不存在的 Kconfig 选项。当前 `ffmpeg.mk` 优先 GnuTLS，OpenSSL 还受 GPL/NONFREE 组合条件影响，所以“勾选 OpenSSL”不等于 FFmpeg 最终启用 HTTPS。

初期保留较完整的解码器、解封装器和协议以验证功能，再裁剪。至少核对 H.264/AAC、mov（MP4）、mpegts、hls、HTTP/HTTPS/TCP/TLS 及依赖；清单不是可以跨版本直接粘贴的最小配置。若后续启用 DASH、RTSP 或加密 HLS，需要增加相应组件并重测。

原生播放器建议链接：

```makefile
LDLIBS += -lavformat -lavcodec -lavutil -lswscale -lswresample -ldrm -lasound -lm -pthread
# LVGL、curl 等按真实库名和依赖补充；优先使用目标 sysroot 的 pkg-config。
```

流程：确认配置 → 使用 SDK 的 `./build.sh buildroot-config` 等入口修改并按 SDK 方式保存 defconfig → `./build.sh buildroot` → 检查 staging/target 的头文件与库 → 用匹配 sysroot 编译程序 → 重新生成包含依赖的 rootfs 镜像 → 部署 → 在板端确认实际版本和库。只编译应用、只修改 menuconfig 或只看到 target 文件，都不等于板端运行的是新镜像。

## 7. 分阶段实施与验收

| 阶段 | 工作内容 | 完成判据 |
| --- | --- | --- |
| P0 能力盘点 | RAM/Flash/CPU、DRM 模式、触摸、ALSA、FFmpeg/TLS 能力；准备自有测试源 | 保存板端报告，明确哪些库与驱动实际可用 |
| P1 网络媒体核心 | HTTP MP4、音视频解码、统一时间轴、取消/超时 | 无 GUI 下可连续播放，音画同步，停止和断网不永久阻塞 |
| P2 可视化播放器 | LVGL 地址输入、列表、播放暂停、音量、状态/错误提示 | GUI 与视频同时显示，触摸流畅，切换资源无崩溃和资源泄漏 |
| P3 流媒体 | HTTPS、HLS 点播/直播、重连、缓冲、直播边缘策略 | 分片切换、断网恢复、鉴权失败可控，长时间内存有上限 |
| P4 完整浏览器门槛 | Qt WebEngine 构建及图形依赖、中文网页、输入和自有 `<video>` | 明确 4.4 实际链接情况，网页和视频并行时资源可接受 |
| P5 网站兼容性 | 逐个目标站点测试 JS/MSE/登录/全屏/视频 | 以指定站点和流类型形成支持清单，不能用一个演示页代表任意网站 |

性能实验从 320×240/15 fps、640×360/15 或 25 fps 起步，再尝试接近屏幕尺寸的视频；这些是测试档位，不是 RK3506 的保证能力。优先 H.264 8-bit 4:2:0、AAC、单路播放，暂不把 HEVC/AV1/高清实时转码作为首版目标。

板端盘点示例（部分工具需先装入镜像）：

```sh
uname -a
cat /proc/meminfo
cat /proc/cpuinfo
df -h
ls -l /dev/dri/
cat /sys/class/drm/*/status
cat /sys/class/drm/*/modes
aplay -l
ffmpeg -version
ffmpeg -buildconf
ffmpeg -protocols
ffmpeg -demuxers
ffmpeg -decoders
ffmpeg -h protocol=http
ffmpeg -h protocol=tls
```

先用自有媒体 URL 验证读流及解码，命令中的地址需替换：

```sh
ffprobe -v error -show_format -show_streams 'http://YOUR_SERVER/test.mp4'
ffmpeg -benchmark -i 'http://YOUR_SERVER/test.mp4' -t 60 -f null -
ffmpeg -benchmark -i 'http://YOUR_SERVER/live/index.m3u8' -t 60 -f null -
```

输出到 null 仅验证读流与解码速度，不证明 LCD 输出、音频输出或音画同步。随后用真实程序测 CPU、RSS/可用内存、首帧时间、丢帧、音画偏差、网络重连、GUI 响应和温度；建议至少连续运行 2 小时，并反复打开/关闭/seek/断网，检查内存是否持续增长。

HTTP 404、无效证书、DNS 失败、慢网络、无音轨、无视频轨、格式不支持、HLS 分片丢失都要有明确结果。未经板测，不给出“支持 720p/1080p”或“可实时转码”的结论。

## 8. 下一步的具体交付建议

先在本目录新增独立 `http_player` 工程，复用现有显示和音频路径的思路，保留原实验程序。可拆为 `player_core`、`network_input`、`video_output`、`audio_output`、`ui` 五个模块，用命令/事件接口连接，先完成 P0～P3。

同时收集目标板实际 RAM/Flash、屏幕与触摸信息、目标网页和媒体样例，以及“是否必须访问任意第三方网站”的产品约束。若完整网页浏览不可删减，就以 P4 为继续投入的前置门槛；若仅需自有内容门户，路线 A 配合服务器 API 更容易控制资源和实现质量。
