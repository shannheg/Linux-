# SPDX-License-Identifier: GPL-2.0
#
# alsa_max98357out —— SAI0 + MAX98357A 播放通路练习
#
#   make          默认目标：编用户态播放程序 main.c -> saiwav
#   make app      同上
#   make modules  编内核模块 alsa-max98357.ko（本目录 .c 还没写完整时先别执行）
#   make dtb      把 dp-rk3506g-mini.dts 拷进内核 dts 目录并单独编 .dtb（语法自检用）
#   make check-env 打印交叉编译器 / sysroot / alsa-lib 的探测结果
#   make clean
#
# 说明：
#   1) main.c 是**用户态**程序，只能走 app 规则交叉编译，千万别交给 Kbuild
#      （否则 main() 会和内核入口冲突，链接直接报错）。
#   2) 用内核现成的 maxim,max98357a + simple-audio-card 时不用写任何驱动，
#      modules 目标可以一直闲着，只需要设备树 + 打开 CONFIG_SND_SOC_MAX98357A。
#   3) kbuild 会优先包含同目录的 Kbuild，没有 Kbuild 时才包含本 Makefile
#      （scripts/Makefile.build:44），所以这一个文件既能当模块 Makefile 用，
#      也能直接 make 编 app —— 与 led_bus_device_tree/Makefile 的用法一致。
#   4) 编 saiwav 需要 alsa-lib 的头和库，它们来自 buildroot 的 sysroot：
#      SDK 没编过 buildroot（或没开 BR2_PACKAGE_ALSA_LIB）时就会报
#      “致命错误：alsa/asoundlib.h：没有那个文件或目录”，先跑 make check-env 确认。

# SDK 根目录自动推导：本文件在 <sdk>/kernel/driver_pratice/alsa_max98357out/，
# 往上三层就是 SDK 根；也可以命令行覆盖：make SDK_DIR=/path/to/sdk
SDK_DIR ?= $(abspath $(CURDIR)/../../..)
KERN_DIR ?= $(SDK_DIR)/kernel
ARCH ?= arm
CROSS_COMPILE ?= $(SDK_DIR)/prebuilts/gcc/linux-x86/arm/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-

# ---------------- 用户态程序 ----------------
APP := saiwav

# alsa-lib 的头/库在 buildroot 的 sysroot 里；不同 SDK 的配置名不一样，
# 这里自动挑一份已存在的 sysroot，也可以手动指定：
#   make SYSROOT=/path/to/buildroot/output/<config>/host/arm-buildroot-linux-gnueabihf/sysroot
SYSROOT ?= $(firstword $(wildcard $(SDK_DIR)/buildroot/output/*/host/*/sysroot))

# 编译器优先用 buildroot 生成的 wrapper（位于 output/<config>/host/bin/），它自带
# --sysroot；若直接用 prebuilts 里的裸交叉 gcc，必须手动加 --sysroot，
# 否则会报 “ld: cannot find /lib/libc.so.6”（它会去主机 /lib 找）
BR_HOST_BIN := $(abspath $(SYSROOT)/../../bin)
BR_WRAPPER  := $(firstword \
	$(wildcard $(BR_HOST_BIN)/arm-none-linux-gnueabihf-gcc) \
	$(wildcard $(BR_HOST_BIN)/arm-buildroot-linux-gnueabihf-gcc))
APP_CC ?= $(if $(BR_WRAPPER),$(BR_WRAPPER),$(CROSS_COMPILE)gcc)

ALSA_HDR    := $(SYSROOT)/usr/include/alsa/asoundlib.h
APP_CFLAGS  := -Wall -Wextra -O2 --sysroot=$(SYSROOT) -I$(SYSROOT)/usr/include
APP_LDFLAGS := --sysroot=$(SYSROOT) -L$(SYSROOT)/usr/lib
APP_LDLIBS  := -lasound

# ---------------- 内核模块（可选）----------------
# 源文件名带 '-' 没问题：kbuild 会用它生成 KBUILD_MODNAME 时把 '-' 换成 '_'
# （scripts/Makefile.lib 的 name-fix），生成的模块名就是 alsa-max98357.ko
obj-m += alsa-max98357.o

# ---------------- 设备树自检 ----------------
DTS_NAME := dp-rk3506g-mini

.PHONY: all app modules dtb check-env clean

all: app

app: $(APP)

$(APP): main.c
	@if [ -z "$(SYSROOT)" ] || [ ! -f "$(ALSA_HDR)" ]; then \
		echo ""; \
		echo "找不到 alsa-lib 头文件: $(ALSA_HDR)"; \
		echo "原因：SDK 还没编过 buildroot（或 buildroot 里没打开 BR2_PACKAGE_ALSA_LIB）"; \
		echo "解决："; \
		echo "  1) 在 SDK 根目录执行 ./build.sh buildroot  （会生成 rootfs + sysroot）"; \
		echo "     或在 buildroot 里 menuconfig 勾选 ALSA library / alsa-utils 后再 make"; \
		echo "  2) 如果 sysroot 在别处：make SYSROOT=/已存在的/sysroot"; \
		echo "  3) 先看诊断：make check-env"; \
		echo ""; \
		exit 1; \
	fi
	$(APP_CC) $(APP_CFLAGS) -o $@ $< $(APP_LDFLAGS) $(APP_LDLIBS)

# 环境自检：编译器 / sysroot / alsa-lib 头与库 / libc
check-env:
	@echo "SDK_DIR       : $(SDK_DIR)"
	@echo "SYSROOT       : $(SYSROOT)"
	@echo "APP_CC        : $(APP_CC)"
	@echo "CROSS_COMPILE : $(CROSS_COMPILE)"
	@if [ -x "$(APP_CC)" ] || command -v $(APP_CC) >/dev/null 2>&1; then \
		echo "  [OK]   APP_CC"; \
	else \
		echo "  [MISS] APP_CC（检查 SDK_DIR / buildroot 是否编过）"; \
	fi
	@for f in "$(ALSA_HDR)" "$(SYSROOT)/usr/lib/libasound.so" "$(SYSROOT)/usr/lib/libasound.so.2" \
		"$(SYSROOT)/lib/libc.so.6" "$(SYSROOT)/lib/ld-linux-armhf.so.3"; do \
		if [ -e "$$f" ]; then echo "  [OK]   $$f"; else echo "  [MISS] $$f"; fi; \
	done
	@echo "提示：板子上运行还需要 /usr/lib/libasound.so.2（rootfs 里得有 alsa-lib）"

# 编内核模块：和 led_bus_device_tree 一样，靠 kbuild 外部模块机制
modules:
	$(MAKE) -C $(KERN_DIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

# 只编译设备树，用来快速检查 dts 语法（完整镜像还是用 ./build.sh kernel）
dtb:
	cp -f $(DTS_NAME).dts $(KERN_DIR)/arch/arm/boot/dts/
	$(MAKE) -C $(KERN_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) $(DTS_NAME).dtb

clean:
	$(MAKE) -C $(KERN_DIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
	$(RM) $(APP)
