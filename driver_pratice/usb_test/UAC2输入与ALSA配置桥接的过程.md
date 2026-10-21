# UAC2 输入与 ALSA 配置桥接的过程

初稿日期：2026-10-20；更新日期：2026-10-21。

第 1～11 节保留配置与基本桥接说明；第 12～18 节补充从初始代码到当前程序的修改记录、函数解析、启动原理及排查方法。当前程序见 [uac2_in_pcm5102_out.c](uac2_in_pcm5102_out.c)，构建入口见 [Makefile](Makefile)。

本文整理本次对话中的 RK3506 UAC2 开启、ConfigFS 配置、虚拟声卡识别及 PCM5102 桥接流程，并按当前 SDK 源码核对。文中的 SDK 构建命令在 Linux 构建环境执行；板端命令在 RK3506 Linux 执行，不在 Windows PowerShell 执行。

## 1. 本次对话已经确认的板端状态

用户提供的输出先后确认了以下事实：

- USB0 对应 `dwc2 ff740000.usb`，能够以 High-Speed 完成枚举。
- 最初 `/var/run/.usbdevice` 为 `adb`，`/etc/profile.d/usbdevice.sh` 为 `USB_FUNCS="adb"`，ConfigFS 仅有 `ffs.adb` 及其配置链接。当时没有创建 UAC2 功能。
- 根文件系统为 `/dev/root / squashfs ro,...`。SquashFS 本身只读，不能通过 `mount -o remount,rw /` 改成可写。
- 后续板端已经出现以下两张声卡：

```text
 0 [rk3506pcm5102  ]: simple-card - rk3506-pcm5102
                      rk3506-pcm5102
 1 [UAC2Gadget     ]: UAC2_Gadget - UAC2_Gadget
                      UAC2_Gadget 0
```

这证明板端 UAC2 虚拟 ALSA 声卡已经注册，不能据此认定 USB 音频传输、PCM5102 桥接和长期稳定性都已验证。用户尚未提供最终 `aplay -l`、`arecord -l` 或 `/proc/asound/pcm` 的输出。

2026-10-21 整理时电脑工作区 `.config` 中仍为 `# CONFIG_USB_CONFIGFS_F_UAC2 is not set`。这是本地配置快照，不等于板上运行内核的配置；应分别核对构建目录、部署镜像和运行版本。

## 2. CONFIG_USB_CONFIGFS_F_UAC2 在哪里开启

在 SDK 根目录执行：

```sh
./build.sh kernel-config
```

菜单路径：

```text
Device Drivers
  → USB support
    → USB Gadget Support
      → USB Gadget functions configurable through configfs
        → [*] Audio Class 2.0
```

也可以在 menuconfig 中按 `/`，搜索 `USB_CONFIGFS_F_UAC2`，查看依赖和所在位置。对应配置为：

```text
CONFIG_USB_CONFIGFS_F_UAC2=y
```

该开关在 [drivers/usb/gadget/Kconfig](../../drivers/usb/gadget/Kconfig) 定义，是 `bool` 类型。其直接依赖为 `USB_CONFIGFS`、`SND`，并自动选择 `USB_LIBCOMPOSITE`、`SND_PCM`、`USB_U_AUDIO`、`USB_F_UAC2`。

| 基础项 | 作用 | 本地配置核对结果 |
| --- | --- | --- |
| `CONFIG_SND`、`CONFIG_SND_PCM` | ALSA 声卡与 PCM 支持 | 均为 `y` |
| `CONFIG_USB_GADGET` | USB Device/Gadget 框架 | `m` |
| `CONFIG_USB_CONFIGFS` | 用 ConfigFS 组合 USB 功能 | `m` |
| `CONFIG_CONFIGFS_FS` | ConfigFS 文件系统 | `m` |
| `CONFIG_USB_DWC2` | USB 控制器驱动 | `m` |
| `CONFIG_USB_DWC2_DUAL_ROLE` | 包含 Device 能力 | `y` |
| `CONFIG_PHY_ROCKCHIP_INNO_USB2` | Rockchip USB2 PHY | `m` |

这些父项可保持当前模块方式，不必为了 UAC2 全部改成内建。`CONFIG_SND_USB_AUDIO` 用于 Linux 作为 Host 连接外部 USB 声卡，不能代替 Gadget UAC2。

SDK 的 [10-kernel.sh](../../../device/rockchip/common/build-hooks/10-kernel.sh) 会在 `kernel-config` 中执行 `menuconfig`、`savedefconfig`，并回写当前 `RK_KERNEL_CFG` 指定的配置文件。不要只改临时 `.config`；构建还可能合并配置片段，应核对最终配置。

后续构建入口如下，本文没有执行这些命令：

```sh
./build.sh kernel
./build.sh kernel-modules
```

模块安装目录以构建输出为准。部署时应保证内核、模块与板端 `uname -r` 匹配；新增模块也必须进入实际启动的文件系统。

## 3. 开启内核选项后，为什么还需要运行时配置

内核选项提供 UAC2 驱动能力，不会直接将现有 USB Device 切换成音频设备。还需要创建 UAC2 功能实例、设置参数、加入 USB 配置，最后绑定 UDC。

```text
设备树使能 USB 控制器、PHY，并确定 Device 角色
    → 内核提供 DWC2、Gadget、ConfigFS 和 UAC2 驱动
    → usbdevice 创建 functions/uac2.0
    → 配置音频参数
    → 创建 configs/b.1/f-uac2.0 链接
    → 将 USB 控制器名称写入 Gadget 的 UDC
    → 绑定期间注册板端 UAC2 虚拟 ALSA 声卡
    → PC 连接并枚举，获得 USB 音频接口
```

声卡注册不需要等待 Windows 开始播放，也不依赖 PCM5102 桥接程序。`USB_STATE=CONFIGURED` 只说明当前 Gadget 已被主机配置，单独的 ADB Gadget 也能出现这一状态。

当前 [dp-rk3506g-mini.dts](dp-rk3506g-mini.dts) 包含 [rk3506g-mini-v10.dtsi](../../arch/arm/boot/dts/rk3506g-mini-v10.dtsi)，其中 USB0 为 `peripheral`、USB1 为 `host`，控制器和 PHY 已使能。控制器基础定义在 [rk3502.dtsi](../../arch/arm/boot/dts/rk3502.dtsi)。因此，按这份 DTS 构建时不必再增加一个 UAC2 设备树节点；实际 PC 接口仍需对应 OTG0 的硬件接线。

ADB 和 UAC2 可以作为同一个复合 Gadget 的两个功能，共用同一个 UDC。应由同一个 `usbdevice` 流程管理，避免另外启动独立的 `g_audio` 或另一套脚本争用控制器。

## 4. 为什么加入 /etc/usbdevice.d/50-uac2.sh，文件名是否固定

当前 SDK 的 [/usr/bin/usbdevice 源码](../../../external/rkscript/usbdevice) 支持加载自定义 hook：

```sh
if [ -d /etc/usbdevice.d ]; then
    for hook in $(find /etc/usbdevice.d/ -type f); do
        source "$hook"
    done
fi
```

加入该文件的目的是复用 SDK 现有的 Gadget 生命周期，设置功能组合、采样参数和自定义行为，而不用复制一整套创建 Gadget 的脚本。

**文件名不要求是 `50-uac2.sh`。** 这是本次示例采用的名称，也可以是 `uac2.sh`、`usb-audio.sh` 等。当前实现有以下具体规则：

- 加载入口目录是 `/etc/usbdevice.d/`，不是任意目录；文件仅放在 `/userdata` 下不会自动生效。
- 使用 `find ... -type f`，没有按 `.sh` 后缀筛选；备份文件也可能被加载，不要把旧版脚本留在该目录中。
- 当前没有 `sort`，不能认为 `50-` 一定决定执行顺序。避免多个文件重复覆盖相同变量或定义同名函数。
- 文件通过 `source` 在当前 Shell 中加载，不是单独启动的进程，因此通常不需要可执行权限；SDK 也以 `0644` 安装 hook。
- 建议使用 LF 换行，路径和文件名不含空格。
- `uac2_post_prepare_hook` 这样的函数名由 SDK 调度规则决定，不能随意改名。修改文件名不影响调用，修改函数名则可能不再被调度。

`50-uac2.sh` 不是内核要求的文件，也不是启用 UAC2 的唯一办法。如果只需 SDK 默认配置，可以在 SDK `menuconfig` 中开启 UAC2；本例使用 hook，是为了明确设置 48 kHz、声道、位宽等参数，并保留 ADB。

SDK 配置入口：

```sh
./build.sh menuconfig
```

需要的选项：

```text
RK_USB_GADGET=y
RK_USB_UAC2=y
```

需要 ADB 时同时保留原有 ADB 选项。定义见 [Config.in.usb-gadget](../../../device/rockchip/common/configs/post/Config.in.usb-gadget)。

配置加载顺序为：`/etc/profile` 引入默认环境 → `usb_load_config` 尝试读取旧式 `.usb_config` → 加载 `/etc/usbdevice.d/` 中的 hook → 规范化和排序功能列表。因此，本例 hook 的 `USB_FUNCS` 可以覆盖默认的 `adb`；其他 hook 也可能再次覆盖它。

## 5. 完整 hook 示例及内部语句详解

以下内容是板端 `/etc/usbdevice.d/50-uac2.sh` 的基础完整示例。目前工程已有实际 [50-uac2.sh](50-uac2.sh)，该文件还包含 `Infinity DP_Industry Codec` 产品和功能名称配置；自动安装使用实际文件，而不是从本文代码块生成文件。

```sh
#!/bin/sh

USB_FUNCS="uac2 adb"
UAC2_INSTANCES="uac2.0"

uac2_post_prepare_hook()
{
    # update 会重复调用 prepare；已链接时不要重复写入属性。
    if [ -L /sys/kernel/config/usb_gadget/rockchip/configs/b.1/f-uac2.0 ]; then
        return 0
    fi

    # PC 播放 → 板端 UAC capture：48 kHz / 16 bit / 双声道。
    echo 3 > c_chmask || return 1
    echo 48000 > c_srate || return 1
    echo 2 > c_ssize || return 1
    echo async > c_sync || return 1

    # 板端 UAC playback → PC 录音：48 kHz / 16 bit / 单声道。
    echo 1 > p_chmask || return 1
    echo 48000 > p_srate || return 1
    echo 2 > p_ssize || return 1

    # 暂不声明尚未接入实际音频处理的静音、音量控制。
    echo 0 > c_mute_present || return 1
    echo 0 > c_volume_present || return 1
    echo 0 > p_mute_present || return 1
    echo 0 > p_volume_present || return 1

    return 0
}
```

### 5.1 功能组合、实例与回调时机

| 语句 | 含义 |
| --- | --- |
| `#!/bin/sh` | 标明 Shell 脚本用途；实际被 `source` 时由调用者的 Shell 解析 |
| `USB_FUNCS="uac2 adb"` | 在同一个 Gadget 中启用 UAC2 和 ADB；只需音频时可改成 `"uac2"` |
| `UAC2_INSTANCES="uac2.0"` | 指定 UAC2 实例名，对应 `functions/uac2.0`；当前 SDK 未指定时也默认使用 `uac2.0` |
| `uac2_post_prepare_hook()` | UAC2 prepare 阶段的后置回调，配置链接与绑定之前执行 |
| `if [ -L .../f-uac2.0 ]; then return 0; fi` | 已存在该实例的配置链接时成功返回，防止运行中的重复属性写入 |
| `echo 值 > 属性` | 向内核提供的 ConfigFS 属性写入配置，不是在普通目录中创建配置文本 |
| `|| return 1` | 某条写入失败时立即返回失败，交给 SDK 记录和处理 |
| `return 0` | 当前函数成功完成；不是退出整个 `usbdevice` 脚本 |

SDK 的 `usb_run_stage uac2 prepare` 按 `uac2_pre_prepare_hook` → `uac2_prepare` → `uac2_post_prepare_hook` 调用存在的函数。调用前已 `cd` 到 `functions/uac2.0`，所以 `c_chmask` 等属性可用相对路径。

`rockchip`、`b.1` 和 `f-uac2.0` 的检查路径对应当前 SDK 默认 Gadget、配置及链接命名。如果将实例改成 `uac2.1` 或修改 SDK 命名，保护路径也必须同步调整。

修改参数后执行 `usbdevice restart`：停止阶段会解绑 UDC、移除旧链接并尝试删除旧 UAC2 实例；随后重建并写入新参数。单纯再次执行 `start` 或等待 `update`，会因保护逻辑跳过已有实例的参数配置。

### 5.2 音频参数

`p/c` 从板端 ALSA 视角命名；USB 的 IN/OUT 从 PC 主机视角命名：

| 场景 | USB 方向 | 板端 UAC ALSA 方向 | 应用操作 |
| --- | --- | --- | --- |
| PC 播放到板子 | OUT | capture，`c_*` | `arecord` / `snd_pcm_readi()` |
| 板子提供音频给 PC 录音 | IN | playback，`p_*` | `aplay` / `snd_pcm_writei()` |

| 属性 | 示例值 | 含义 |
| --- | --- | --- |
| `c_chmask` | `3` | 通道位掩码 `0b11`，启用两个声道；不是直接填写声道数量 |
| `c_srate` | `48000` | PC → 板端音频的采样率，48 kHz |
| `c_ssize` | `2` | 每声道样本占 2 字节，当前驱动映射为 `S16_LE` |
| `c_sync` | `async` | 异步 OUT 模式，启用该方向的显式反馈端点；不等于已完成与 SAI 的时钟同步 |
| `p_chmask` | `1` | 位掩码 `0b1`，启用一个声道 |
| `p_srate` | `48000` | 板端 → PC 音频的采样率 |
| `p_ssize` | `2` | 每声道样本占 2 字节，`S16_LE` |
| `c_mute_present`、`p_mute_present` | `0` | 不向主机声明相应方向的 UAC 静音控制 |
| `c_volume_present`、`p_volume_present` | `0` | 不向主机声明相应方向的 UAC 音量控制 |

当前 [u_uac2.h](../../drivers/usb/gadget/function/u_uac2.h) 默认 capture 采样率为 64000，所以本例明确写入 48000。当前 [u_audio.c](../../drivers/usb/gadget/function/u_audio.c) 将 `ssize=3` 映射为 `S24_3LE`，`ssize=4` 映射为 `S32_LE`；不要将三字节打包的 `S24_3LE` 直接当成四字节容器的 `S24_LE`。

关闭 UAC 音量和静音声明，不代表所有主机侧软件音量都失效。以后若开启设备端控制，需要将控制变化实际映射到 DSP 或物理声卡；不能只看到主机滑块变化就认定音频增益已生效。

本例声明的单声道 PC 录音端点仍需程序提供音频。PCM5102 是播放 DAC，本身不提供麦克风采集，创建 UAC2 录音端点不会自动获得真实麦克风信号。

## 6. 反复断开、枚举与 hook 重复调用保护

用户曾提供约每三秒重复出现 `CONFIGURED`、`DISCONNECTED`、`read descriptors`、`read strings`、`bound driver` 的日志。这些日志表明设备在反复初始化、绑定和枚举，不能单凭它们认定 ADB 与 UAC2 在争抢 UDC。

当前 [61-usbdevice.rules](../../../external/rkscript/61-usbdevice.rules) 对 USB 状态变化调用 `usbdevice update`。`update` 会再次经过 prepare；而 [f_uac2.c](../../drivers/usb/gadget/function/f_uac2.c) 的属性写入在实例已被引用时可能返回 `-EBUSY`。

因此，缺少保护的 hook 存在以下循环风险：状态变化 → update → 重写已链接 UAC2 属性 → EBUSY → SDK 停止、重试 → 再次枚举。第 5 节的链接检查用于避免这一条路径。这是源码分析得到的可能原因，本次对话未提供修正后的完整日志，不能写成已确认的唯一根因或已实测修复。

板端检查：

```sh
tail -n 150 /var/log/usbdevice.log
dmesg | tail -n 60
```

重点查找 `Handling update request`、`uac2_post_prepare_hook`、`Device or resource busy`、`Retrying update request`、`Stopping functions`。如果没有相应错误，应继续检查 ADB 进程退出、其他管理脚本、主机侧行为等，不能仅凭循环周期下结论。

进程列表里有一个常驻 `usbdevice update` 也不自动等于循环重启：SDK 会启动一个每 0.5 秒检查、拉起 ADB 的后台 Shell，它可能保留原命令名。此前 `top` 中 `36.4` 等数值对应 `%VSZ`，右侧 `%CPU` 才是 CPU 占用；应结合顶部 CPU 汇总判断整机负载。

## 7. SquashFS 只读时如何放入 hook

板端已确认：

```text
/dev/root / squashfs ro,relatime,errors=continue 0 0
```

SquashFS 无法通过 remount 改为可写。短时调试可用可写目录绑定挂载；以下假设原目录存在，且完整脚本已放在 `/userdata/50-uac2.sh`：

```sh
mkdir -p /tmp/usbdevice.d
cp -a /etc/usbdevice.d/. /tmp/usbdevice.d/
cp /userdata/50-uac2.sh /tmp/usbdevice.d/
mount -o bind /tmp/usbdevice.d /etc/usbdevice.d
ls -l /etc/usbdevice.d/
```

挂载后可以编辑 `/tmp/usbdevice.d/50-uac2.sh`，通过 `/etc/usbdevice.d` 看到同一内容。该方法不修改原 SquashFS 镜像，重启后临时挂载及 `/tmp` 内容不保留；再次执行前应检查现有挂载，避免重复叠加。

通过串口或网络执行以下命令，因为重配 USB 会暂时断开同一接口上的 ADB：

```sh
/usr/bin/usbdevice restart
cat /var/run/.usbdevice
cat /proc/asound/cards
tail -n 80 /var/log/usbdevice.log
```

长期使用时，将脚本保存在 SDK 源码中，再通过 `RK_USB_HOOKS` 配置其路径，保留已有必要 hook。安装脚本支持相对 SDK 根目录的路径或板级目录下的路径，会以 `0644` 安装到 `/etc/usbdevice.d/`。需重新生成、部署实际使用的 rootfs；仅在 SDK 修改选项，不会自动更新板上已有文件。

工程现在已有 [50-uac2.sh](50-uac2.sh)。本次已在 SDK 实际 [usb-gadget/install.sh](../../../device/rockchip/common/overlays/rootfs/usb-gadget/install.sh) 中增加默认安装步骤，并同步修改用户提供的 [install.sh 副本](install.sh)。启用 `RK_USB_GADGET` 和 `RK_USB_UAC2` 后，不额外填写 `RK_USB_HOOKS` 也会安装本地 UAC2 hook。

### 7.1 自动安装 50-uac2.sh 的具体修改

新增位置在 `mkdir -p "$TARGET_DIR/etc/usbdevice.d"` 之后、原有 `for hook in $RK_USB_HOOKS` 循环之前：

```sh
if [ -n "$RK_USB_UAC2" ]; then
    UAC2_HOOK="$RK_SDK_DIR/kernel-6.1/driver_pratice/usb_test/50-uac2.sh"
    message "Installing default UAC2 hook: $UAC2_HOOK"
    install -m 0644 "$UAC2_HOOK" \
        "$TARGET_DIR/etc/usbdevice.d/50-uac2.sh"
fi
```

这里的两个根目录有不同用途：`RK_SDK_DIR` 是构建主机上的 SDK 根目录；`TARGET_DIR` 是待打包的目标 rootfs 目录，不是板端正在运行的 `/`。安装后的文件最终在镜像里表现为 `/etc/usbdevice.d/50-uac2.sh`。

处理规则如下：

- 外层已有 `RK_USB_GADGET` 关闭时提前退出的逻辑；仅开启内核 UAC2 不会触发这一安装步骤，还要开启 SDK 的 `RK_USB_UAC2`。
- SDK 启用 UAC2 时先安装工程默认脚本，权限为 `0644`；运行时由 `usbdevice` source，不需要可执行位。
- 默认脚本是固定工程文件，启用 UAC2 却缺少文件时，`install` 会报错；脚本的 `#!/bin/bash -e` 使该失败中止安装，避免静默生成缺少必要配置的镜像。
- 随后继续执行原有 `RK_USB_HOOKS` 循环。如果显式配置了另一个同名 `50-uac2.sh`，后安装的文件覆盖默认版本；其他名称的 hook 会同时保留。仍需避免多个 hook 重复覆盖 `USB_FUNCS` 或同名函数。
- 原来的清理步骤会移除目标目录中已有的 usbdevice 配置，必须把默认安装放在清理之后；仅手动往 Buildroot target 复制文件可能在下一次打包时被清掉。

实际 rootfs 流程通过 `post-overlay.sh` 处理 SDK 的 `usb-gadget` overlay，调用其中的安装脚本，并提供 `message`、服务安装等构建 helper。练习目录的 `install.sh` 副本不会自动被扫描执行。不要把这份安装脚本当作板端配置脚本运行，也不要为了调用副本再把它登记为额外 overlay，避免 USB 安装和清理流程重复执行。

在 Linux SDK 根目录确认配置并构建：

```sh
./build.sh menuconfig
```

需要保留：

```text
RK_ROOTFS_OVERLAY=y
RK_USB_GADGET=y
RK_USB_UAC2=y
```

`RK_ROOTFS_OVERLAY` 是 SDK 通用 rootfs overlay 的入口；若关闭，实际安装脚本不会经过这一标准流程。需要保留 ADB 时，也保留 SDK 原有 ADB 配置；当前项目 hook 的 `USB_FUNCS="uac2 adb"` 会请求同时启动两种功能。

```sh
./build.sh rootfs
```

构建日志应出现 `Installing default UAC2 hook:`。随后检查实际构建输出 rootfs target 中的 `etc/usbdevice.d/50-uac2.sh`，确认文件内容与工程源文件一致，再部署重新生成的 rootfs 镜像。

板端重启到新镜像后检查：

```sh
cat /etc/usbdevice.d/50-uac2.sh
cat /var/run/.usbdevice
cat /proc/asound/cards
```

以上是 rootfs 打包和部署流程，不要求为这次脚本安装修改重新编译内核。本次已修改安装脚本和文档；两份安装脚本和 UAC2 hook 的 Shell 语法检查通过，两份安装脚本内容一致。在临时目录中验证了启用时复制、关闭时不安装默认 hook、显式同名 hook 覆盖、其他 hook 保留、重复安装，以及缺少源文件时报错退出。未在 Linux SDK 中实际构建或部署 rootfs。

## 8. 如何查看声卡号、CARD id 和 PCM 设备号

这三个概念应分别确认：

| 信息 | 查看入口 | 本次已确认的板端信息 |
| --- | --- | --- |
| 声卡号 | `/proc/asound/cards`，每张卡行首数字 | PCM5102 为 0，UAC2 为 1 |
| CARD id | `/proc/asound/cards` 方括号中的字符串，去掉填充空格 | `rk3506pcm5102`、`UAC2Gadget` |
| PCM 设备号 | `aplay -l`、`arecord -l`、`/proc/asound/pcm` | 尚需最终板端输出确认；当前驱动创建的 PCM 设备编号为 0 |

板端执行：

```sh
cat /proc/asound/cards
aplay -l
arecord -l
cat /proc/asound/pcm
```

`aplay -l` 查看播放 PCM，`arecord -l` 查看采集 PCM。使用小写 `-l`；大写 `-L` 列出的是 ALSA 逻辑设备。

例如出现：

```text
card 1: UAC2Gadget [UAC2_Gadget], device 0: UAC2 PCM [UAC2 PCM]
```

表示声卡号 1、设备号 0，可以使用 `hw:1,0`，或更便于应对声卡编号变化的 `hw:CARD=UAC2Gadget,DEV=0`。CARD id 仍需保证唯一，并以当前板端实际值为准。

`/proc/asound/pcm` 的 `00-00` 表示声卡 0、设备 0；`01-00` 表示声卡 1、设备 0。PCM5102 在 `/proc/asound/cards` 中没有打印设备号，不代表它没有 PCM 设备。

`UAC2_Gadget 0` 末尾的 `0` 属于驱动生成的声卡长名称，不能把它当成 PCM 设备号。源代码中 [f_uac2.c](../../drivers/usb/gadget/function/f_uac2.c) 调用 `g_audio_setup()`，后者在 [u_audio.c](../../drivers/usb/gadget/function/u_audio.c) 中通过 `snd_pcm_new(..., 0, ...)` 创建设备 0。

## 9. UAC2 如何取数据，如何桥接 PCM5102

UAC2 虚拟声卡读出的已经是 PCM，无需再经过 MP3/AAC 等文件解码。按第 5 节参数，PC → 板端数据为 48 kHz、双声道、`S16_LE`；使用交错访问时排列为 `L0 R0 L1 R1 ...`。

```text
Windows 播放
    → USB OUT
    → UAC2 capture 的 ALSA 环形缓冲
    → snd_pcm_readi()
    → 用户态缓冲 / 可选 DSP 与重采样
    → snd_pcm_writei()
    → PCM5102 声卡的 ALSA 播放环形缓冲
    → DMA → SAI0 FIFO → I2S → PCM5102 → 模拟输出
```

应用打开 UAC2 时选择 `SND_PCM_STREAM_CAPTURE`，打开 PCM5102 时选择 `SND_PCM_STREAM_PLAYBACK`。PCM5102 驱动声明双声道及 `S16_LE` 等格式，实际整条 SAI 链路仍需协商和板端验证，见 [pcm5102a.c](../../sound/soc/codecs/pcm5102a.c)。

没有唯一的“ALSA 标准格式”。若两端都是 48 kHz、双声道、`S16_LE`、`RW_INTERLEAVED`，短时验证可直接搬运，不必为了桥接引入 FFmpeg。只有格式、采样率、声道布局不同或 DSP 有要求时，才需要格式转换或 `libswresample`。

`snd_pcm_readi()` 和 `snd_pcm_writei()` 的计数单位是音频帧，不是字节。双声道 16 bit 每帧为 4 字节，因此 480 帧对应 1920 字节、10 ms 音频。

### 9.1 先验证 UAC capture 能否收到 PC 音频

先在 Windows 选择该 USB 扬声器并播放测试音；板端使用实际 CARD id 和设备号：

```sh
arecord -D hw:CARD=UAC2Gadget,DEV=0 \
    -t wav -f S16_LE -r 48000 -c 2 -d 5 /tmp/usb-from-pc.wav
```

这是 WAV 文件，包含文件头；若需要无文件头的原始 PCM，将 `-t wav` 改为 `-t raw` 并使用合适文件名。

### 9.2 再做短时桥接验证

以下假设板端确认两张卡的设备号均为 0、CARD id 与本次输出一致，并且物理声卡接受这些参数：

```sh
arecord -D hw:CARD=UAC2Gadget,DEV=0 \
    -t raw -f S16_LE -r 48000 -c 2 |
aplay -D hw:CARD=rk3506pcm5102,DEV=0 \
    -t raw -f S16_LE -r 48000 -c 2
```

如果物理端需要 ALSA 格式转换，可在验证阶段尝试 `plughw:CARD=rk3506pcm5102,DEV=0`；它不保证所有参数都可用，也不自动完成两个硬件时钟之间的长期同步。

### 9.3 正式桥接程序需要补齐的处理

- 两端独立配置格式、period、buffer，并按协商后的参数处理数据。
- 处理短读短写、`EAGAIN`、xrun、挂起、USB 拔插与重新打开；不能每次假定完整读写成功。
- 根据需要使用读线程、环形缓冲和写线程，记录缓冲水位与异常次数。
- 处理主机开始、停止音频流和重枚举，清理旧缓冲，避免继续播放陈旧音频。
- 处理 USB 与 SAI 的时钟漂移：根据水位动态补偿重采样比例，或实现与物理播放消费速率相关联的 USB 反馈闭环。加大缓冲只能延后满溢或欠载；固定的 `48000 → 48000` 格式转换不会自动解决漂移。

现有 [main1.c](../alsa_max98357out/main1.c) 可复用 ALSA 播放及短写恢复部分，把文件解码输入替换成 UAC capture 输入；不应把文件结束时的解码/重采样冲刷逻辑原样当作实时 USB 桥接流程。

## 10. 如需修改名称

| 要修改的内容 | 当前入口 | 生效范围 |
| --- | --- | --- |
| 板端 CARD id | `/sys/class/sound/cardN/id` | 用于 `hw:CARD=...` 的标识 |
| 板端声卡与 PCM 显示名称 | `f_uac2.c` 的 `g_audio_setup(agdev, "UAC2 PCM", "UAC2_Gadget")` | 修改驱动后构建、部署 |
| USB 音频功能字符串 | UAC2 ConfigFS 的 `function_name` | USB 音频功能描述符 |
| USB 产品字符串 | Gadget 的 `strings/0x409/product`，SDK 的 `RK_USB_PRODUCT` | 整个 USB 产品的描述字符串 |

临时改 CARD id 的示例，必须先确认 UAC2 确实是声卡 1：

```sh
printf '%s' 'DPUAC2' > /sys/class/sound/card1/id
```

当前 [sound/core/init.c](../../sound/core/init.c) 会检查写入 id 的每个字符，因此使用不附加换行的 `printf`。该修改不改变声卡显示名称，声卡重建后也不保留。

如果需要 USB 音频功能字符串，可在第 5 节 hook 的保护判断之后加入：

```sh
echo "DP USB Audio" > function_name || return 1
```

随后执行 `usbdevice restart`。这不改变板端 ALSA 声卡名；Windows 最终显示还可能受产品字符串和已有设备记录影响。

后续对话中，仅写入 `function_name="Infinity DP_Industry Codec"` 后，Windows 显示名称未随之改变。需要区分以下字段：当前 `f_uac2.c` 将 `function_name` 放入 IAD 的 `iFunction`；它不是整个设备的 USB 产品字符串 `iProduct`。微软说明，设备管理器中的 UAC2 名称可以由 USB 产品字符串覆盖，见 [USB Audio 2.0 drivers](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/usb-2-0-audio-drivers)。

需要同时配置时，在 hook 文件顶部设置：

```sh
USB_PRODUCT="Infinity DP_Industry Codec"
```

在 `uac2_post_prepare_hook()` 的链接保护判断之后、绑定之前加入：

```sh
printf '%s' "$USB_PRODUCT" > \
    /sys/kernel/config/usb_gadget/rockchip/strings/0x409/product || return 1
echo "$USB_PRODUCT" > function_name || return 1
```

显式写 `product` 的原因是：当前 `usbdevice` 仅在首次创建 Gadget 时执行 `usb_init()` 初始化产品字符串；普通 `restart` 保留 Gadget 根目录，不会重新执行该初始化。因此只修改 `USB_PRODUCT` 变量，不一定更新已存在的 `product` 属性。

重启 USB 服务、重新枚举后，分别读取以下文件确认板端配置已生效：

```sh
cat /sys/kernel/config/usb_gadget/rockchip/strings/0x409/product
cat /sys/kernel/config/usb_gadget/rockchip/functions/uac2.0/function_name
```

Windows 的播放/录音端点还有持久保存的友好名称，可能不同于设备管理器名称。若描述符已更新而声音列表仍是旧名，可通过 `mmsys.cpl` 中对应端点的属性查看、修改；这是当前电脑的端点设置，见 [Friendly Names for Audio Endpoint Devices](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/friendly-names-for-audio-endpoint-devices)。

## 11. 验证顺序与本次整理的边界

建议按以下顺序确认，避免把枚举、传输和实际出声混为一谈：

1. `/sys/class/udc/` 有控制器，Device 角色正常。
2. `/var/run/.usbdevice` 包含 `uac2`，ConfigFS 有 `uac2.0` 和对应配置链接，Gadget 的 `UDC` 非空。
3. `/proc/asound/cards` 有 UAC2；用 `aplay -l`、`arecord -l` 或 `/proc/asound/pcm` 确认设备号。
4. PC 枚举成功，设备不会持续断开、重连。
5. 板端 UAC capture 能录到正确 PCM；物理 PCM5102 声卡单独播放正常。
6. 短时桥接能出声，再验证拔插、主机停流、缓冲水位和长时间稳定性。

首次整理只涉及文档。后续对话已经修改用户态桥接程序、增加 Makefile，并完成若干版本的 ARM 目标语法检查；用户也提供了板端运行日志。不能再把整个过程概括为“只有文档、没有程序修改”。截至本次更新，用户最后反馈的版本仍停留在等待 PCM，最新的采集启动阈值修正尚无板端验证结果，详见第 18 节。完整实施方案也可参阅 [原 readme.md](readme.md)。

## 12. 从初始代码到当前程序的修改记录

以下记录依据本次对话中读取的已保存代码和用户提供的日志整理，不是 Git 提交历史。历史版本行号会变化，因此以函数名和代码片段定位；当前代码仍以同目录 C 文件为准。

### 12.1 各阶段做了什么

| 阶段 | 对话中看到的状态或问题 | 修改内容 | 修改目的与验证情况 |
| --- | --- | --- | --- |
| 初始文件解码版本 | 从原 FFmpeg 文件播放示例复制而来，只有 `UAC2_DEVICE` 宏，没有 UAC capture 读取；仍走文件打开、解码和重采样 | 确认 UAC2 输入本身已经是 PCM，提出直接 ALSA 搬运路径 | 这是路径分析，初始程序还没有实现 UAC2 桥接 |
| 用户加入桥接 main 的草稿 | UAC2 一度用 playback 打开；有 `uint8`、`vac2_init`、未初始化帧数、未声明 `frames_read`、未处理短写等问题；某个中间版本还同时存在两个 `main()` 和不完整的旧解码循环 | 检查并指出编译错误、方向错误、帧数与字节数混用风险 | 这些问题是在不同草稿中先后出现，不表示当前文件仍有这些错误 |
| 第一版完整直通程序 | 需要一份只做 UAC2 → PCM5102 的完整程序 | 移除未使用的 FFmpeg 头文件和 `play_audio()` 转换函数；保留一个 main；补齐初始化、按实际帧数完整写出、恢复、信号退出和清理 | 仅依赖 ALSA；完成真实 ALSA 头文件的 ARM 目标语法检查 |
| 增加构建入口 | `usb_test` 目录原来没有此程序的 Makefile | 新增默认构建目标、Buildroot sysroot 和 wrapper 探测、环境检查及清理目标 | 检查了构建规则和 Shell 命令语法；本机缺少可用 GNU make 和 Buildroot 输出 sysroot，未完成链接 |
| 无输入时 EIO 的修正 | 用户运行后看到 `Input/output error; recovery failed`，程序退出 | capture 改成 `SND_PCM_NONBLOCK`，两端增加 `SND_PCM_EINTR`；无数据时有限等待；输入空闲时停止、重新准备播放端；增加数据进出日志 | 修正不应因正常等待数据而陷入长时间阻塞的处理方式；没有把所有 EIO 一律忽略 |
| 一直 Waiting 的修正 | 非阻塞版本不退出，但 Windows 播放时仍没有读到 PCM；监视器只有 PPM 事件 | 新增 `capture_sw_params()`，将 capture 的 `start_threshold` 改为 1 帧、`avail_min` 改为 1024 帧 | 修复默认启动阈值可能大于单次读取量的代码缺口；最新改动通过 ARM 目标语法检查，仍待板端确认 |

### 12.2 初始草稿的具体问题与替换方式

**打开方向。** 旧草稿的 `uac2_init()` 使用 `SND_PCM_STREAM_PLAYBACK`，这对应板端向 PC 送音频。PC 播放 → 板端读取必须使用 `SND_PCM_STREAM_CAPTURE`。PCM5102 则使用 `SND_PCM_STREAM_PLAYBACK`。这是两张声卡各自的方向，不能因为“PC 正在播放”就把 UAC2 也按 playback 打开。

**函数名和类型。** 将误写的 `vac2_init()` 改为实际定义的 `uac2_init()`；缓冲元素使用 `<stdint.h>` 中的 `uint8_t`。读取返回值使用有符号的 `snd_pcm_sframes_t`，因为它既可能是正帧数，也可能是负错误码。

**未初始化长度。** 草稿曾先声明 `frames_to_write`，没有赋值就把它传给 `snd_pcm_readi()`。这会把不确定的数值当作请求帧数，存在越过用户缓冲区的风险。现在始终用 `PERIOD_FRAMES` 限制单次读取，只有返回正数后才转换为无符号的待写帧数。

**实际读取量。** `snd_pcm_readi()` 返回 600 帧时，只能写这 600 帧，不能继续使用请求值 1024。否则可能播放缓冲中未更新的旧数据。负错误码也必须先处理，不能转成无符号长度传给播放接口。

**短写。** 一次 `snd_pcm_writei()` 不保证写完全部数据。现在根据实际写出的帧数减少剩余量，并推进数据指针；成功恢复播放端后继续处理本批剩余数据，不直接读取下一批。

**旧代码入口。** 中间草稿保留的第二个 `main()` 会造成重定义，缺少参数的 `snd_pcm_readi()` 等旧实验语句也会阻止编译。完整直通版本已去掉旧文件解码入口，当前文件只有一个 `main(void)`。

**资源清理。** 两个句柄从 `NULL` 开始；初始化中途失败时，关闭已打开句柄并设回 `NULL`；main 的 `out` 路径仅关闭非空句柄。这样既避免泄漏，也避免失败后重复关闭同一个句柄。

## 13. 当前函数与关键参数解析

以下行号是 2026-10-21 整理时 [uac2_in_pcm5102_out.c](uac2_in_pcm5102_out.c) 的定位参考，后续编辑可能移动。用户后续补充的两端初始化失败日志也保留在当前 main 中。

| 函数或位置 | 当前起始行 | 职责 |
| --- | --- | --- |
| `request_stop()` | 25 | 信号处理函数，只设置退出标志 |
| `capture_sw_params()` | 31 | 为 capture 单独设置自动启动阈值与等待阈值 |
| `pcm_init()` | 53 | 通用打开、格式配置，以及失败关闭处理 |
| `uac2_init()` | 104 | 以 capture 方向初始化 UAC2 |
| `alsa_init()` | 110 | 以 playback 方向初始化 PCM5102 |
| `recover_pcm()` | 115 | 包装 ALSA 错误恢复并记录结果 |
| `wait_pcm()` | 133 | 有限时间等待 PCM 可读或可写 |
| `main()` | 148 | 注册信号、初始化、读取与完整写出、停流处理及清理 |

### 13.1 参数的单位不能混淆

| 参数 | 当前值 | 单位及实际意义 |
| --- | --- | --- |
| `UAC2_DEVICE` | `hw:1,0` | 声卡 1、PCM 设备 0，必须与实际板端一致 |
| `ALSA_DEVICE` | `hw:0,0` | 声卡 0、PCM 设备 0，当前用于 PCM5102 播放 |
| `OUTPUT_RATE` | `48000` | 每声道每秒 48000 个样本，即每秒 48000 帧 |
| `OUTPUT_CHANNELS` | `2` | 每个音频帧包含左右两个声道样本 |
| `BYTES_PER_FRAME` | `2 * sizeof(int16_t)` | 每帧 4 字节 |
| `PERIOD_FRAMES` | `1024` | 应用单次读取的最大帧数，约 21.33 ms；不是已经固定的硬件 period |
| `OUTPUT_LATENCY` | `100000` | 各端 `snd_pcm_set_params()` 的缓冲延迟请求，100 ms；不是实测端到端延迟 |
| `PCM_WAIT_MS` | `100` | `snd_pcm_wait()` 的单次等待超时，100 ms |

用户态 `buffer` 大小为 `1024 * 2 * 2 = 4096` 字节。它与 UAC2 内核 ALSA 环形缓冲、PCM5102 内核播放缓冲和 SAI 硬件 FIFO 是不同的存储位置。

例如请求 1024 帧但实际读到 600 帧，对应有效数据只有 2400 字节。若第一次成功写出 400 帧，数据指针推进 1600 字节，剩余 200 帧；缓冲中未被本次读取更新的部分不能送去播放。

### 13.2 pcm_init()：打开和设置格式

`snd_pcm_open()` 创建用户态 PCM 句柄并打开对应 ALSA 设备，不会自动把两张声卡连起来。

当前模式设置为：

```c
int mode = SND_PCM_EINTR;
if (stream == SND_PCM_STREAM_CAPTURE)
    mode |= SND_PCM_NONBLOCK;
```

- capture 使用非阻塞模式，无可读数据时返回 `-EAGAIN`，由程序决定等待和退出。
- playback 保持阻塞写入，由 ALSA 控制输出节奏。
- `SND_PCM_EINTR` 允许 ALSA 等待被信号打断；它与 main 中不设置 `SA_RESTART` 的信号注册配合，避免库内部自动重试使退出标志迟迟无法被检查。

随后配置两端共同格式：

```c
snd_pcm_set_params(*pcm_handle, SND_PCM_FORMAT_S16_LE,
                   SND_PCM_ACCESS_RW_INTERLEAVED,
                   OUTPUT_CHANNELS, OUTPUT_RATE, 0,
                   OUTPUT_LATENCY);
```

`S16_LE` 表示有符号 16 bit 小端；`RW_INTERLEAVED` 表示通过 readi/writei 按 `L0 R0 L1 R1 ...` 访问；`soft_resample=0` 表示该示例不请求软件重采样。它不负责跨 USB 与 SAI 时钟的漂移补偿，即使设为 1 也不能理解为已经实现动态同步。

设置参数通常使 PCM 进入 `PREPARED`。采集如何进入 `RUNNING` 还取决于启动阈值，第 14 节专门说明。

### 13.3 main()：按实际帧数搬运

当前循环先读取 UAC2，再完整写出这一批，不需要将 PCM 包装成 FFmpeg 的 `AVPacket` 或 `AVFrame`。`snd_pcm_readi()` 的返回值是帧数，不是帧对象，也不是返回数据指针。

```text
readi 请求最多 1024 帧
    → 正数：获得本批有效帧数
    → 零或 EAGAIN：等待，不发送旧缓冲
    → 其他负数：尝试恢复；恢复失败退出

本批有效数据
    → writei
    → 正数：推进指针、减少剩余帧数
    → 零或 EAGAIN：等待后重试剩余数据
    → 可恢复错误：恢复后重试剩余数据
    → 恢复失败：退出清理
```

关键更新语句为：

```c
frames_to_write = (snd_pcm_uframes_t)frames_read;
/* 每次成功写入后执行： */
frames_to_write -= (snd_pcm_uframes_t)frames_written;
data_ptr += (size_t)frames_written * BYTES_PER_FRAME;
```

转换为无符号帧数之前，必须已经确定 `frames_read > 0`。当前 `uint8_t *data_ptr` 的指针步长是字节，因此乘以每帧 4 字节。若以后改成 `int16_t *`，指针步长将变成样本大小，不能照抄同一套字节偏移。

### 13.4 recover_pcm()：哪些错误能恢复

本次核对的 ALSA 1.2.11 `snd_pcm_recover()` 主要处理以下情况。源码来自 SDK 已有 [alsa-lib 源码包](../../../buildroot/dl/alsa-lib/alsa-lib-1.2.11.tar.bz2) 中的 `src/pcm/pcm.c`；板端实际库版本仍需以部署版本为准。

| 返回值 | 常见含义 | 当前处理与边界 |
| --- | --- | --- |
| `-EAGAIN` | 非阻塞时暂时没有数据或空间 | 直接等待，不当作致命错误 |
| `-EINTR` | 系统调用或等待被信号打断 | 已请求退出则结束；否则可继续 |
| `-EPIPE` | capture overrun 或 playback underrun | `snd_pcm_recover()` 通常通过 `snd_pcm_prepare()` 恢复 |
| `-ESTRPIPE` | PCM 被挂起 | ALSA 尝试 resume，必要时 prepare；恢复过程可能等待 |
| `-EIO` | I/O 异常，可能包含没有进度导致的超时等情况 | 该版本 recover 不专门处理，直接返回错误；不能一律解释成正常无输入 |
| `-ENODEV` 等 | 设备被移除或其他不可恢复错误 | 恢复失败后退出；当前没有自动发现并重新打开声卡 |

“恢复后继续本批剩余数据”仅保证应用没有因为短写就丢弃剩余指针范围，不等于 xrun 前后的音频完全无损。prepare 可能重置内核缓冲，实际播放仍可能有中断。

### 13.5 wait_pcm()：等待而不是空转

无数据时仅在 while 中反复 `continue` 会导致忙循环。当前调用 `snd_pcm_wait(pcm, 100)`，让线程等待可读/可写事件，或在约 100 ms 后返回。

正常返回 1 表示就绪、0 表示超时；发生错误时 helper 调用 `recover_pcm()`，恢复成功也可能返回 0，因此不能把所有返回 0 都当成主机主动关闭音频流的证明。

### 13.6 停流与信号退出

程序已经收到过 PCM 后，如果读取再次得到零或 EAGAIN，并且等待没有取得可读进展，会调用播放端 `snd_pcm_drop()` 和 `snd_pcm_prepare()`：前者停止播放并丢弃未播放数据，后者使播放端重新准备。后续获得新输入时，继续写入即可再次按播放启动阈值启动。

`input_active` 和 `output_active` 是应用观察到数据读写后的标志，不等于驱动或 Windows 的流状态。日志 `UAC2 input idle` 是等待超时后的处理结果，不是 USB `SET_INTERFACE alt=0` 的直接通知。

`request_stop()` 只修改 `volatile sig_atomic_t` 标志，不在信号处理函数里打印、分配内存或关闭 ALSA。main 检查标志后走统一 `out` 清理。退出采用 drop 而非 drain，是为了停止并丢弃剩余数据，不等待播放完全排空；不能据此保证挂起恢复等所有阻塞路径都严格在 100 ms 内退出。

## 14. 一直 Waiting 的启动阈值问题及修正原理

### 14.1 为什么非阻塞之后仍然可能没有数据

对话中的反馈先是 EIO 后退出，修改成非阻塞后变成持续打印启动提示、但没有 `UAC2 received PCM`。非阻塞解决的是“如何等数据”，不会自动修正“采集有没有被启动”。

ALSA 1.2.11 的 `snd_pcm_set_params()` 对启动阈值默认设置为：

```c
(buffer_size / period_size) * period_size
```

这个数接近整个 ALSA 缓冲区，可能大于应用每次请求的 1024 帧。实际值要读取板端 `sw_params`，不能把 4096 或 4800 当作所有设备必然使用的阈值。

当前内核 [sound/core/pcm_lib.c](../../sound/core/pcm_lib.c) 对 capture 的自动启动条件为：

```c
if (!is_playback &&
    runtime->state == SNDRV_PCM_STATE_PREPARED &&
    size >= runtime->start_threshold) {
    err = snd_pcm_start(substream);
    if (err < 0)
        goto _end_unlock;
}
```

如果实际阈值大于 1024，程序又没有显式启动采集，1024 帧的 readi 请求不会满足自动启动条件。此时可能停留在 `PREPARED`；非阻塞读取返回 EAGAIN，程序等待、重试，仍然没有真正开始采集。

| 应用配置 | 本地采集可能出现的行为 |
| --- | --- |
| 阻塞读 + 过高的 capture 启动阈值 | 采集可能未启动，等待无进度，最终可能超时报错 |
| 非阻塞读 + 同样过高的阈值 | 返回 EAGAIN，不退出但可能一直等待 |
| 非阻塞读 + capture 阈值为 1 | 首次正长度读取即可启动本地采集；主机无数据时继续等待，有数据后读取 |

这是当前源代码中已经核实的启动条件风险；对话尚未提供修正前的板端 `PREPARED` 状态快照，不能据此宣称它已经是整板无声的唯一实测根因。

### 14.2 capture_sw_params() 的具体修改

当前完整函数如下：

```c
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
```

| 函数调用 | 作用 |
| --- | --- |
| `snd_pcm_sw_params_malloc()` | 分配软件参数容器，失败直接返回 |
| `snd_pcm_sw_params_current()` | 读取当前参数，只修改需要覆盖的字段 |
| `snd_pcm_sw_params_set_start_threshold(..., 1)` | 将 capture 自动启动阈值改为 1 帧，使 1024 帧读取请求满足条件 |
| `snd_pcm_sw_params_set_avail_min(..., 1024)` | 调整等待就绪的最小可用帧数，减少频繁唤醒；它不是自动启动阈值 |
| `snd_pcm_sw_params()` | 将容器中的参数实际提交给 PCM，单纯修改容器不会生效 |
| `snd_pcm_sw_params_free()` | 释放容器；提交后的参数由 PCM 保存 |

该函数在 `pcm_init()` 的 `snd_pcm_set_params()` 成功之后调用，并且仅对 capture 调用。如果顺序反过来，后面的 set_params 会重新覆盖软件参数。playback 保留适当预填充后再启动的默认策略，没有把两端启动阈值都改成 1。

设置阈值不会立即调用 START；首次 readi 由内核触发 START。以后 xrun 恢复为 `PREPARED` 后，保留的软件参数也允许下一次 readi 再次启动。另一种实现是显式 `snd_pcm_start()`，但需要在初始化和各次恢复后正确管理状态，本例没有采用该方案。

### 14.3 USB 开流与 ALSA 开始采集是两个条件

```text
Windows 端：
打开正确的 USB 播放端点
    → SET_INTERFACE，USB OUT 流接口 alt=1
    → f_uac2.c::afunc_set_alt()
    → u_audio_start_capture()，启用接收端点并提交 USB request

板端应用：
打开 UAC2 capture 并设置参数
    → PREPARED
    → readi 请求满足 start_threshold
    → 内核 snd_pcm_start()
    → uac_pcm_trigger(START) 设置 prm->ss，进入 RUNNING

两条路径均正常：
USB 接收完成
    → u_audio_iso_complete()
    → 复制数据到 ALSA capture 环形缓冲，推进 hw_ptr
    → 应用 readi 取得 PCM
```

关键实现见 [f_uac2.c](../../drivers/usb/gadget/function/f_uac2.c) 和 [u_audio.c](../../drivers/usb/gadget/function/u_audio.c)。接收完成回调发现 `prm->ss` 为空、或者 ALSA 尚未 RUNNING 时，不会把本次 USB 数据作为有效采集数据交给应用。

因此，USB 设备已经 CONFIGURED、Windows 打开 USB OUT 流、板端 capture RUNNING、应用实际读到 PCM，是四个不同的检查点。采集启动阈值只解决本地 ALSA 启动条件，不会强迫 Windows 选择这张设备或开启音频流。

## 15. Makefile 的新增与构建原理

同目录 [Makefile](Makefile) 默认将 `uac2_in_pcm5102_out.c` 编译为 `uac2_in_pcm5102_out`，仅使用 `-lasound`，没有 `-lavcodec`、`-lavformat` 或 `-lswresample`。

在 Linux SDK 环境进入 `kernel-6.1/driver_pratice/usb_test` 后执行：

```sh
make check-env
make
```

需要指定环境时，例如：

```sh
make SYSROOT=/实际/Buildroot/sysroot APP_CC=/实际/交叉gcc
```

| 变量或规则 | 作用 |
| --- | --- |
| `SDK_DIR` | 从当前构建目录向上推导 SDK 根目录，也可命令行覆盖 |
| `SYSROOT` | 探测 Buildroot 的 `output/*/host/*/sysroot`；多份输出时应显式选择与板端一致的配置 |
| `BR_WRAPPER`、`APP_CC` | 优先选择同一份 Buildroot 输出的编译器 wrapper，否则回退到 SDK 交叉 gcc |
| `--sysroot` | 使用目标 ARM 系统的头文件、库和链接环境，避免误用主机 `/lib` 等目录 |
| `ALSA_HDR`、`check-env` | 检查编译器、sysroot、ALSA 头文件和开发用链接库 |
| `-lasound` | 链接 ALSA 用户态库；板端只有 `libasound.so.2` 不等于 sysroot 已有供链接器查找的 `libasound.so` |
| `$(APP): $(SOURCE) Makefile` | 源码或 Makefile 修改后重新构建 |
| `| check-env` | 顺序依赖，每次检查环境，但不只因该检查是 phony 就无条件重编译目标 |
| `make clean` | 删除本程序的可执行文件，不清理内核或其他示例 |

该程序是用户态程序，不使用 `make -C kernel M=... modules` 这类内核模块构建入口。构建成功后仍需把新程序部署到板端；板端启动日志本身不能区分它是否是刚刚修改的最新版本。

## 16. 无声与无输入时如何分层定位

### 16.1 优先检查正在运行程序的 PCM 状态

保持桥接程序运行，并让 Windows 向这张 USB 播放设备持续送音频；在另一个板端终端执行。以下仍假设 UAC2 为声卡 1、PCM 设备 0：

```sh
cat /proc/asound/card1/pcm0c/sub0/sw_params
cat /proc/asound/card1/pcm0c/sub0/hw_params
cat /proc/asound/card1/pcm0c/sub0/status
```

隔一段时间再次执行 status 命令，比较指针。采集端路径中是 `pcm0c`；PCM5102 播放端则对应 `/proc/asound/card0/pcm0p/sub0/status`。源码打印入口见 [sound/core/pcm.c](../../sound/core/pcm.c)。

| 观察结果 | 能得到的结论与下一步 |
| --- | --- |
| `start_threshold` 仍大于 1024 | 检查是否已部署最新程序、软件参数设置是否成功 |
| capture 为 `PREPARED` | ALSA 尚未开始采集；不能只去排查功放 |
| capture 为 `RUNNING`，`hw_ptr` 长时间不变 | 采集缓冲没有新数据进展，继续检查主机 USB OUT 开流、端点及传输 |
| capture 的 `hw_ptr` 持续变化 | 驱动正在推进采集缓冲；样本可能是实际音频，也可能是静音 |
| `UAC2 received PCM` | 应用本次实际获得了正帧数 |
| `playback accepted` | ALSA 写入接口接受了部分 PCM，不保证已经启动 DMA、输出 I2S 或模拟出声 |
| PCM5102 playback 为 RUNNING 且指针推进 | 物理声卡的软件播放链路有进度；仍需核对实际 I2S、DAC、功放和音量 |

`playback accepted` 可能发生在播放缓冲尚未达到启动阈值时，所以不能把第一批写入成功当成物理输出的证明。

### 16.2 amixer 命令在哪里执行，缺失时如何处理

Windows CMD 中的 `amixer` 找不到，是因为该命令属于 Linux ALSA 工具。通过串口、SSH 或 ADB 进入板端后执行，前提是板端镜像确实安装了该工具。

```sh
amixer -c 1 cget 'iface=PCM,name=Capture Rate'
```

当前驱动的 `Capture Rate` 控件在 USB OUT 流未激活时返回 0，激活时返回配置采样率，例如 48000；其判断来自驱动的 `active` 状态，不是 PCM 样本幅度，也不代表应用已经读取成功。

用户后续在板端也得到 `amixer: command not found`，说明当时没有可用的命令。要打包该工具，在 Linux SDK 中使用 `./build.sh buildroot-config`，开启：

```text
BR2_PACKAGE_ALSA_UTILS=y
BR2_PACKAGE_ALSA_UTILS_AMIXER=y
```

然后构建并部署实际 rootfs。定义见 [alsa-utils/Config.in](../../../buildroot/package/alsa-utils/Config.in)，安装到 `usr/bin/amixer` 的规则见 [alsa-utils.mk](../../../buildroot/package/alsa-utils/alsa-utils.mk)。短时调试可复制与板端库匹配的构建产物到 `/oem` 后用绝对路径执行，无需写只读的 `/usr/bin`。

### 16.3 udev 事件能证明什么

板端已有 `udevadm` 时，可以先启动监视，再在 Windows 打开该设备的播放测试：

```sh
udevadm monitor --kernel --property --subsystem-match=u_audio
```

主机开启 USB OUT 音频流时，当前驱动会发送以下形式的事件：

```text
USB_STATE=SET_INTERFACE
STREAM_DIRECTION=OUT
STREAM_STATE=ON
```

监视器不回放启动前的事件。因此，若流早已开启，之后只看到其他通知，不等于从未开流。暂停音乐也不保证关闭 USB 端点，Windows 音频引擎可能继续发送静音样本。

用户看到的另一种日志是：

```text
USB_STATE=SET_AUDIO_CLK
PPM=-22
```

这是 `ppm_calculate_work()` 比较 USB SOF 帧计数和板端 `ktime_get_raw()` 后得到的时钟偏差估算，-22 约为百万分之负二十二，即 -0.0022%。该任务约每秒运行，使用 20 组结果求平均，在满足条件且结果变化时才发事件，所以日志间隔不固定。

它只要求 Gadget 处于 CONFIGURED，不要求应用收到音频。它没有直接测量 PCM5102 的 SAI 时钟，也不会因为发出 `SET_AUDIO_CLK` 就自动调节 SAI。不能把 PPM 通知当成主机正在播放、数据已进入 ALSA 或物理时钟闭环完成的证明。

### 16.4 两条链路单独验证

首先用第 9.1 节的 UAC capture 录音确认 USB 输入；再停止桥接程序，使用已验证的本地 WAV 单独测试 PCM5102，或者在已安装 speaker-test 时执行：

```sh
speaker-test -D hw:0,0 -c 2 -r 48000 -F S16_LE -t sine
```

测试前停止占用物理声卡的桥接程序，避免 `Device or resource busy`。本地播放正常但 UAC 无数据，应优先查 USB/采集；UAC 能读到有效音频但物理声卡也无法本地播放，应优先查 SAI、DAC 与功放链路。

## 17. 当前版本已经实现与仍未实现的内容

| 项目 | 当前状态 |
| --- | --- |
| 同格式 PCM 直接搬运 | 已实现，48 kHz、双声道、S16_LE、交错访问 |
| 有符号读写返回值、实际读取量、短写循环 | 已实现 |
| 无数据时有限等待 | 已实现，capture 非阻塞，等待超时 100 ms |
| capture 专用启动阈值 | 已实现，start_threshold=1，avail_min=1024；仍需实际运行确认 |
| xrun 等恢复 | 已调用 ALSA recover；恢复失败有诊断并退出 |
| 停流时播放端 drop/prepare | 已实现，是应用等待超时后的处理，不是精确 USB OFF 通知 |
| 信号和失败退出清理 | 已实现，句柄置空、统一 drop/close |
| PCM 输入、输出进度日志 | 已实现，按数据活动阶段首次打印，避免逐包打印 |
| 自动识别声卡、热插拔重连 | 未实现；当前使用固定 `hw:1,0` 和 `hw:0,0` |
| 动态时钟漂移补偿、长期缓冲闭环 | 未实现 |
| DSP、音量/静音到物理输出的映射 | 未实现 |
| 麦克风反向桥接 | 未实现；PCM5102 本身也没有采集能力 |

当前程序是单线程的通路验证示例。读写共用一个用户态缓冲，播放阻塞期间不会并行读取 capture；调度延迟、两端缓冲和时钟差异仍可能造成 xrun。今后需要长期稳定运行时，再引入独立读写线程、环形缓冲、动态重采样或真实反馈闭环；不能把本次启动阈值修正当作所有稳定性问题都已解决。

## 18. 验证记录与下一步验收

第 12～18 节首次补充时仅修改 Markdown，没有额外修改 C、Makefile、内核或板端配置。随后第 7.1 节的自动安装更新另修改了两份 `install.sh`，并完成该节记录的 Shell 语法和临时目录安装检查；没有执行 rootfs 构建或板端命令。桥接程序的验证范围如下：

- 对话中多个 C 版本曾使用 SDK 现有 ALSA 1.2.11 源码包中的真实头文件、ARM hard-float libc 头文件和本机 Clang，进行 `-std=c11 -Wall -Wextra -Werror -fsyntax-only` 检查；最新阈值修正版本通过该检查。
- 语法检查没有链接生成 ARM 可执行文件，没有验证目标板实际安装的 ALSA 库及驱动行为。Makefile 只完成规则和 Shell 命令检查，本机没有可用 GNU make 和已构建的 Buildroot 输出 sysroot。
- 曾尝试在本机编译模拟 ALSA 接口的运行测试，但本机 Clang 不支持所需 x86_64 代码生成目标，测试未运行，不能记为测试通过。
- 用户反馈证明旧阻塞版本在无输入场景出现 EIO 后退出；后续非阻塞版本保持等待；用户最后提供的运行状态仍未出现 `UAC2 received PCM`，udev 中只有 PPM 通知。
- 最新 `capture_sw_params()` 修改后，尚未收到板端 `start_threshold=1`、capture RUNNING、指针推进、成功读取 PCM、PCM5102 实际出声或长时间运行的结果。

下一步应重新构建、部署最新可执行文件，并按以下顺序保留输出：

1. `cat /proc/asound/cards`、`cat /proc/asound/pcm`，确认程序使用的两端设备正确。
2. 程序运行时读取 capture 的 `sw_params`、`hw_params`、两次 `status`，确认阈值、格式、RUNNING 与指针进度。
3. 从 Windows 明确选择这张 USB 播放设备进行测试，区分主机开流事件、板端实际收到 PCM 和静音样本。
4. 确认 PCM5102 本地播放正常，再验证桥接后的实际声音。
5. 最后验证停流再播放、Ctrl+C、USB 断开，以及长时间运行时的 xrun 和缓冲水位。

在这些结果得到确认前，文档中的当前程序应标记为“源码与语法已核对，板端通路验证进行中”。
