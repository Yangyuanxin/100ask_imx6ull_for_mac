/*
 * 02_framebuffer —— 帧缓冲直写演示 (1024x600, ARGB8888)
 *
 * 用法: 02_framebuffer [秒数]
 *   不传秒数则默认显示 5 秒后自动还原屏幕
 *   Ctrl-C 也会还原并退出
 *
 * 说明: 板上若有其它 GUI(LVGL/Qt)在绘制屏幕, 本程序写帧缓冲时会覆盖它们,
 *       退出时会把原来保存的画面写回。
 */
#include <fcntl.h>
#include <linux/fb.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static unsigned char *fb_mem;
static unsigned char *backup;
static struct fb_var_screeninfo vinfo;
static int fbfd;

static void restore(void)
{
    if (fb_mem && backup)
        memcpy(fb_mem, backup, vinfo.yres * vinfo.xres * vinfo.bits_per_pixel / 8);
}

static void cleanup(void)
{
    restore();
    free(backup);
    munmap(fb_mem, vinfo.yres * vinfo.xres * vinfo.bits_per_pixel / 8);
    close(fbfd);
}

static void on_signal(int sig)
{
    (void)sig;
    cleanup();
    printf("\n[02_framebuffer] 屏幕已还原\n");
    _exit(0);
}

static unsigned int hsv2rgb(int h, float s, float v)
{
    float c = v * s, x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1)), m = v - c;
    float r = 0, g = 0, b = 0;

    if (h < 60)      { r = c; g = x; }
    else if (h < 120){ r = x; g = c; }
    else if (h < 180){ g = c; b = x; }
    else if (h < 240){ g = x; b = c; }
    else if (h < 300){ r = x; b = c; }
    else             { r = c; b = x; }

    return 0xff000000u
         | ((unsigned)((r + m) * 255) << 16)
         | ((unsigned)((g + m) * 255) << 8)
         | ((unsigned)((b + m) * 255));
}

/*
 * 画一帧: 每行都是同一条调色板, 只是旋转了偏移量。
 * 逐像素做 HSV->RGB 浮点运算在单核 i.MX6ULL 上实测只有 ~5 FPS,
 * 改成"每行算一条 palette + memcpy 整块写"能显著提速。
 * (帧缓冲通常映射为 non-cacheable, 写带宽本身就是瓶颈)
 */
static void render_frame(unsigned int *px, unsigned int *palette,
                         __u32 w, __u32 h, int stride, int frame)
{
    for (__u32 x = 0; x < w; x++) {
        int hue = (int)(360.0f * x / w + frame * 4) % 360;
        palette[x] = hsv2rgb(hue, 1.0f, 1.0f);
    }
    for (__u32 y = 0; y < h; y++) {
        __u32 shift = (__u32)(frame * 2 + y * 3) % w;
        __u32 n1 = w - shift;
        memcpy(px + (size_t)y * stride, palette + shift, n1 * 4);
        memcpy(px + (size_t)y * stride + n1, palette, shift * 4);
    }
}

int main(int argc, char **argv)
{
    int seconds = (argc > 1) ? atoi(argv[1]) : 5;
    long screen_size;
    struct fb_fix_screeninfo finfo;

    fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd < 0) {
        perror("open /dev/fb0");
        return 1;
    }
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) < 0 ||
        ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        perror("ioctl fb");
        close(fbfd);
        return 1;
    }
    screen_size = vinfo.yres * finfo.line_length;
    printf("[02_framebuffer] %ldx%ld bpp=%ld line_length=%ld (line_length 与 xres*bpp/8 可能不同)\n",
           (long)vinfo.xres, (long)vinfo.yres, (long)vinfo.bits_per_pixel, (long)finfo.line_length);

    fb_mem = mmap(NULL, screen_size, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if (fb_mem == MAP_FAILED) {
        perror("mmap");
        close(fbfd);
        return 1;
    }
    backup = malloc(screen_size);
    if (!backup) {
        perror("malloc");
        return 1;
    }
    memcpy(backup, fb_mem, screen_size);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    printf("[02_framebuffer] 显示 %d 秒 ...\n", seconds);
    /*
     * 按墙钟计时: i.MX6ULL 单核逐像素画满一屏要几十毫秒,
     * 不能假设固定 30FPS, 否则实际时长会远超传入的秒数。
     */
    unsigned int *palette = malloc(vinfo.xres * sizeof(*palette));
    if (!palette) {
        perror("malloc palette");
        return 1;
    }

    time_t t0 = time(NULL);
    int f = 0;
    while (difftime(time(NULL), t0) < seconds) {
        render_frame((unsigned int *)fb_mem, palette,
                     vinfo.xres, vinfo.yres, finfo.line_length / 4, f);
        f++;
        struct timespec ts = { 0, 20 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
    free(palette);
    printf("[02_framebuffer] 共绘制 %d 帧, 约 %d FPS\n", f, seconds > 0 ? f / seconds : f);

    cleanup();
    printf("[02_framebuffer] 完成, 屏幕已还原\n");
    return 0;
}
