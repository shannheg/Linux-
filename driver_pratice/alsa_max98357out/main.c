// SPDX-License-Identifier: GPL-2.0
/*
 * main.c —— ALSA 播放测试程序（用户态）
 *
 * 作用：读取 /oem/known.wav，解析 WAV 头，把 PCM 数据写进既有的声卡
 *      （DTS: simple-audio-card "rk3506-max98357" = sai0 + maxim,max98357a）。
 *
 * 【重要】这不是内核模块，不要加进 Kbuild（driver_pratice/Makefile 别加这个目录），
 *        要用 SDK 的交叉工具链单独编译，然后把可执行文件拷到板子上运行。
 *
 * 编译：本目录已经配好 Makefile（照 led_bus_device_tree 的写法），直接
 *   make                   # 等价于 make app，交叉编译出 saiwav
 *   make SDK_DIR=/你的/sdk  # SDK 路径不同时在命令行覆盖
 * 手工编译的等价命令（需要临时改参数时用）：
 *   CROSS=<sdk>/prebuilts/gcc/linux-x86/arm/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-gcc
 *   SYSROOT=<sdk>/buildroot/output/rockchip_rk3506/host/arm-buildroot-linux-gnueabihf/sysroot
 *   $CROSS main.c -O2 -Wall -o saiwav -I$SYSROOT/usr/include -L$SYSROOT/usr/lib -lasound
 *
 * 用法：
 *   ./saiwav                        # 播放 /oem/known.wav 到 default 设备
 *   ./saiwav /oem/known.wav         # 指定 wav 文件
 *   ./saiwav /oem/known.wav hw:0,0  # 指定 PCM 设备（也可写 plughw:0,0）
 *   ./saiwav list                   # 只列出系统里的声卡和 PCM 设备
 *   ./saiwav -h                     # 帮助
 *
 * 板子上调试顺序建议：
 *   dmesg | grep -iE "sai|max98357|asoc"    # 确认 SAI/声卡注册成功
 *   ./saiwav list                           # 确认 card/pcm 节点
 *   ./saiwav                                # 放音
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <alsa/asoundlib.h>

#define DEF_WAV			"/oem/known.wav"
#define DEF_PCM			"default"
#define PERIOD_FRAMES		1024	/* 每次写 1024 帧 */
#define BUFFER_PERIODS		4	/* buffer = 4 个 period，抗调度抖动 */

/* WAV 头里我们关心的字段 */
struct wav_info {
	uint32_t rate;
	uint16_t channels;
	uint16_t bits;
	uint16_t type;		/* 1 = PCM, 0xFFFE = WAVE_FORMAT_EXTENSIBLE */
	uint16_t block_align;
	uint64_t data_off;	/* data 块里 PCM 数据的起始偏移 */
	uint64_t data_size;	/* data 块字节数 */
};

static uint16_t rd_u16(const unsigned char *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd_u32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* 读满 n 字节（read() 可能短读） */
static ssize_t read_full(int fd, void *buf, size_t n)
{
	unsigned char *p = buf;
	size_t done = 0;

	while (done < n) {
		ssize_t r = read(fd, p + done, n - done);

		if (r < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (r == 0)
			break;		/* 文件到尾 */
		done += r;
	}

	return done;
}

/* WAV 的位宽/类型 -> ALSA 的采样格式 */
static snd_pcm_format_t wav_to_alsa_format(const struct wav_info *wi)
{
	if (wi->type != 1)		/* 浮点/压缩等一律不支持 */
		return SND_PCM_FORMAT_UNKNOWN;

	switch (wi->bits) {
	case 8:
		return SND_PCM_FORMAT_U8;	/* WAV 的 8bit 是无符号 */
	case 16:
		return SND_PCM_FORMAT_S16_LE;
	case 24:
		return SND_PCM_FORMAT_S24_3LE;	/* WAV 的 24bit 是 3 字节紧凑打包 */
	case 32:
		return SND_PCM_FORMAT_S32_LE;
	default:
		return SND_PCM_FORMAT_UNKNOWN;
	}
}

/*
 * 解析 RIFF/WAVE：不假设"44 字节固定头"，而是按 chunk 遍历，
 * 这样带 LIST/fact/JUNK 等附加块（录音笔、Audacity 导出的 wav 常见）也能正常读。
 */
static int wav_parse(int fd, struct wav_info *wi)
{
	unsigned char hdr[12], ck[8];
	int have_fmt = 0, have_data = 0;
	off_t fsz;

	memset(wi, 0, sizeof(*wi));

	if (lseek(fd, 0, SEEK_SET) < 0) {
		perror("lseek");
		return -1;
	}
	if (read_full(fd, hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr) ||
	    memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) {
		fprintf(stderr, "not a RIFF/WAVE file\n");
		return -1;
	}

	fsz = lseek(fd, 0, SEEK_END);
	if (fsz < 12) {
		fprintf(stderr, "file too short\n");
		return -1;
	}
	lseek(fd, 12, SEEK_SET);

	while (!have_data) {
		unsigned char fmt[40];
		uint32_t sz, rd;
		off_t next;

		if (read_full(fd, ck, sizeof(ck)) != (ssize_t)sizeof(ck))
			break;			/* 读到文件尾也没找到 data */

		sz = rd_u32(ck + 4);
		/* chunk 按偶数字节对齐：大小为奇数时后面补 1 字节 */
		next = lseek(fd, 0, SEEK_CUR) + sz + (sz & 1);

		if (!memcmp(ck, "fmt ", 4)) {
			rd = sz > sizeof(fmt) ? sizeof(fmt) : sz;
			if (rd < 16 || read_full(fd, fmt, rd) != (ssize_t)rd) {
				fprintf(stderr, "bad fmt chunk\n");
				return -1;
			}
			wi->type	= rd_u16(fmt + 0);
			wi->channels	= rd_u16(fmt + 2);
			wi->rate	= rd_u32(fmt + 4);
			wi->block_align	= rd_u16(fmt + 12);
			wi->bits	= rd_u16(fmt + 14);
			if (wi->type == 0xFFFE && rd >= 26)
				wi->type = rd_u16(fmt + 24);	/* GUID 前两字节才是真格式 */
			have_fmt = 1;
		} else if (!memcmp(ck, "data", 4)) {
			wi->data_off  = lseek(fd, 0, SEEK_CUR);
			wi->data_size = sz;
			/* size 为 0（流式写入）或超过文件长度时，用文件长度兜底 */
			if (!wi->data_size ||
			    wi->data_off + wi->data_size > (uint64_t)fsz)
				wi->data_size = (uint64_t)fsz - wi->data_off;
			have_data = 1;
		}

		if (lseek(fd, next, SEEK_SET) < 0)
			break;
	}

	if (!have_fmt || !have_data) {
		fprintf(stderr, "no fmt/data chunk found\n");
		return -1;
	}
	if (!wi->channels || !wi->rate || !wi->bits) {
		fprintf(stderr, "invalid fmt: ch=%u rate=%u bits=%u\n",
			wi->channels, wi->rate, wi->bits);
		return -1;
	}

	return 0;
}

/* 列出系统里的声卡和 PCM 设备（等价于 aplay -l，rootfs 没装 alsa-utils 时很有用） */
static void list_cards(void)
{
	int card = -1;

	printf("---- sound cards ----\n");
	while (snd_card_next(&card) == 0 && card >= 0) {
		snd_ctl_t *ctl;
		snd_ctl_card_info_t *info;
		char ctlname[16];
		int dev = -1;

		snd_ctl_card_info_alloca(&info);
		snprintf(ctlname, sizeof(ctlname), "hw:%d", card);
		if (snd_ctl_open(&ctl, ctlname, 0) < 0)
			continue;
		if (snd_ctl_card_info(ctl, info) < 0) {
			snd_ctl_close(ctl);
			continue;
		}

		printf("card %d: %s [%s]\n", card,
		       snd_ctl_card_info_get_id(info),
		       snd_ctl_card_info_get_name(info));

		while (snd_ctl_pcm_next_device(ctl, &dev) == 0 && dev >= 0) {
			snd_pcm_info_t *pcmi;

			snd_pcm_info_alloca(&pcmi);
			snd_pcm_info_set_device(pcmi, dev);
			snd_pcm_info_set_subdevice(pcmi, 0);
			snd_pcm_info_set_stream(pcmi, SND_PCM_STREAM_PLAYBACK);
			if (snd_ctl_pcm_info(ctl, pcmi) < 0)
				continue;
			printf("  pcm hw:%d,%d  %s\n", card, dev,
			       snd_pcm_info_get_name(pcmi));
		}
		snd_ctl_close(ctl);
	}
	printf("---------------------\n");
}

static int play_wav(const char *path, const char *devname)
{
	struct wav_info wi;
	snd_pcm_t *pcm = NULL;
	snd_pcm_hw_params_t *hw;
	snd_pcm_format_t fmt;
	snd_pcm_uframes_t psize = PERIOD_FRAMES, bsize;
	unsigned char *buf = NULL;
	size_t frame_bytes, buf_bytes;
	uint64_t frames_total, frames_done = 0, remaining;
	snd_pcm_uframes_t period = PERIOD_FRAMES;
	unsigned int rate, last_sec = ~0u;
	int fd = -1, err, rc = 1;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "open %s failed: %s\n", path, strerror(errno));
		list_cards();
		return 1;
	}

	if (wav_parse(fd, &wi))
		goto out;

	fmt = wav_to_alsa_format(&wi);
	if (fmt == SND_PCM_FORMAT_UNKNOWN) {
		fprintf(stderr, "unsupported wav format: type=%u bits=%u\n",
			wi.type, wi.bits);
		goto out;
	}

	frame_bytes  = (size_t)wi.channels * (wi.bits / 8);
	frames_total = wi.data_size / frame_bytes;

	printf("file : %s\n", path);
	printf("wav  : %u Hz, %u ch, %u bit (%s), %llu frames, %.1f s\n",
	       wi.rate, wi.channels, wi.bits, snd_pcm_format_name(fmt),
	       (unsigned long long)frames_total,
	       (double)frames_total / (double)wi.rate);

	/* 打开 PCM 播放设备 */
	err = snd_pcm_open(&pcm, devname, SND_PCM_STREAM_PLAYBACK, 0);
	if (err < 0) {
		fprintf(stderr, "snd_pcm_open(%s) failed: %s\n",
			devname, snd_strerror(err));
		list_cards();
		goto out;
	}

	/* 配置硬件参数：交错访问 + WAV 的格式/声道/速率 */
	snd_pcm_hw_params_alloca(&hw);
	if ((err = snd_pcm_hw_params_any(pcm, hw)) < 0 ||
	    (err = snd_pcm_hw_params_set_access(pcm, hw,
						SND_PCM_ACCESS_RW_INTERLEAVED)) < 0 ||
	    (err = snd_pcm_hw_params_set_format(pcm, hw, fmt)) < 0 ||
	    (err = snd_pcm_hw_params_set_channels(pcm, hw, wi.channels)) < 0) {
		fprintf(stderr, "hw_params basic setup failed: %s\n", snd_strerror(err));
		goto out;
	}

	rate = wi.rate;
	if ((err = snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, 0)) < 0) {
		fprintf(stderr, "set_rate failed: %s\n", snd_strerror(err));
		goto out;
	}
	if ((err = snd_pcm_hw_params_set_period_size_near(pcm, hw, &period, 0)) < 0) {
		fprintf(stderr, "set_period_size failed: %s\n", snd_strerror(err));
		goto out;
	}
	bsize = (snd_pcm_uframes_t)period * BUFFER_PERIODS;
	if ((err = snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &bsize)) < 0) {
		fprintf(stderr, "set_buffer_size failed: %s\n", snd_strerror(err));
		goto out;
	}
	if ((err = snd_pcm_hw_params(pcm, hw)) < 0) {
		fprintf(stderr, "snd_pcm_hw_params failed: %s\n", snd_strerror(err));
		goto out;
	}

	snd_pcm_hw_params_get_period_size(hw, &psize, 0);
	snd_pcm_hw_params_get_buffer_size(hw, &bsize);
	snd_pcm_hw_params_get_rate(hw, &rate, 0);
	printf("pcm  : %s, rate=%u Hz, period=%lu frames, buffer=%lu frames\n",
	       devname, rate, (unsigned long)psize, (unsigned long)bsize);
	if (rate != wi.rate)
		printf("note : 硬件就近取到 %u Hz，与 wav 的 %u Hz 不同（会变调）\n",
		       rate, wi.rate);

	buf_bytes = (size_t)psize * frame_bytes;
	buf = malloc(buf_bytes);
	if (!buf) {
		fprintf(stderr, "malloc %zu failed\n", buf_bytes);
		goto out;
	}

	if (lseek(fd, (off_t)wi.data_off, SEEK_SET) < 0) {
		perror("lseek data");
		goto out;
	}

	/* 循环送数据；writei 返回实际写成功的帧数，可能小于请求值 */
	remaining = frames_total;
	while (remaining) {
		size_t want = (remaining < psize) ? (size_t)remaining : (size_t)psize;
		size_t got = read_full(fd, buf, want * frame_bytes);

		if (got <= 0)
			break;
		got /= frame_bytes;
		if (!got)
			break;

		err = snd_pcm_writei(pcm, buf, got);
		if (err == -EPIPE) {
			fprintf(stderr, "\nunderrun detected, recover\n");
			snd_pcm_prepare(pcm);
			continue;
		}
		if (err == -ESTRPIPE) {
			fprintf(stderr, "\npcm suspended, resume\n");
			while ((err = snd_pcm_resume(pcm)) == -EAGAIN)
				usleep(10000);
			if (err < 0)
				snd_pcm_prepare(pcm);
			continue;
		}
		if (err < 0) {
			fprintf(stderr, "\nsnd_pcm_writei failed: %s\n",
				snd_strerror(err));
			goto out;
		}

		remaining    -= (uint64_t)err;
		frames_done  += (uint64_t)err;

		if (rate && frames_done / rate != last_sec) {
			last_sec = frames_done / rate;
			printf("\rplaying %3llu%%  (%llu/%llu s)",
			       (unsigned long long)(frames_done * 100 / frames_total),
			       (unsigned long long)(frames_done / rate),
			       (unsigned long long)(frames_total / rate));
			fflush(stdout);
		}
	}

	snd_pcm_drain(pcm);
	printf("\rplay done: %llu frames\n", (unsigned long long)frames_done);
	rc = 0;

out:
	if (pcm)
		snd_pcm_close(pcm);
	free(buf);
	if (fd >= 0)
		close(fd);

	return rc;
}

static void usage(const char *argv0)
{
	printf("usage: %s [wav_file] [pcm_device]\n", argv0);
	printf("       %s list\n", argv0);
	printf("  wav_file   : default %s\n", DEF_WAV);
	printf("  pcm_device : default %s (可用 \"hw:0,0\" / \"plughw:0,0\")\n", DEF_PCM);
}

int main(int argc, char **argv)
{
	const char *wav = DEF_WAV;
	const char *dev = DEF_PCM;

	if (argc > 1) {
		if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
			usage(argv[0]);
			return 0;
		}
		if (!strcmp(argv[1], "list")) {
			list_cards();
			return 0;
		}
		wav = argv[1];
	}
	if (argc > 2)
		dev = argv[2];

	return play_wav(wav, dev);
}
