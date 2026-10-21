# SPDX-License-Identifier: GPL-2.0
#
# Linux 构建环境下，在本目录执行：
#   make check-env   检查交叉编译器、sysroot 和 ALSA 开发文件
#   make             编译 uac2_in_pcm5102_out
#   make clean       删除本程序生成的可执行文件
#
# 可覆盖：
#   make SYSROOT=/path/to/buildroot/output/<config>/host/<target>/sysroot
#   make SYSROOT=/path/to/sysroot APP_CC=/path/to/cross-gcc
# 本程序只依赖 alsa-lib，不需要 FFmpeg，也不通过 Kbuild 编译。

.DEFAULT_GOAL := all

SDK_DIR ?= $(abspath $(CURDIR)/../../..)
CROSS_COMPILE ?= $(SDK_DIR)/prebuilts/gcc/linux-x86/arm/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-

APP := uac2_in_pcm5102_out
SOURCE := uac2_in_pcm5102_out.c

# 沿用现有 ALSA 示例的 Buildroot sysroot 探测方式。
# 有多份 Buildroot 输出时，请显式指定与板端 rootfs 匹配的 SYSROOT。
SYSROOT ?= $(firstword $(wildcard $(SDK_DIR)/buildroot/output/*/host/*/sysroot))
BR_HOST_BIN := $(if $(strip $(SYSROOT)),$(abspath $(SYSROOT)/../../bin))
BR_WRAPPER := $(if $(BR_HOST_BIN),$(firstword \
	$(wildcard $(BR_HOST_BIN)/arm-none-linux-gnueabihf-gcc) \
	$(wildcard $(BR_HOST_BIN)/arm-buildroot-linux-gnueabihf-gcc)))
APP_CC ?= $(if $(BR_WRAPPER),$(BR_WRAPPER),$(CROSS_COMPILE)gcc)

ALSA_HDR := $(SYSROOT)/usr/include/alsa/asoundlib.h
APP_CFLAGS := -std=c11 -Wall -Wextra -O2 --sysroot=$(SYSROOT) -I$(SYSROOT)/usr/include
APP_LDFLAGS := --sysroot=$(SYSROOT) -L$(SYSROOT)/usr/lib
APP_LDLIBS := -lasound

.PHONY: all app check-env clean

all: app

app: $(APP)

# check-env 每次检查环境，但不使已有可执行文件无条件重新编译。
$(APP): $(SOURCE) Makefile | check-env
	$(APP_CC) $(CPPFLAGS) $(APP_CFLAGS) $(CFLAGS) -o "$@" "$<" $(APP_LDFLAGS) $(LDFLAGS) $(APP_LDLIBS) $(LDLIBS)

check-env:
	@echo "SDK_DIR : $(SDK_DIR)"
	@echo "SYSROOT : $(SYSROOT)"
	@echo "APP_CC  : $(APP_CC)"
	@if [ -z "$(SYSROOT)" ] || [ ! -d "$(SYSROOT)" ]; then \
		echo "[MISS] 未找到 Buildroot sysroot，请指定 SYSROOT=/实际路径"; \
		echo "请先在 Linux SDK 中构建启用 BR2_PACKAGE_ALSA_LIB 的 Buildroot。"; \
		exit 1; \
	fi
	@if [ -x "$(APP_CC)" ] || command -v "$(APP_CC)" >/dev/null 2>&1; then \
		echo "[OK] 编译器"; \
	else \
		echo "[MISS] 编译器不可用，请检查 APP_CC 或 CROSS_COMPILE"; \
		exit 1; \
	fi
	@if [ ! -f "$(ALSA_HDR)" ]; then \
		echo "[MISS] ALSA 头文件: $(ALSA_HDR)"; \
		exit 1; \
	fi
	@if [ ! -f "$(SYSROOT)/usr/lib/libasound.so" ] && \
		[ ! -f "$(SYSROOT)/usr/lib/libasound.a" ]; then \
		echo "[MISS] $(SYSROOT)/usr/lib 中缺少 libasound.so 或 libasound.a"; \
		echo "需要开发用链接库，只有 libasound.so.2 不足以使用 -lasound。"; \
		exit 1; \
	fi
	@echo "[OK] ALSA 开发文件"

clean:
	$(RM) -- $(APP)
