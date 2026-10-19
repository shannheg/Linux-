# RK3506 USB 音频适配与 Windows 测试

更新：2026-10-15。

目标：通过 USB 将 RK3506 做成 Windows 的立体声扬声器和单声道麦克风，设备内部继续完成播放、采集、分频和降噪。

本文根据 [AUDIO_PROJECT_MEMORY.md](../../AUDIO_PROJECT_MEMORY.md)、当前 Linux 6.1 BSP 源码和微软文档整理。当前完成的是方案与源码核对；以下代码块是待实施示例，尚未据此编译、烧录或连板测试。

## 1. 方案与免驱动条件

采用 **DWC2 Device + ConfigFS + UAC2 Gadget + 用户态 audio_engine**。

Windows 10 从 1703 开始内置 UAC2 驱动 `usbaudio2.sys`，Windows 11 也可使用系统类驱动。因此，标准 UAC2 音频功能可以做到插入后自动识别，无需安装项目专用驱动。描述符、格式、时钟控制请求和端点行为必须符合主机要求。[微软 UAC2 驱动说明](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/usb-2-0-audio-drivers)

若必须兼容 Windows 7/8，应另行验证 UAC1 方案，不能假定这些系统原生支持 UAC2。[微软 UAC1 类驱动说明](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/usb-audio-class-system-driver--usbaudio-sys-)

第一版固定参数：

| Windows 功能 | 格式 | 设备侧 UAC ALSA 方向 | ConfigFS 参数 |
| --- | --- | --- | --- |
| 扬声器，PC → RK3506 | 48 kHz、16 bit、双声道 | capture：应用读取 | `c_chmask=3`、`c_srate=48000`、`c_ssize=2` |
| 麦克风，RK3506 → PC | 48 kHz、16 bit、单声道 | playback：应用写入 | `p_chmask=1`、`p_srate=48000`、`p_ssize=2` |

`p/c` 按设备侧 ALSA 命名，USB 的 IN/OUT 按主机视角命名。`chmask` 是通道位掩码，不能把任意声道数量直接写入它。

```text
Windows 播放
    → USB OUT → UAC capture → audio_engine / DSP
    → 物理 ALSA playback → SAI → Codec / 功放 → 扬声器

物理麦克风
    → 物理 ALSA capture → audio_engine / 降噪
    → UAC playback → USB IN → Windows 录音
```

Windows 只需看到双声道输出；PCM3168A 后续四路或六路分频输出由设备内部生成。USB 传输 PCM，这条输入路径可以直接进入 DSP，无需经过 FFmpeg 文件解码。

## 2. 当前 SDK 的基础与缺口

| 已核查内容 | 当前状态与含义 |
| --- | --- |
| 工作区 `.config` | `CONFIG_USB_DWC2=m`、`CONFIG_USB_GADGET=m`、`CONFIG_USB_CONFIGFS=m`；`CONFIG_USB_CONFIGFS_F_UAC2` 未启用 |
| `rk3506_defconfig` | 已有 DWC2、ConfigFS、USB PHY 基础，需要补充 UAC2 配置并验证最终 `.config` |
| `rk3506g-mini-v10.dtsi` | OTG0 为 `peripheral`，OTG1 为 `host`；实际使用哪个插座须核对板卡与原理图 |
| `f_uac2.c`、`u_audio.c` | 已实现 UAC2 功能与虚拟 ALSA 声卡，无需先新写 USB Audio 驱动 |
| `external/rkscript/usbdevice` | 已支持 UAC2 和 `/etc/usbdevice.d/` hook，可以沿用 |
| 用户态数据桥接 | 仍需实现，UAC2 不会自动把数据送到物理 Codec |
| 时钟、音量、静音 | 已有相关接口，仍需音频引擎接入和实机验证 |

这里的 `.config` 是本地工作区快照，不代表当前板上运行内核的配置。

## 3. 实现步骤

### 步骤一：核对硬件接口和 Device 模式

1. 确认 PC 连接的是板上支持 Device/OTG 的 USB 数据接口，核对 D+/D−、PHY、VBUS 检测及接口角色电路。
2. 核对实际选中的 DTS；如果使用 mini 板现有配置，OTG0 已是 `peripheral`，不必重复修改。
3. 确认对应控制器、PHY 节点为 `okay`，内核驱动已启用并加载。

板端 Linux 执行：

```sh
uname -r
ls /sys/class/udc/
dmesg | grep -Ei 'usb|dwc2|phy'
```

验收：`/sys/class/udc/` 下出现设备控制器。若为空，先定位控制器/PHY 驱动、设备树和角色问题，暂不进入 UAC2 调试。

### 步骤二：配置、构建与部署内核和用户空间

以下构建命令在 **SDK 的 Linux 构建环境、SDK 根目录** 执行，不在 Windows PowerShell 中执行。先确认当前板级配置、`kernel` 指向的内核版本、`RK_KERNEL_CFG` 和 `RK_KERNEL_DTS`。

```sh
./build.sh kernel-config
```

在菜单中启用 `USB Gadget functions configurable through configfs` 下的 `Audio Class 2.0`。关键配置为：

```text
CONFIG_USB_CONFIGFS_F_UAC2=y
```

保持 `CONFIG_SND`、`CONFIG_SND_PCM`、USB Gadget、ConfigFS、DWC2 和 Rockchip USB2 PHY 可用；DWC2 必须支持 Peripheral 或 Dual Role。相关音频功能依赖由 Kconfig 选择，父项可以沿用已有模块方式。

本 SDK 的 `kernel-config` 会执行 `menuconfig`、`savedefconfig`，并回写当前 `RK_KERNEL_CFG`。构建时还会合并配置片段，因此要检查生成后的配置，避免只修改临时 `.config`。

```sh
./build.sh kernel
./build.sh kernel-modules
```

第二条用于构建模块并安装到 SDK 输出的模块暂存目录；实际目录以构建日志为准。`CONFIG_SND_USB_AUDIO` 是 Linux 作为 Host 使用外部 USB 声卡的驱动，不能代替 Gadget UAC2。

在 SDK 的配置菜单中启用 USB Gadget 和 UAC2：

```sh
./build.sh menuconfig
```

对应选项为 `RK_USB_GADGET`、`RK_USB_UAC2`。第一轮可只保留 UAC2；若仍需 ADB，配置成同一个复合 Gadget 的功能。UAC2 音频部分免专用驱动，不代表 ADB 等其他功能也采用同一驱动。

根文件系统需要包含 `usbdevice`、ALSA 库与 `aplay`、`arecord`、`amixer`；自测时可加入 `speaker-test`。检查实际 Buildroot 配置及目标文件，再重新生成包含模块、工具和启动配置的文件系统镜像。

部署时更新实际启动所用的内核镜像、需要变更的 DTB 和对应模块/根文件系统。验收要区分“构建成功”“文件已进入镜像”“板上运行新版本”。模块版本应匹配 `uname -r`，不能仅替换内核而遗漏新增模块。

### 步骤三：沿用 SDK 启动脚本配置 UAC2

先通过串口或网络登录板卡。重配 Gadget 会中断同一 USB 上的 ADB 连接。

模块构建时，确认对应模块存在并加载；内建项无需重复加载：

```sh
modprobe phy-rockchip-inno-usb2
modprobe dwc2
modprobe libcomposite
modprobe usb_f_uac2
```

不要让另一个独立 Gadget 脚本或 `g_audio` 同时占用同一 UDC。以下方案统一交由 SDK 的 `/usr/bin/usbdevice` 管理。

实施时，将下面内容保存为板端 `/etc/usbdevice.d/50-uac2.sh`，使用 LF 换行。该文件由 `usbdevice` 加载，不是独立运行脚本。若根文件系统只读，应通过 SDK overlay/hook 打包进镜像。

```sh
#!/bin/sh

# 第一轮只启用 UAC2；需要保留 ADB 时改为 "uac2 adb"。
USB_FUNCS="uac2"
UAC2_INSTANCES="uac2.0"

uac2_post_prepare_hook()
{
    # SDK 此时已进入 functions/uac2.0，尚未链接配置或绑定 UDC。
    # Windows 播放 → 设备侧 capture。
    echo 3 > c_chmask || return 1
    echo 48000 > c_srate || return 1
    echo 2 > c_ssize || return 1
    echo async > c_sync || return 1

    # 设备侧 playback → Windows 录音。
    echo 1 > p_chmask || return 1
    echo 48000 > p_srate || return 1
    echo 2 > p_ssize || return 1

    # 首轮不声明尚未映射到实际音频处理的设备端控制。
    echo 0 > c_mute_present || return 1
    echo 0 > c_volume_present || return 1
    echo 0 > p_mute_present || return 1
    echo 0 > p_volume_present || return 1
}
```

注意：本 BSP 的 `u_uac2.h` 默认 `c_srate=64000`，必须显式设置 48000。`c_sync=async` 启用主机播放方向的显式反馈。

SDK 会先读取旧式 `.usb_config`，再加载 hook；此例的 `USB_FUNCS` 会将功能固定为 UAC2。检查其他 hook，避免重复定义同一函数或覆盖这些变量。

固化时，可在 SDK 源码目录保存该 hook，并将其路径加入 `RK_USB_HOOKS`（保留已有必要 hook）。安装流程支持相对 SDK 根目录的路径，并将文件装入目标 `/etc/usbdevice.d/`。本文没有额外创建该脚本文件，需要实施时按代码块保存。

先停止使用 UAC 声卡的音频引擎、`aplay` 和 `arecord`，再执行：

```sh
/usr/bin/usbdevice restart
tail -n 80 /var/log/usbdevice.log
```

`restart` 会解绑 UDC、移除旧功能链接并重建功能，然后执行 prepare hook、链接配置、绑定 UDC。修改参数后不要仅对运行中的实例再次 `start`，否则可能因实例正在使用而报 `Device or resource busy`。

确认配置和枚举状态：

```sh
UAC_FUNC=/sys/kernel/config/usb_gadget/rockchip/functions/uac2.0
for attr in c_chmask c_srate c_ssize c_sync p_chmask p_srate p_ssize; do
    printf '%s=' "$attr"
    cat "$UAC_FUNC/$attr"
done
cat /sys/kernel/config/usb_gadget/rockchip/UDC
cat /sys/class/udc/*/state
cat /sys/class/udc/*/current_speed
cat /proc/asound/cards
aplay -l
arecord -l
```

Windows 连接并配置设备后，UDC 状态应进入 `configured`，Linux 应出现 UAC 虚拟声卡。记录实际协商速率；优先验证 High-Speed，若产品允许 Full-Speed，再单独测试相应兼容性。

SDK 默认会选择发现的第一个 UDC；若有多个控制器，实施时应明确绑定到所连接的那个。产品名、VID/PID、序列号也应记录，正式产品使用项目分配的标识和稳定唯一序列号。

### 步骤四：先验证 USB 双向 PCM

声卡号动态分配，不能预设 UAC 永远是 `hw:2,0`。从 `/proc/asound/cards` 的方括号内读取实际 CARD id，以下仅以 `UAC2Gadget` 举例：

```sh
UAC_CARD=UAC2Gadget
```

**PC → 板端：** Windows 选择该 USB 扬声器，播放已知左右声道测试音；板端从 UAC capture 录制 10 秒：

```sh
arecord -D "hw:CARD=$UAC_CARD,DEV=0" \
    -t wav -f S16_LE -r 48000 -c 2 -d 10 /tmp/usb-from-pc.wav
```

检查 WAV 的时长、波形、电平和左右声道。可复制到 PC 分析，或用已验证的本地声卡播放；此步骤可以先不依赖物理扬声器。

**板端 → PC：** 准备一个 48 kHz、16 bit、单声道、非满幅的已知 WAV 文件，例如 `/tmp/mono-48k-s16.wav`。Windows 先打开该 USB 麦克风录音，再在板端执行：

```sh
aplay -D "hw:CARD=$UAC_CARD,DEV=0" /tmp/mono-48k-s16.wav
```

检查 Windows 录制文件能否得到预期信号。主机未打开录音端点时，设备侧播放可能等待或填满缓冲；不能据此直接判定 USB 故障。

两步分别成功后再接真实 Codec 和麦克风，以便区分 USB 问题与本地音频硬件问题。

### 步骤五：加入真实扬声器与麦克风桥接

首先单独验证物理声卡：用本地 WAV 播放出声、用 `arecord` 获得有效麦克风数据。确定实际 CARD id、设备号、声道数和格式。

以下为短时验证示例。将变量替换成实际声卡 id；示例假设物理声卡的 PCM 设备号为 0，实际不同则相应修改。两个方向分别在两个终端运行：

```sh
UAC_CARD=UAC2Gadget
SPK_CARD=REPLACE_WITH_SPEAKER_CARD_ID
MIC_CARD=REPLACE_WITH_MIC_CARD_ID

# PC → UAC capture → 物理扬声器。
arecord -D "hw:CARD=$UAC_CARD,DEV=0" -t raw -f S16_LE -r 48000 -c 2 | \
    aplay -D "plughw:CARD=$SPK_CARD,DEV=0" -t raw -f S16_LE -r 48000 -c 2
```

```sh
UAC_CARD=UAC2Gadget
MIC_CARD=REPLACE_WITH_MIC_CARD_ID

# 物理麦克风 → UAC playback → PC；Windows 同时打开录音。
arecord -D "plughw:CARD=$MIC_CARD,DEV=0" -t raw -f S16_LE -r 48000 -c 1 | \
    aplay -D "hw:CARD=$UAC_CARD,DEV=0" -t raw -f S16_LE -r 48000 -c 1
```

`plughw` 便于首轮协商格式，不代表所有物理声卡都会接受这些参数，也不负责两个独立时钟的长期同步。多通道 Codec 的通道映射要在正式引擎中明确，不能把默认转换当作分频路由。

正式 `audio_engine` 需要实现：

1. 使用稳定的 CARD id 打开四条 PCM 通路，协商各自的格式、period 和 buffer。
2. 处理 `snd_pcm_readi()` / `snd_pcm_writei()` 短读写、`EAGAIN`、xrun、挂起及设备移除；普通 xrun 可结合 `snd_pcm_recover()`，设备重新枚举后需重新打开。
3. 为两条数据路径设置环形缓冲，记录水位、丢帧和欠载。实时线程避免阻塞式日志、文件操作和 UI 调用。
4. 接入 PCM 格式转换、必要的重采样、播放 DSP 和采集处理；UI 通过控制接口改参数。
5. 处理主机播放/录音的开始和停止、USB 拔插与重新枚举，避免停流后旧数据继续播放。

第一版 USB 使用 `S16_LE`，内部可转浮点处理，再转为物理声卡格式。本树 `ssize=3` 对应 `S24_3LE`，`ssize=4` 对应 `S32_LE`；不能把三字节 USB 24 bit 数据直接当成四字节容器的 `S24_LE`。

### 步骤六：完成时钟、音量与静音闭环

USB 与本地 SAI/Codec 即使都标称 48 kHz，实际速率也可能不同。单纯加大缓冲只能延后满溢或欠载，不能解决长期漂移。

推荐首版监控两个方向的缓冲水位，围绕目标水位小幅平滑调整重采样比率；动态补偿应有范围限制和重置策略。后续可评估使用真实音频消费/生产速率调节 USB 反馈或硬件音频时钟。

源码已有 `Capture Pitch 1000000`、`Playback Pitch 1000000` 控件，以及 `USB_STATE=SET_AUDIO_CLK` / `PPM=` 通知。它们提供调节或测量接口，没有自动读取物理 Codec 的消费进度并完成闭环。Windows 异步 OUT 要求显式反馈，保留并验证对应端点。[微软同步类型要求](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/usb-2-0-audio-drivers)

首版 hook 关闭了设备侧音量/静音控制。后续启用对应 `*_present=1` 前，应让音频引擎订阅 UAC ALSA 控制变化，并映射到 DSP 增益或 Codec mixer；采用短渐变避免突变。当前 `u_audio.c` 的 PCM 搬运不自动应用这些增益，Windows 滑块变化不能单独证明实际音量生效。

全双工通话场景还要评估 AEC；RNNoise 降噪不能代替声学回声消除。

## 4. Windows 识别和测试步骤

1. 运行 `winver` 记录系统版本。连接数据线，运行 `devmgmt.msc`，检查音频设备是否正常、是否出现问题代码。
2. 在“声音、视频和游戏控制器”下检查对应设备的驱动详情，确认使用 Microsoft 的 `usbaudio2.sys`。设备名可能来自产品字符串，也可能显示为 USB Audio Class 2 Device。
3. 运行 `mmsys.cpl`，分别在“播放”和“录制”页找到设备，选择作为测试应用的输出/输入；确认报告格式与 48 kHz、16 bit、2/1 声道配置一致。
4. 使用 USBView 核对 VID/PID、序列号、连接速率、音频接口和端点。USBView 可随 Windows SDK 的 Debugging Tools 安装。[微软 USBView 说明](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/usbview)
5. 按步骤四分别验证两个 USB 方向，再按步骤五接入真实硬件。录音应用无输入时检查选中的设备及麦克风访问权限。
6. 同时播放和录音，检查左右声道、单声道采集和串音。基线测量关闭可选音效、自动增益和降噪，记录主机混音格式；Windows 共享模式可能进行转换，不能仅凭应用源文件格式认定 USB 上的格式。

Windows 使用系统音频接口即可进行播放和录音测试，无需先开发 Windows 端应用或专用驱动。

## 5. 验收顺序与故障定位

| 阶段/现象 | 验证重点 |
| --- | --- |
| UDC 目录为空 | Device 模式、控制器/PHY 驱动、设备树状态 |
| 有 UDC，Windows 完全无反应 | 数据线、连接的物理接口、VBUS、UDC 是否已绑定、其他 Gadget 是否占用 |
| 已枚举但无音频端点或 Code 10 | UAC 描述符、格式/时钟请求、反馈端点、Windows 事件日志与板端内核日志 |
| Linux 无 UAC 声卡 | UAC2 模块、创建功能是否成功、`usbdevice.log` |
| Windows 播放但扬声器无声 | 先验证 UAC capture 文件；有数据则检查桥接、物理声卡、Codec/功放与 mute |
| Windows 麦克风无信号 | 先用已知 WAV 写 UAC playback；随后检查物理采集、桥接方向和应用权限 |
| 运行一段时间后爆音 | 双向缓冲水位趋势、xrun、时钟漂移、实时线程调度 |
| 改参数报 busy | 停止 PCM 使用者后 `usbdevice restart`，检查是否仍有其他管理程序占用 |
| 音量/静音只改变界面 | 检查设备是否声明控制、引擎是否接收并应用控制变化 |

建议先做 10 分钟双向测试，再做 1 小时和 8 小时稳定性测试；这些是项目建议门槛，不是已达到的指标。

长测记录 Windows 版本、USB 端口/Hub、内核与根文件系统版本、采样格式、双向缓冲水位范围、xrun/丢帧次数、CPU 占用和端到端延迟。验收还包括拔插、播放/录音应用反复打开关闭、主机睡眠恢复、音量/静音及板端负载变化。数字通路稳定后，再单独评估声学噪声、回声和音质。

## 6. 源码定位与实施清单

| 路径 | 用途 |
| --- | --- |
| [drivers/usb/gadget/Kconfig](../../drivers/usb/gadget/Kconfig) | `USB_CONFIGFS_F_UAC2` 配置与依赖 |
| [f_uac2.c](../../drivers/usb/gadget/function/f_uac2.c) | UAC2 描述符、控制请求、ConfigFS 属性 |
| [u_audio.c](../../drivers/usb/gadget/function/u_audio.c) | 虚拟 ALSA PCM、反馈、pitch、音量/静音通知、PPM 辅助 |
| [u_uac2.h](../../drivers/usb/gadget/function/u_uac2.h) | 默认通道、采样率和样本字节数 |
| [configfs-usb-gadget-uac2](../../Documentation/ABI/testing/configfs-usb-gadget-uac2) | ConfigFS 属性定义 |
| [gadget-testing.rst](../../Documentation/usb/gadget-testing.rst) | UAC2 用户态桥接测试示例 |
| [mini 板设备树](../../arch/arm/boot/dts/rk3506g-mini-v10.dtsi) | USB0/USB1 角色及 PHY 使能 |
| [usbdevice](../../../external/rkscript/usbdevice) | Gadget 创建、hook、解绑与重建 |
| [Config.in.usb-gadget](../../../device/rockchip/common/configs/post/Config.in.usb-gadget) | SDK UAC2、标识和 hook 配置入口 |
| [10-kernel.sh](../../../device/rockchip/common/build-hooks/10-kernel.sh) | 内核配置、构建与模块安装入口 |

待实施：

- [ ] 核对实际板卡、USB 插座、DTS 和运行镜像。
- [ ] 启用并持久保存 UAC2 内核配置，完成构建与部署。
- [ ] 将 UAC2 hook、音频工具及模块打包进入目标文件系统。
- [ ] Windows 自动加载系统驱动并出现扬声器和麦克风。
- [ ] 两个方向分别通过已知 PCM 信号验证。
- [ ] 接通真实扬声器、麦克风和独立音频引擎。
- [ ] 完成双向漂移补偿、音量/静音映射及断线恢复。
- [ ] 完成长时间双向和 Windows 拔插/睡眠恢复测试。
