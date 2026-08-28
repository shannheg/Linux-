# SPDX-License-Identifier: GPL-2.0

SDK_DIR ?= /home/ubuntu/rk3506_linux6.1_sdk_v1.2.0_iot_evm
KERN_DIR ?= $(SDK_DIR)/kernel
ARCH ?= arm
CROSS_COMPILE ?= $(SDK_DIR)/prebuilts/gcc/linux-x86/arm/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-

APP := dpdrv

obj-m += test_drv.o

test_drv-objs := \
    board_demo.o \
    leddrv.o \
    chip_gpio.o

.PHONY: all modules app clean

all: modules app

modules:
	$(MAKE) -C $(KERN_DIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

app: $(APP)

$(APP): main.c
	$(CROSS_COMPILE)gcc -Wall -Wextra -O2 -o $@ $<

clean:
	$(MAKE) -C $(KERN_DIR) M=$(CURDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
	$(RM) $(APP)
