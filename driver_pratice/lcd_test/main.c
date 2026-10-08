// SPDX-License-Identifier: GPL-2.0
/*
 * lcd_test/main.c —— 在 RGB 屏(800x480)上循环播放 /oem/123.mp4
 *
 * 思路（RK3506 没有 GPU、也没有 VPU，所以只能软解 + KMS 直接上屏）：
 *   1) 通过 /sys/class/drm/ 自动找到“status=connected”的显示接口和对应的 card
 *   2) 用 libdrm 的 KMS 接口（dumb buffer + drmModePageFlip）直接输出，不需要
 *      X11/Wayland/OpenGL
 *   3) 用 FFmpeg(libavformat/libavcodec/libswscale) 软解 H.264，转成 XRGB8888
 *      后拷进 dumb buffer；读到文件尾就 seek 回 0 重新解，实现循环
 *
 * 依赖（需要在 buildroot 里打开，然后重编并重烧 rootfs）：
 *   BR2_PACKAGE_LIBDRM=y
 *   BR2_PACKAGE_FFMPEG=y     （libavcodec/libavformat/libswscale/libavutil）
 *
 * 编译：本目录直接 make（Makefile 会自动推导 SDK/工具链/sysroot）
 *
 * 用法：
 *   ./lcd_play                    # 循环播放 /oem/123.mp4
 *   ./lcd_play /oem/other.mp4     # 指定文件
 *   ./lcd_play list               # 只列出 /sys/class/drm 下的显示接口
 *
 * 注意：
 *   - 运行前确保没有别的程序占着屏幕：
 *       cat /sys/kernel/debug/dri/0/clients     （master=y 的那个要先停掉）
 *   - 视频建议先转成 800x480/15~20fps/H.264 baseline，否则 A7 软解会掉帧：
 *       ffmpeg -i in.mp4 -vf scale=800:480 -c:v libx264 -profile:v baseline \
 *              -pix_fmt yuv420p -r 20 -g 20 -bf 0 -b:v 800k -an -movflags +faststart 123.mp4
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>

#define DEF_FILE	"/oem/123.mp4"
#define SYSFS_DRM_DIR	"/sys/class/drm"
#define NBUF		2		/* dumb buffer 双缓冲 */
#define FMT_AV		AV_PIX_FMT_BGR0	/* 内存序 B,G,R,X == DRM 的 XRGB8888(小端) */

struct buf {
	uint32_t fb;
	uint32_t handle;
	uint32_t pitch;
	uint64_t size;
	void *map;
};

/* 注意：不能叫 drm_ctx —— libdrm 的 drm.h 里已有同名 struct drm_ctx */
struct lcd_drm {
	int fd;
	uint32_t conn_id;
	uint32_t crtc_id;
	drmModeModeInfo mode;
	drmModeCrtc *saved;
	struct buf buf[NBUF];
	int cur;
};

static volatile sig_atomic_t g_stop;
static volatile sig_atomic_t g_flip_pending;

static void on_sigint(int sig)
{
	(void)sig;
	g_stop = 1;
}

static double now_sec(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static const char *conn_type_name(uint32_t type)
{
	switch (type) {
	case DRM_MODE_CONNECTOR_VGA:		return "VGA";
	case DRM_MODE_CONNECTOR_DVII:		return "DVI-I";
	case DRM_MODE_CONNECTOR_DVID:		return "DVI-D";
	case DRM_MODE_CONNECTOR_DVIA:		return "DVI-A";
	case DRM_MODE_CONNECTOR_Composite:	return "Composite";
	case DRM_MODE_CONNECTOR_LVDS:		return "LVDS";
	case DRM_MODE_CONNECTOR_HDMIA:		return "HDMI-A";
	case DRM_MODE_CONNECTOR_HDMIB:		return "HDMI-B";
	case DRM_MODE_CONNECTOR_DisplayPort:	return "DP";
	case DRM_MODE_CONNECTOR_eDP:		return "eDP";
	case DRM_MODE_CONNECTOR_DSI:		return "DSI";
	case DRM_MODE_CONNECTOR_DPI:		return "DPI";
	case DRM_MODE_CONNECTOR_SPI:		return "SPI";
	case DRM_MODE_CONNECTOR_WRITEBACK:	return "Writeback";
	default:				return "Unknown";
	}
}

/* 列出 /sys/class/drm 下所有 card / connector 及状态（对应 [card*] [card*-XXX]） */
static void sysfs_list(void)
{
	DIR *d = opendir(SYSFS_DRM_DIR);
	struct dirent *de;

	if (!d) {
		perror(SYSFS_DRM_DIR);
		return;
	}

	printf("== %s ==\n", SYSFS_DRM_DIR);
	while ((de = readdir(d))) {
		char path[256], buf[128];
		FILE *f;

		if (strncmp(de->d_name, "card", 4) || !strncmp(de->d_name, "renderD", 7))
			continue;

		printf("%-20s", de->d_name);

		snprintf(path, sizeof(path), "%s/%s/status", SYSFS_DRM_DIR, de->d_name);
		f = fopen(path, "r");
		if (f) {
			if (fgets(buf, sizeof(buf), f))
				printf(" status=%s", strtok(buf, "\n"));
			fclose(f);
		}

		snprintf(path, sizeof(path), "%s/%s/modes", SYSFS_DRM_DIR, de->d_name);
		f = fopen(path, "r");
		if (f) {
			if (fgets(buf, sizeof(buf), f))
				printf(" mode=%s", strtok(buf, "\n"));
			fclose(f);
		}
		printf("\n");
	}
	closedir(d);
}

/*
 * 在 /sys/class/drm 里找第一个 connected 的 connector（形如 card0-RGB-1），
 * 返回 card 号（0/1/...），并把 ebtry 名字回传给调用者。
 */
static int sysfs_find_card(char *entry, size_t entry_sz)
{
	DIR *d = opendir(SYSFS_DRM_DIR);
	struct dirent *de;
	int card = -1;

	if (!d)
		return -1;

	while ((de = readdir(d))) {
		char path[256], buf[64];
		FILE *f;

		/* 只要 cardN-XXX 这种“card + connector”条目 */
		if (strncmp(de->d_name, "card", 4) || !strchr(de->d_name, '-'))
			continue;

		snprintf(path, sizeof(path), "%s/%s/status", SYSFS_DRM_DIR, de->d_name);
		f = fopen(path, "r");
		if (!f)
			continue;
		if (fgets(buf, sizeof(buf), f) && !strncmp(buf, "connected", 9)) {
			if (entry && entry_sz)
				snprintf(entry, entry_sz, "%s", de->d_name);
			card = atoi(de->d_name + 4);	/* 跳过 "card" */
			fclose(f);
			break;
		}
		fclose(f);
	}
	closedir(d);

	return card;
}

static int create_dumb(struct lcd_drm *c, int idx, uint32_t w, uint32_t h)
{
	struct drm_mode_create_dumb creq = {0};
	struct drm_mode_map_dumb mreq = {0};
	struct buf *b = &c->buf[idx];

	creq.width = w;
	creq.height = h;
	creq.bpp = 32;				/* XRGB8888 */
	if (drmIoctl(c->fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
		fprintf(stderr, "CREATE_DUMB failed: %s\n", strerror(errno));
		return -1;
	}
	b->handle = creq.handle;
	b->pitch = creq.pitch;
	b->size = creq.size;

	if (drmModeAddFB(c->fd, w, h, 24, 32, b->pitch, b->handle, &b->fb) < 0) {
		fprintf(stderr, "drmModeAddFB failed: %s\n", strerror(errno));
		return -1;
	}

	mreq.handle = b->handle;
	if (drmIoctl(c->fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
		fprintf(stderr, "MAP_DUMB failed: %s\n", strerror(errno));
		return -1;
	}

	b->map = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, c->fd, mreq.offset);
	if (b->map == MAP_FAILED) {
		b->map = NULL;
		fprintf(stderr, "mmap dumb failed: %s\n", strerror(errno));
		return -1;
	}

	memset(b->map, 0, b->size);

	return 0;
}

static void page_flip_handler(int fd, unsigned int frame, unsigned int sec,
			      unsigned int usec, void *data)
{
	(void)fd; (void)frame; (void)sec; (void)usec; (void)data;
	g_flip_pending = 0;
}

static int drm_flip(struct lcd_drm *c, int idx)
{
	drmEventContext ev = {
		.version = DRM_EVENT_CONTEXT_VERSION,
		.page_flip_handler = page_flip_handler,
	};
	struct pollfd pfd = { .fd = c->fd, .events = POLLIN };

	g_flip_pending = 1;
	if (drmModePageFlip(c->fd, c->crtc_id, c->buf[idx].fb,
			    DRM_MODE_PAGE_FLIP_EVENT, NULL) < 0) {
		fprintf(stderr, "drmModePageFlip failed: %s\n", strerror(errno));
		return -1;
	}

	while (g_flip_pending && !g_stop) {
		if (poll(&pfd, 1, 1000) > 0)
			drmHandleEvent(c->fd, &ev);
		else
			break;			/* 超时也别死等 */
	}

	return 0;
}

static void drm_cleanup(struct lcd_drm *c)
{
	int i;

	if (c->saved) {
		drmModeSetCrtc(c->fd, c->saved->crtc_id, c->saved->buffer_id,
			       c->saved->x, c->saved->y, &c->conn_id, 1,
			       &c->saved->mode);
		drmModeFreeCrtc(c->saved);
	}

	for (i = 0; i < NBUF; i++) {
		if (c->buf[i].fb)
			drmModeRmFB(c->fd, c->buf[i].fb);
		if (c->buf[i].map)
			munmap(c->buf[i].map, c->buf[i].size);
		if (c->buf[i].handle) {
			struct drm_mode_destroy_dumb dreq = { .handle = c->buf[i].handle };

			drmIoctl(c->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
		}
	}

	if (c->fd >= 0)
		close(c->fd);
	memset(c, 0, sizeof(*c));
	c->fd = -1;
}

static int drm_setup(struct lcd_drm *c, int card)
{
	char path[32];
	drmModeRes *res = NULL;
	drmModeConnector *conn = NULL;
	drmModeEncoder *enc = NULL;
	int i, ret = -1;

	memset(c, 0, sizeof(*c));
	c->fd = -1;

	snprintf(path, sizeof(path), "/dev/dri/card%d", card);
	c->fd = open(path, O_RDWR | O_CLOEXEC);
	if (c->fd < 0) {
		fprintf(stderr, "open %s failed: %s\n", path, strerror(errno));
		return -1;
	}

	res = drmModeGetResources(c->fd);
	if (!res) {
		fprintf(stderr, "drmModeGetResources failed: %s\n", strerror(errno));
		goto out;
	}

	/* 选第一个“已连接且有 mode”的 connector（本板只有 RGB 一个） */
	for (i = 0; i < res->count_connectors; i++) {
		conn = drmModeGetConnector(c->fd, res->connectors[i]);
		if (!conn)
			continue;
		if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0)
			break;
		drmModeFreeConnector(conn);
		conn = NULL;
	}
	if (!conn) {
		fprintf(stderr, "没有找到已连接的显示接口\n");
		goto out;
	}
	c->conn_id = conn->connector_id;

	/* 优先 800x480（本板屏），否则用 connector 的首选 mode */
	for (i = 0; i < conn->count_modes; i++) {
		if (conn->modes[i].hdisplay == 800 && conn->modes[i].vdisplay == 480)
			break;
	}
	if (i >= conn->count_modes)
		i = 0;
	c->mode = conn->modes[i];

	printf("card : /dev/dri/card%d\n", card);
	printf("conn : id=%u type=%s-%d  %ux%u@%uHz\n",
	       c->conn_id, conn_type_name(conn->connector_type),
	       conn->connector_type_id,
	       c->mode.hdisplay, c->mode.vdisplay, c->mode.vrefresh);

	/* 拿 CRTC：优先 connector 当前绑定的 encoder 的 crtc，否则退化用第一个 */
	if (conn->encoder_id) {
		enc = drmModeGetEncoder(c->fd, conn->encoder_id);
		if (enc && enc->crtc_id)
			c->crtc_id = enc->crtc_id;
	}
	if (!c->crtc_id) {
		if (res->count_crtcs <= 0) {
			fprintf(stderr, "没有可用的 CRTC\n");
			goto out;
		}
		c->crtc_id = res->crtcs[0];
	}
	printf("crtc : id=%u\n", c->crtc_id);

	c->saved = drmModeGetCrtc(c->fd, c->crtc_id);

	for (i = 0; i < NBUF; i++) {
		if (create_dumb(c, i, c->mode.hdisplay, c->mode.vdisplay) < 0)
			goto out;
	}

	/* 先把第一帧刷上去，后续用 page flip 切换 */
	if (drmModeSetCrtc(c->fd, c->crtc_id, c->buf[0].fb, 0, 0,
			   &c->conn_id, 1, &c->mode) < 0) {
		fprintf(stderr, "drmModeSetCrtc failed: %s（是不是有别的程序占着屏幕？）\n",
			strerror(errno));
		goto out;
	}
	c->cur = 0;
	ret = 0;

out:
	if (conn)
		drmModeFreeConnector(conn);
	if (enc)
		drmModeFreeEncoder(enc);
	if (res)
		drmModeFreeResources(res);
	if (ret < 0)
		drm_cleanup(c);
	return ret;
}

static void usage(const char *argv0)
{
	printf("usage: %s [mp4_file]\n", argv0);
	printf("       %s list\n", argv0);
	printf("  mp4_file : 默认 %s\n", DEF_FILE);
}

int main(int argc, char **argv)
{
	const char *file = DEF_FILE;
	AVFormatContext *fmt = NULL;
	AVCodec *codec = NULL;   /* 4.4 的 av_find_best_stream 形参是 AVCodec **，不能是 const */
	AVCodecContext *dec = NULL;
	AVPacket *pkt = NULL;
	AVFrame *frame = NULL;
	struct SwsContext *sws = NULL;
	struct lcd_drm drm;
	char entry[64] = {0};
	int vst, card;
	double fps = 25.0, t0, shown = 0;
	int sws_w = 0, sws_h = 0, sws_fmt = -1;
	int ret = 1;

	if (argc > 1) {
		if (!strcmp(argv[1], "list")) {
			sysfs_list();
			return 0;
		}
		if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
			usage(argv[0]);
			return 0;
		}
		file = argv[1];
	}

	signal(SIGINT, on_sigint);
	signal(SIGTERM, on_sigint);

	/* 1) 先看接口，再找 card */
	sysfs_list();
	card = sysfs_find_card(entry, sizeof(entry));
	if (card < 0) {
		fprintf(stderr, "在 %s 下没有找到 status=connected 的 connector\n",
			SYSFS_DRM_DIR);
		return 1;
	}
	printf("found: %s -> /dev/dri/card%d\n", entry, card);

	/* 2) 打开 DRM 并配好 KMS */
	if (drm_setup(&drm, card) < 0)
		return 1;

	/* 3) 打开视频并准备软解 */
	if (avformat_open_input(&fmt, file, NULL, NULL) < 0) {
		fprintf(stderr, "open %s failed\n", file);
		goto out_drm;
	}
	if (avformat_find_stream_info(fmt, NULL) < 0) {
		fprintf(stderr, "find_stream_info failed\n");
		goto out;
	}
	vst = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
	if (vst < 0) {
		fprintf(stderr, "没有视频流\n");
		goto out;
	}
	dec = avcodec_alloc_context3(codec);
	if (!dec || avcodec_parameters_to_context(dec, fmt->streams[vst]->codecpar) < 0) {
		fprintf(stderr, "avcodec_parameters_to_context failed\n");
		goto out;
	}
	dec->thread_count = 0;			/* 0 = 自动（A7 上帧级并行能明显加速） */
	if (avcodec_open2(dec, codec, NULL) < 0) {
		fprintf(stderr, "avcodec_open2(%s) failed\n", codec->name);
		goto out;
	}

	{
		AVRational r = av_guess_frame_rate(fmt, fmt->streams[vst], NULL);

		if (r.num && r.den)
			fps = av_q2d(r);
	}
	printf("file : %s\n", file);
	printf("video: %s %dx%d, fps=%.2f, pix_fmt=%s\n",
	       codec->name, dec->width, dec->height, fps,
	       av_get_pix_fmt_name(dec->pix_fmt) ?: "?");
	printf("scale: %dx%d -> %ux%u (BGR0/XRGB8888)\n", dec->width, dec->height,
	       drm.mode.hdisplay, drm.mode.vdisplay);

	pkt = av_packet_alloc();
	frame = av_frame_alloc();
	if (!pkt || !frame) {
		fprintf(stderr, "alloc packet/frame failed\n");
		goto out;
	}

	/* 4) 解码 + 上屏 + 循环 */
	t0 = now_sec();
	while (!g_stop) {
		int nxt;

		if (av_read_frame(fmt, pkt) < 0) {
			/* 到文件尾：seek 回 0 继续（这就是“循环播放”） */
			if (av_seek_frame(fmt, vst, 0, AVSEEK_FLAG_BACKWARD) < 0) {
				fprintf(stderr, "seek 失败，退出\n");
				break;
			}
			avcodec_flush_buffers(dec);
			shown = 0;
			t0 = now_sec();
			continue;
		}
		if (pkt->stream_index != vst) {
			av_packet_unref(pkt);
			continue;
		}

		if (avcodec_send_packet(dec, pkt) < 0) {
			av_packet_unref(pkt);
			continue;
		}
		av_packet_unref(pkt);

		while (!g_stop && avcodec_receive_frame(dec, frame) == 0) {
			uint8_t *dst[4];
			int dstlines[4];

			/* 分辨率/像素格式变了就重建转换上下文 */
			if (!sws || sws_w != frame->width || sws_h != frame->height ||
			    sws_fmt != frame->format) {
				sws_freeContext(sws);
				sws = sws_getContext(frame->width, frame->height,
						     frame->format,
						     drm.mode.hdisplay, drm.mode.vdisplay,
						     FMT_AV, SWS_BILINEAR,
						     NULL, NULL, NULL);
				if (!sws) {
					fprintf(stderr, "sws_getContext failed\n");
					av_frame_unref(frame);
					goto out;
				}
				sws_w = frame->width;
				sws_h = frame->height;
				sws_fmt = frame->format;
			}

			nxt = drm.cur ^ 1;	/* 往“后台缓冲”里画，画完 flip 过去 */

			dst[0] = drm.buf[nxt].map;
			dst[1] = dst[2] = dst[3] = NULL;
			dstlines[0] = drm.buf[nxt].pitch;
			dstlines[1] = dstlines[2] = dstlines[3] = 0;
			sws_scale(sws, (const uint8_t * const *)frame->data,
				  frame->linesize, 0, frame->height, dst, dstlines);

			if (drm_flip(&drm, nxt) < 0) {
				av_frame_unref(frame);
				goto out;
			}
			drm.cur = nxt;
			av_frame_unref(frame);

			shown++;

			/* 按视频帧率配速，避免软解跑满 CPU */
			if (fps > 0.1) {
				double wait = t0 + shown / fps - now_sec();

				if (wait > 0.001)
					usleep((useconds_t)(wait * 1e6));
			}
		}
	}
	printf("\nplay loop ended, total frames=%d\n", (int)shown);
	ret = 0;

out:
	if (sws)
		sws_freeContext(sws);
	av_frame_free(&frame);
	av_packet_free(&pkt);
	avcodec_free_context(&dec);
	if (fmt)
		avformat_close_input(&fmt);
out_drm:
	drm_cleanup(&drm);

	return ret;
}
