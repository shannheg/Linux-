#!/bin/sh

USB_FUNCS="uac2 adb"
UAC2_INSTANCES="uac2.0"
USB_PRODUCT="Infinity DP_Industry Codec"

uac2_post_prepare_hook()
{
    # usbdevice update 会重复调用本函数。
    # 实例已链接时跳过参数写入，避免 EBUSY 导致解绑重试。
    # 修改参数后，需执行 usbdevice restart 才会重新配置。
    if [ -L /sys/kernel/config/usb_gadget/rockchip/configs/b.1/f-uac2.0 ]; then
        return 0
    fi

    # Windows 播放 → 板端 UAC capture。
    # 48 kHz、16 bit、双声道。
    # USB 产品名称
    printf '%s' "$USB_PRODUCT" > \
        /sys/kernel/config/usb_gadget/rockchip/strings/0x409/product || return 1
    echo "Infinity DP_Industry Codec" > function_name || return 1
    echo 3 > c_chmask || return 1
    echo 48000 > c_srate || return 1
    echo 2 > c_ssize || return 1
    echo async > c_sync || return 1

    # 板端 UAC playback → Windows 录音。
    # 48 kHz、16 bit、单声道。
    # 实际麦克风数据仍需用户态程序提供。
    echo 1 > p_chmask || return 1
    echo 48000 > p_srate || return 1
    echo 2 > p_ssize || return 1

    # 暂不声明尚未接入实际音频处理的音量和静音控制。
    echo 0 > c_mute_present || return 1
    echo 0 > c_volume_present || return 1
    echo 0 > p_mute_present || return 1
    echo 0 > p_volume_present || return 1

    return 0
}
