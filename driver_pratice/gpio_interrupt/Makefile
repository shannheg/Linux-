# SPDX-License-Identifier: GPL-2.0
#
# 单独编译成模块时使用的 Makefile（编入内核请用同目录的 Kbuild）。
# 用法：先把 Kbuild 改名屏蔽，再执行 make，得到
#   interrupt-key-led.ko / interrupt_key.ko
# 然后 insmod 加载。

SDK_DIR ?= /home/sk/rk3506_linux6.1_sdk_v1.2.0_iot_evm
KERN_DIR ?= $(SDK_DIR)/kernel
ARCH ?= arm
CROSS_COMPILE ?= $(SDK_DIR)/prebuilts/gcc/linux-x86/arm/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-

obj-m += interrupt-key-led.o
obj-m += interrupt_key.o

.PHONY: all modules clean

all: modules

modules:
	$(MAKE) -C $(KERN_DIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KERN_DIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
