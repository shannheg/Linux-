# SPDX-License-Identifier: GPL-2.0
#
# lcd_test —— 在 RGB 屏(800x480)上循环播放 /oem/123.mp4（用户态程序）
#
#   make            默认：交叉编译 main.c -> lcd_play
#   make app        同上
#   make clean
#
# 说明：
#   1) main.c 是**用户态**程序，不要加进 Kbuild（不是内核模块）。
#   2) 它用 libdrm(KMS) 直接上屏、用 FFmpeg 软解 MP4，所以 rootfs 里必须有
#      libdrm 和 libav* 库；在 buildroot 里打开下面几项后重编并重烧 rootfs：
#         BR2_PACKAGE_LIBDRM=y            （RK3506 默认被 RGA/MPP 等 select，一般已开）
#         BR2_PACKAGE_FFMPEG=y
#         BR2_PACKAGE_FFMPEG_SWSCALE=y    （libswscale 默认不开，本程序链接了 -lswscale）
#   3) 跨平台自动推导：本文件在 <sdk>/kernel/driver_pratice/lcd_test/，
#      往上三层就是 SDK 根；也可命令行覆盖：
#         make SDK_DIR=/path/to/sdk
#         make SYSROOT=/path/to/buildroot/output/<cfg>/host/<tuple>/sysroot

# SDK 根目录自动推导（往上是 kernel -> driver_pratice -> lcd_test）
SDK_DIR ?= $(abspath $(CURDIR)/../../..)
KERN_DIR ?= $(SDK_DIR)/kernel
ARCH ?= arm
CROSS_COMPILE ?= $(SDK_DIR)/prebuilts/gcc/linux-x86/arm/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-

# ---------------- 交叉编译环境 ----------------
SYSROOT ?= $(firstword $(wildcard $(SDK_DIR)/buildroot/output/*/host/*/sysroot))

# 优先用 buildroot 生成的 wrapper 编译器（自带 --sysroot，能找到 libc/ld）；
# 没有 wrapper 才退回裸交叉 gcc，并显式加 --sysroot
BR_HOST_BIN := $(abspath $(SYSROOT)/../../bin)
BR_WRAPPER  := $(firstword \
	$(wildcard $(BR_HOST_BIN)/arm-none-linux-gnueabihf-gcc) \
	$(wildcard $(BR_HOST_BIN)/arm-buildroot-linux-gnueabihf-gcc))
APP_CC ?= $(if $(BR_WRAPPER),$(BR_WRAPPER),$(CROSS_COMPILE)gcc)

# ---------------- 目标程序 ----------------
APP := lcd_play

# -w：忽略（不显示）所有警告；想看警告时把它换回 -Wall -Wextra
APP_CFLAGS  := -w -O2 --sysroot=$(SYSROOT) \
	       -I$(SYSROOT)/usr/include -I$(SYSROOT)/usr/include/libdrm
APP_LDFLAGS := --sysroot=$(SYSROOT) -L$(SYSROOT)/usr/lib
APP_LDLIBS  := -lavformat -lavcodec -lswscale -lavutil -ldrm -lm

.PHONY: all app clean

all: app

app: $(APP)

$(APP): main.c
	$(APP_CC) $(APP_CFLAGS) -o $@ $< $(APP_LDFLAGS) $(APP_LDLIBS)

clean:
	$(RM) $(APP)
