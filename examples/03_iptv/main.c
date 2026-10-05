/*
 * 03_iptv —— i.MX6ULL 上的网络电视
 *
 *   - 从 channels.conf 读取频道列表 (格式: 频道名|URL)
 *   - 调用板子上的 ffmpeg 解码 -> 直接输出到 /dev/fb0 (1024x600)
 *   - 触摸屏左/右 1/3 区域: 上一台 / 下一台; 中间: 弹出频道列表
 *   - 板载 gpio-keys 或 USB 键盘上下键也能换台
 *   - 播放中断(直播断流)自动重连
 *
 * 用法: iptv [-c 频道文件] [-t 触摸设备] [-k 按键设备] [-a 开音频] [-w 宽] [-H 高]
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_CH   64
#define NAME_LEN 128
#define URL_LEN  512

typedef struct {
    char name[NAME_LEN];
    char url[URL_LEN];
} channel_t;

/* ------------------------------- 全局状态 ------------------------------- */
static channel_t g_ch[MAX_CH];
static int       g_nch;
static int       g_cur;
static pid_t     g_pid = -1;
static int       g_audio;
static int       g_w, g_h;                /* 0 = 启动时自动探测 fb0 */
static char      g_pixfmt[16] = "bgra";   /* 输出格式由 fb0 位深决定 */
static char      g_bg[64] = "color=c=0x101830:s=1024x600:d=1";   /* 菜单背景 */
static int       g_menu;                       /* 1 = 正在显示频道列表 */
static const char *g_font = "/usr/share/fonts/ttf/msyh.ttc";
static const char *g_conf = "/etc/iptv/channels.conf";
static const char *g_touch = "/dev/input/event2";
static const char *g_key = "/dev/input/event3";

static volatile sig_atomic_t g_quit;
static volatile sig_atomic_t g_req;     /* 1=下一台 2=上一台 3=菜单 */

static void on_signal(int s)
{
    if (s == SIGUSR1)      g_req = 1;   /* kill -USR1 -> 下一台 */
    else if (s == SIGUSR2) g_req = 2;   /* kill -USR2 -> 上一台 */
    else if (s == SIGHUP)  g_req = 3;   /* kill -HUP  -> 频道列表 */
    else                   g_quit = 1;
}

/* --------------------------- 频道配置文件解析 --------------------------- */
static int load_channels(const char *path)
{
    FILE *fp = fopen(path, "r");
    char line[1024];

    if (!fp) {
        fprintf(stderr, "[iptv] 打不开频道文件 %s: %s\n", path, strerror(errno));
        return -1;
    }
    g_nch = 0;
    while (fgets(line, sizeof(line), fp) && g_nch < MAX_CH) {
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0')
            continue;
        char *sep = strchr(p, '|');
        if (!sep)
            continue;
        *sep = '\0';
        char *url = sep + 1;
        char *nl = strpbrk(p, "\r\n");
        if (nl)
            *nl = '\0';
        nl = strpbrk(url, "\r\n");
        if (nl)
            *nl = '\0';
        while (*url == ' ' || *url == '\t')
            url++;
        snprintf(g_ch[g_nch].name, NAME_LEN, "%.*s", NAME_LEN - 1, p);
        snprintf(g_ch[g_nch].url, URL_LEN, "%.*s", URL_LEN - 1, url);
        g_nch++;
    }
    fclose(fp);
    if (!g_nch) {
        fprintf(stderr, "[iptv] %s 里没有可用频道\n", path);
        return -1;
    }
    return 0;
}

/*
 * 防"自动休眠"黑屏:
 *   内核默认 consoleblank=600 —— console 空闲 10 分钟就把 framebuffer blank 掉,
 *   表现就是"看着看着自己黑屏"。背光其实是好的(实测 bl_power=0 brightness=8/8),
 *   所以不是背光节能, 得从这里治:
 *     1) tty1 上写 ESC[9;0] -> 关掉那个定时器(治本, 等价于 setterm -blank 0)
 *     2) fb0/blank = 0      -> 万一已经被 blank, 立刻点亮(治标)
 *
 * ⚠️ 注意 /sys/module/kernel/parameters/consoleblank 在这块板上是**只读**的
 *    (-r--r--r--), 写它只会拿到 Permission denied —— 别指望那条路。
 *    真要永久生效得改内核启动参数: fw_setenv 加 consoleblank=0(见 S95iptv)。
 *
 * 定时器可能被别的东西(tty 切换/getty 重启)重新设回去, 所以要周期性地写。
 */
static void keep_screen_on(void)
{
    /* ESC[9;n] = 设置 console blank 间隔(分钟), 0 = 永不黑屏 */
    int fd = open("/dev/tty1", O_WRONLY | O_NOCTTY);
    if (fd >= 0) {
        (void)!write(fd, "\033[9;0]", 6);
        close(fd);
    }

    FILE *fp = fopen("/sys/class/graphics/fb0/blank", "w");
    if (fp) { fputs("0", fp); fclose(fp); }

    /* 关掉光标闪烁 */
    fp = fopen("/sys/class/graphics/fbcon/cursor_blink", "w");
    if (fp) { fputs("0", fp); fclose(fp); }

    /*
     * 彻底做法: 把 framebuffer console 从 fb0 上解绑。
     * 只要 fbcon 还绑着, 它就和 ffmpeg 抢同一块显存 —— 光标闪烁是它在画,
     * 内核 printk / getty 的登录提示也会直接叠在视频画面上。
     * ⚠️ 是 vtcon1 不是 vtcon0: 实测 vtcon0 = "(S) dummy device",
     *    vtcon1 = "(M) frame buffer device" 才是真正画到屏幕的那个。
     * 解绑后 console 输出照常在串口(ttymxc0), 不影响调试。
     */
    fp = fopen("/sys/class/vtconsole/vtcon1/bind", "w");
    if (fp) { fputs("0", fp); fclose(fp); }
}

/*
 * 屏幕参数不能写死: 这块板重启后 fb0 可能是 1024x600/32bpp,
 * 也可能是 1280x720/16bpp —— 两种都实测出现过。写错的话 ffmpeg 会直接失败:
 *     [fbdev] Pixel format bgra is not supported, use rgb565le
 *     av_interleaved_write_frame(): Invalid argument
 * (症状很隐蔽: 进程一直在跑, 但画面根本没写进去)
 * 所以启动时读 sysfs 拿真实宽高和位深, 再决定输出像素格式。
 */
static void detect_fb(void)
{
    int w = 0, h = 0, bpp = 0;
    FILE *fp = fopen("/sys/class/graphics/fb0/virtual_size", "r");

    if (fp) {
        if (fscanf(fp, "%d,%d", &w, &h) != 2)
            w = h = 0;
        fclose(fp);
    }
    fp = fopen("/sys/class/graphics/fb0/bits_per_pixel", "r");
    if (fp) {
        if (fscanf(fp, "%d", &bpp) != 1)
            bpp = 0;
        fclose(fp);
    }

    if (!g_w)
        g_w = w > 0 ? w : 1024;
    if (!g_h)
        g_h = h > 0 ? h : 600;

    /* fbdev 只认设备真正支持的格式 */
    if (bpp == 16)
        snprintf(g_pixfmt, sizeof(g_pixfmt), "rgb565le");
    else if (bpp == 24)
        snprintf(g_pixfmt, sizeof(g_pixfmt), "bgr24");
    else
        snprintf(g_pixfmt, sizeof(g_pixfmt), "bgra");

    snprintf(g_bg, sizeof(g_bg), "color=c=0x101830:s=%dx%d:d=1", g_w, g_h);

    printf("[iptv] 屏幕 %dx%d, %d bpp, 输出 %s\n", g_w, g_h, bpp, g_pixfmt);
    keep_screen_on();     /* 开机先关掉自动黑屏 */
    fflush(stdout);
}

/*
 * HDMI 输出状态。
 *
 * 这块板的 HDMI 是 sii902x 发射芯片, 它把 LCDIF 的并行 RGB 信号转成 HDMI,
 * 用的就是同一个 fb0 —— 所以 HDMI 屏上看到的就是 fb0 的内容, 和 LCD 完全同源,
 * 不需要(也没办法)往第二块 framebuffer 再写一份, 插上就有画面。
 *
 * 但有个坑: 内核驱动检测到插入后会读电视 EDID, 再 fb_set_var() 改掉 fb0 的
 * 分辨率(见 drivers/video/fbdev/mxc/mxsfb_sii902x.c 的 sii902x_cable_connected)。
 * 这一改, 我们启动时探测的屏幕尺寸就作废了 —— ffmpeg 还按旧尺寸输出,
 * 画面会错位, 甚至因为格式/尺寸不匹配直接写不进去(症状是进程在跑但没画面)。
 * 所以主循环里盯着插拔, 分辨率一变就重新探测 + 重开播放。
 */
static int hdmi_cable(void)
{
    FILE *fp = fopen("/sys/bus/i2c/devices/1-0039/cable_state", "r");
    char b[32] = {0};

    if (!fp)
        return -1;                      /* 板上没有这颗芯片 */
    if (fgets(b, sizeof(b), fp)) {
        fclose(fp);
        return strncmp(b, "plugin", 6) == 0 ? 1 : 0;
    }
    fclose(fp);
    return -1;
}

static void fb_size_now(int *w, int *h)
{
    FILE *fp = fopen("/sys/class/graphics/fb0/virtual_size", "r");

    *w = *h = 0;
    if (fp) {
        if (fscanf(fp, "%d,%d", w, h) != 2)
            *w = *h = 0;
        fclose(fp);
    }
}

/* --------------------------- 屏幕模式(分辨率) --------------------------- */
/*
 * 插上 HDMI 时内核的 sii902x 驱动会读电视 EDID, 然后直接 fb_set_var() 把 fb0
 * 改成电视的分辨率(实测 1024x600 -> 1280x720)。麻烦在于 LCD 面板是固定的
 * 1024x600、一个物理像素对一个数据像素、不会自己缩放: 拿一路 1280x720 的信号
 * 它只能按自己的时序去采, 采不到的地方就变成黑边(实测左边约 1/5 黑)。
 *
 * HDMI 和 LCD 共用同一个 fb0, 分辨率只能二选一, 所以给一个策略开关:
 *   IPTV_SCREEN=lcd   (默认) 改回面板原生 1024x600 —— LCD 正常, 电视自己缩放
 *   IPTV_SCREEN=hdmi        用电视的 1280x720 —— 电视最清晰, 代价是 LCD 有黑边
 */
typedef struct {
    const char *name;
    int w, h, bpp;
    int pixclock;          /* ps */
    int lm, rm;            /* 水平后肩 / 前肩 */
    int um, bm;            /* 垂直后肩 / 前肩 */
    int hs, vs;            /* 同步脉宽 */
} fb_mode;

/*
 * LCD 面板时序来自设备树 display#1/display-timings/timing0_1024x768
 * (名字写着 768, 实测 hactive=1024 vactive=600, clock=50MHz -> 58.59Hz)。
 * 这组数是面板厂给的, 必须照抄 —— 换成别的时序面板就认不出信号。
 */
static const fb_mode MODE_LCD = {
    "LCD 1024x600", 1024, 600, 32, 20000, 140, 160, 20, 12, 20, 3
};

/* HDMI 电视: CEA-861 标准 1280x720@60 */
static const fb_mode MODE_HDMI = {
    "HDMI 1280x720", 1280, 720, 32, 13468, 220, 110, 20, 5, 40, 5
};

static const fb_mode *screen_mode(void)
{
    const char *s = getenv("IPTV_SCREEN");

    if (s && strncmp(s, "hdmi", 4) == 0)
        return &MODE_HDMI;
    return &MODE_LCD;
}

/*
 * 把 fb0 切到指定模式。返回: 1=已切换  0=本来就是  -1=失败。
 * 只改必要的字段: red/green/blue 位域和 sync 极性都保留驱动给的
 * (sync 来自 LCD 设备树的 hsync/vsync-active, 动它画面会反色或抖)。
 */
static int fb_set_mode(const fb_mode *m)
{
    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0)
        return -1;

    struct fb_var_screeninfo v;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &v) < 0) {
        close(fd);
        return -1;
    }
    if (v.xres == m->w && v.yres == m->h && v.bits_per_pixel == m->bpp) {
        close(fd);
        return 0;
    }

    v.xres = v.xres_virtual = m->w;
    v.yres = v.yres_virtual = m->h;
    v.xoffset = v.yoffset = 0;
    v.bits_per_pixel = m->bpp;
    v.pixclock = m->pixclock;
    v.left_margin = m->lm;
    v.right_margin = m->rm;
    v.upper_margin = m->um;
    v.lower_margin = m->bm;
    v.hsync_len = m->hs;
    v.vsync_len = m->vs;
    v.vmode = FB_VMODE_NONINTERLACED;
    v.activate = FB_ACTIVATE_NOW;

    int r = ioctl(fd, FBIOPUT_VSCREENINFO, &v);
    close(fd);
    return r < 0 ? -1 : 1;
}

/* ------------------------------ ffmpeg 调用 ---------------------------- */
/*
 * drawtext 的 text 参数里 : \ ' % 都有特殊含义, 需要转义,
 * 否则频道名带这些字符时 ffmpeg 会直接报错。
 */
static void esc_text(char *dst, size_t n, const char *src)
{
    size_t d = 0;
    for (const char *s = src; *s && d + 2 < n; s++) {
        if (*s == '\\' || *s == ':' || *s == '\'' || *s == '%' || *s == ',')
            dst[d++] = '\\';
        dst[d++] = *s;
    }
    dst[d] = '\0';
}

static void spawn_ffmpeg(char *const argv[])
{
    pid_t p = fork();
    if (p < 0)
        return;
    if (p == 0) {
        /* 子进程: 关掉标准输出, 免得 ffmpeg 的日志刷到串口控制台上 */
        int fd = open("/dev/null", O_WRONLY);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        setpgid(0, 0);
        execvp("ffmpeg", argv);
        _exit(127);
    }
    g_pid = p;
}

static void stop_play(void)
{
    if (g_pid > 0) {
        kill(-g_pid, SIGTERM);          /* 杀整个进程组(ffmpeg 可能有子线程) */
        kill(g_pid, SIGKILL);
        int st;
        waitpid(g_pid, &st, 0);
        g_pid = -1;
    }
}

static int alive(void)
{
    return g_pid > 0 && kill(g_pid, 0) == 0;
}

/*
 * 换台过渡。
 * 从杀掉上一个 ffmpeg 到新频道第一帧画面出现, 中间有 1~2 秒的空档
 * (HLS 要重新探测 + 缓冲, 这个延迟没法治), 这段时间屏幕上还留着
 * 上一频道的最后一帧 —— 用户看到的就是"按了键, 卡住了"。
 * 所以这里先画一张"正在切换到 XXX"顶上去, 让按键立刻有反馈:
 * 起播慢这件事改不掉, 但"看得出它在切"和"以为死机了"是两种体验。
 */
static void show_switching(int idx)
{
    static int first = 1;          /* 开机第一次不叫"切换", 叫"载入" */
    char vf[2048], name[512], sub[512];

    esc_text(name, sizeof(name), g_ch[idx].name);
    snprintf(sub, sizeof(sub), "第 %d / %d 个频道", idx + 1, g_nch);

    snprintf(vf, sizeof(vf),
             "drawtext=fontfile=%s:text='%s':x=(w-text_w)/2:y=%d:fontsize=28:fontcolor=#8899aa,"
             "drawtext=fontfile=%s:text='%s':x=(w-text_w)/2:y=%d:fontsize=46:fontcolor=white,"
             "drawtext=fontfile=%s:text='%s':x=(w-text_w)/2:y=%d:fontsize=24:fontcolor=#667788",
             g_font, first ? "正在载入" : "正在切换", g_h / 2 - 100,
             g_font, name, g_h / 2 - 40,
             g_font, sub, g_h / 2 + 40);
    first = 0;

    char *argv[32];
    int i = 0;
    argv[i++] = (char *)"ffmpeg";
    argv[i++] = (char *)"-nostdin";
    argv[i++] = (char *)"-loglevel";
    argv[i++] = (char *)"error";
    argv[i++] = (char *)"-f";
    argv[i++] = (char *)"lavfi";
    argv[i++] = (char *)"-i";
    argv[i++] = g_bg;
    argv[i++] = (char *)"-vf";
    argv[i++] = vf;
    argv[i++] = (char *)"-frames:v";
    argv[i++] = (char *)"1";
    argv[i++] = (char *)"-pix_fmt";
    argv[i++] = g_pixfmt;
    argv[i++] = (char *)"-f";
    argv[i++] = (char *)"fbdev";
    argv[i++] = (char *)"/dev/fb0";
    argv[i] = NULL;

    spawn_ffmpeg(argv);
    /* 等这张图画完再起播放器, 否则两边抢 fb0 */
    if (g_pid > 0) {
        int st;
        waitpid(g_pid, &st, 0);
        g_pid = -1;
    }
}

/* 播放第 idx 个频道, 画面上叠加 8 秒的频道名 OSD */
static void play(int idx)
{
    if (idx < 0)
        idx = g_nch - 1;
    if (idx >= g_nch)
        idx = 0;
    stop_play();
    g_cur = idx;
    g_menu = 0;

    /* 起播要 1~2 秒, 先给个"正在切换"的反馈, 不然按键像是没反应 */
    show_switching(idx);

    char osd[1536], name[512], scalef[256], fpsbuf[32];
    esc_text(name, sizeof(name), g_ch[idx].name);

    /*
     * 缩放策略 —— 这才是这块板的性能瓶颈所在。
     * 实测(792MHz, 640x360 的源, 端到端真写屏幕):
     *     放大到 1024x600 满屏 ....... 11 FPS, CPU 92%
     *     放大到 800x450 ............. 14 FPS, CPU 79%
     *     不放大(1:1, 四周补黑边) .... 22 FPS  ← 差一倍
     * 而纯解码(不缩放不写屏)能到 47 FPS, 说明解码根本不是瓶颈,
     * 贵的是 swscale 的插值和每帧 2.4MB 的 framebuffer 写入。
     *   IPTV_SCALE=native (默认) 不放大: 最快, 代价是画面不满屏
     *   IPTV_SCALE=fit           最多 0.78 屏宽: 折中
     *   IPTV_SCALE=full          放大到满屏: 画面最好, 但只有 11 FPS
     * 表达式用 min(iw, X) 是为了两头都照顾: 比屏幕小的源不放大,
     * 比屏幕大的源(720p/1080p)照样缩到屏幕内。
     */
    const char *sm = getenv("IPTV_SCALE");
    if (sm && strncmp(sm, "native", 6) == 0) {
        /* 不放大(1:1 直通): 最快, 但画面只占屏宽 62%, 四周黑边明显 */
        snprintf(scalef, sizeof(scalef), "scale=w='min(iw\\,%d)':h=-2", g_w);
    } else if (sm && strncmp(sm, "fit", 3) == 0) {
        snprintf(scalef, sizeof(scalef), "scale=%d:-2", g_w * 85 / 100);
    } else {
        /*
         * 默认: 放大到满屏。
         * 实测(本地素材)放大到 85% 屏宽 13 FPS、满屏 12 FPS —— 几乎一样贵,
         * 说明开销花在"放大"这个动作本身, 跟放大多少关系不大。
         * 既然代价都要付, 那就直接给满屏: 640x360 的源放大成 1024x576,
         * 上下只剩 24px, 黑边基本看不见。
         */
        snprintf(scalef, sizeof(scalef),
                 "scale=%d:%d:force_original_aspect_ratio=decrease", g_w, g_h);
    }

    /*
     * IPTV_FPS=N: 限制输出帧率。CPU 跑满(90%+)时帧间隔会抖, 而且一忙
     * 直播间延迟会越积越大; 限帧能留出余量、画面更稳也更凉快。默认不限。
     * fps 滤镜故意放在最前面 —— 先丢帧, 后面的缩放就不必为丢掉的帧白干活。
     */
    fpsbuf[0] = '\0';
    const char *fe = getenv("IPTV_FPS");
    if (fe) {
        int fps = atoi(fe);
        if (fps > 0 && fps < 60)
            snprintf(fpsbuf, sizeof(fpsbuf), "fps=%d,", fps);
    }

    snprintf(osd, sizeof(osd),
             "%s%s,"
             "pad=%d:%d:(ow-iw)/2:(oh-ih)/2:color=black,"
             "drawtext=fontfile=%s:text='%s':x=36:y=28:fontsize=34:"
             "fontcolor=white:shadowcolor=black:shadowx=2:shadowy=2:enable='lt(t\\,8)'",
             fpsbuf, scalef, g_w, g_h, g_font, name);

    /*
     * 低延迟那几个参数(nobuffer / 很小的 analyzeduration)实测会让起播失败:
     *   - HLS: "Error when loading first segment ... Immediate exit requested"
     *     (要读几个分片才能确定流信息)
     *   - http mp4: 探测数据被压掉, 直接退出
     * 所以只对真正的实时流(rtmp/rtsp/udp/rtp)才加, 其余一律用保守参数。
     */
    const char *url = g_ch[idx].url;
    int lowlat = strncmp(url, "rtmp", 4) == 0 || strncmp(url, "rtsp", 4) == 0 ||
                 strncmp(url, "udp:", 4) == 0 || strncmp(url, "rtp:", 4) == 0;

    char *argv[64];
    int i = 0;
    argv[i++] = (char *)"ffmpeg";
    argv[i++] = (char *)"-nostdin";
    argv[i++] = (char *)"-loglevel";
    argv[i++] = (char *)"warning";
    argv[i++] = (char *)"-rw_timeout";
    argv[i++] = (char *)"15000000";     /* 15s 读不到数据就退出, 由本程序重连 */
    /*
     * 别加 -threads 1。直觉上单核应该单线程最快, 实测恰恰相反:
     * 同样的流, 默认(多线程)19 FPS, 强制单线程只有 14 FPS —— 多线程能让
     * 解码和后面的缩放/写屏流水重叠起来, 单核照样有收益。
     */
    if (lowlat) {
        argv[i++] = (char *)"-fflags";
        argv[i++] = (char *)"nobuffer+discardcorrupt";
        argv[i++] = (char *)"-flags";
        argv[i++] = (char *)"low_delay";
        /*
         * 以前这里还有一个 -framedrop, 实际上这个 ffmpeg(4.2.3, Buildroot 编的)
         * 根本没编进这个选项, 结果是 rtmp/rtsp 源一上来就
         * "Unrecognized option 'framedrop'" 直接起不来。要丢帧保实时用 IPTV_FPS。
         */
        argv[i++] = (char *)"-probesize";
        argv[i++] = (char *)"500000";
        argv[i++] = (char *)"-analyzeduration";
        argv[i++] = (char *)"500000";
    } else {
        /*
         * 直播源给足探测数据(-probesize 10M), 不然 HLS 起播容易失败。
         * 这两个参数就是 ffplay 默认的 -probesize 10M / -max_delay 800000。
         */
        argv[i++] = (char *)"-probesize";
        argv[i++] = (char *)"10000000";
    }
    argv[i++] = (char *)"-max_delay";
    argv[i++] = (char *)"800000";      /* 0.8s: 分片等太久就放弃, 交给上层重连 */
    argv[i++] = (char *)"-i";
    argv[i++] = (char *)url;
    argv[i++] = (char *)"-map";
    argv[i++] = (char *)"0:v:0";        /* 只取第一条视频轨, 别去解多余变体 */
    if (!g_audio)
        argv[i++] = (char *)"-an";
    /*
     * IPTV_LOWCPU=1: 只解关键帧(丢掉非参考帧)。
     * 这块板是纯 CPU 软解, 720p/1080p 直播源只有 3~5 FPS,
     * 开这个能明显流畅一些, 代价是画面会有跳动。
     */
    const char *lowcpu = getenv("IPTV_LOWCPU");
    if (lowcpu && lowcpu[0] == '1') {
        argv[i++] = (char *)"-skip_frame:v";
        argv[i++] = (char *)"noref";
    }
    argv[i++] = (char *)"-sws_flags";
    argv[i++] = (char *)"fast_bilinear";
    argv[i++] = (char *)"-vf";
    argv[i++] = osd;
    argv[i++] = (char *)"-pix_fmt";
    argv[i++] = g_pixfmt;
    argv[i++] = (char *)"-f";
    argv[i++] = (char *)"fbdev";
    argv[i++] = (char *)"/dev/fb0";
    if (g_audio) {
        argv[i++] = (char *)"-f";
        argv[i++] = (char *)"alsa";
        argv[i++] = (char *)"default";
    }
    argv[i] = NULL;

    printf("[iptv] 播放 %d/%d: %s\n", idx + 1, g_nch, g_ch[idx].name);
    fflush(stdout);
    spawn_ffmpeg(argv);
}

/* 用 ffmpeg 的 lavfi 生成一张静态频道列表画面, 覆盖到 framebuffer */
static void show_menu(void)
{
    stop_play();
    g_menu = 1;

    char vf[4096];
    int n = snprintf(vf, sizeof(vf),
                     "drawtext=fontfile=%s:text='频道列表  (点击选择 / 左右滑切换 / 中间键返回)':"
                     "x=30:y=24:fontsize=28:fontcolor=yellow", g_font);

    int row_h = (g_h - 90) / (g_nch > 12 ? 12 : g_nch);
    if (row_h > 44)
        row_h = 44;
    for (int i = 0; i < g_nch && i < 12; i++) {
        char text[512], esc[512];
        snprintf(text, sizeof(text), "%2d  %s", i + 1, g_ch[i].name);
        esc_text(esc, sizeof(esc), text);
        n += snprintf(vf + n, sizeof(vf) - n,
                      ",drawtext=fontfile=%s:text='%s':x=60:y=%d:fontsize=%d:fontcolor=%s",
                      g_font, esc, 80 + i * row_h,
                      row_h > 30 ? 28 : 22,
                      i == g_cur ? "green" : "white");
        if (n >= (int)sizeof(vf) - 200)
            break;
    }

    char *argv[32];
    int i = 0;
    argv[i++] = (char *)"ffmpeg";
    argv[i++] = (char *)"-nostdin";
    argv[i++] = (char *)"-loglevel";
    argv[i++] = (char *)"error";
    argv[i++] = (char *)"-f";
    argv[i++] = (char *)"lavfi";
    argv[i++] = (char *)"-i";
    argv[i++] = g_bg;
    argv[i++] = (char *)"-vf";
    argv[i++] = vf;
    argv[i++] = (char *)"-frames:v";
    argv[i++] = (char *)"1";
    argv[i++] = (char *)"-pix_fmt";
    argv[i++] = g_pixfmt;
    argv[i++] = (char *)"-f";
    argv[i++] = (char *)"fbdev";
    argv[i++] = (char *)"/dev/fb0";
    argv[i] = NULL;

    printf("[iptv] 显示频道列表\n");
    fflush(stdout);
    spawn_ffmpeg(argv);
    /* 等这张静态图画完, 否则后面的输入可能把它打断 */
    if (g_pid > 0) {
        int st;
        waitpid(g_pid, &st, 0);
        g_pid = -1;
    }
}

/*
 * 频道表为空时不要只留一片黑屏, 在屏幕上说明怎么加频道。
 * 跟菜单一样用 lavfi 画一张静态图。
 */
static void show_notice(const char *l1, const char *l2, const char *l3)
{
    char vf[2048], e1[512], e2[512], e3[512];
    esc_text(e1, sizeof(e1), l1);
    esc_text(e2, sizeof(e2), l2);
    esc_text(e3, sizeof(e3), l3);
    snprintf(vf, sizeof(vf),
             "drawtext=fontfile=%s:text='%s':x=60:y=170:fontsize=36:fontcolor=yellow,"
             "drawtext=fontfile=%s:text='%s':x=60:y=250:fontsize=24:fontcolor=white,"
             "drawtext=fontfile=%s:text='%s':x=60:y=300:fontsize=24:fontcolor=white",
             g_font, e1, g_font, e2, g_font, e3);

    char *argv[32];
    int i = 0;
    argv[i++] = (char *)"ffmpeg";
    argv[i++] = (char *)"-nostdin";
    argv[i++] = (char *)"-loglevel";
    argv[i++] = (char *)"error";
    argv[i++] = (char *)"-f";
    argv[i++] = (char *)"lavfi";
    argv[i++] = (char *)"-i";
    argv[i++] = g_bg;
    argv[i++] = (char *)"-vf";
    argv[i++] = vf;
    argv[i++] = (char *)"-frames:v";
    argv[i++] = (char *)"1";
    argv[i++] = (char *)"-pix_fmt";
    argv[i++] = g_pixfmt;
    argv[i++] = (char *)"-f";
    argv[i++] = (char *)"fbdev";
    argv[i++] = (char *)"/dev/fb0";
    argv[i] = NULL;

    spawn_ffmpeg(argv);
    if (g_pid > 0) {
        int st;
        waitpid(g_pid, &st, 0);
        g_pid = -1;
    }
}

/* ------------------------------ 开机动画 ------------------------------ */
/*
 * 机顶盒式的开机动画: 开机先放一段 Logo + 进度条, 再进电视。
 *
 * 为什么要有这一段: 不加的话, 从通电到真正出画面要等联网 + 对时(几十秒),
 * 这期间屏幕一片黑, 看起来跟死机一样。另外一层黑屏由 S95iptv 脚本
 * 在最开头画的那张"正在启动"静态图负责, 这里接它后面的班。
 *
 * 动画仍然交给 ffmpeg 生成(主控不碰像素, 还能直接复用 drawtext 的字体):
 *   - 默认: lavfi 合成 —— 标题淡入 + 进度条逐格填充 + "按任意键跳过"
 *   - 若 /etc/iptv/boot.{mp4,png,jpg} 存在(或用 IPTV_BOOT_MEDIA 指定),
 *     就播它当"开机广告", 这是最接近真实机顶盒的用法。
 */
#define BOOT_BARS 10

static int boot_media(char *dst, size_t n)
{
    static const char *cand[] = {
        "/etc/iptv/boot.mp4", "/etc/iptv/boot.png", "/etc/iptv/boot.jpg"
    };
    const char *p = getenv("IPTV_BOOT_MEDIA");

    if (p && access(p, R_OK) == 0) {
        snprintf(dst, n, "%s", p);
        return 1;
    }
    for (unsigned i = 0; i < sizeof(cand) / sizeof(cand[0]); i++) {
        if (access(cand[i], R_OK) == 0) {
            snprintf(dst, n, "%s", cand[i]);
            return 1;
        }
    }
    return 0;
}

static void show_boot(int tfd, int kfd)
{
    const char *e = getenv("IPTV_BOOT_SEC");
    int sec = e ? atoi(e) : 5;
    if (sec < 1)
        sec = 1;
    if (sec > 30)
        sec = 30;

    char media[256];
    int has_media = boot_media(media, sizeof(media));

    char vf[4096];
    int n;

    if (has_media) {
        /* 自定义广告: 缩放到屏幕就行, 底下压一行跳过提示 */
        n = snprintf(vf, sizeof(vf),
                     "scale=%d:%d:force_original_aspect_ratio=decrease,"
                     "pad=%d:%d:(ow-iw)/2:(oh-ih)/2:color=0x0a1a3a,"
                     "drawtext=fontfile=%s:text='按任意键跳过':x=(w-text_w)/2:y=%d:"
                     "fontsize=22:fontcolor=gray",
                     g_w, g_h, g_w, g_h, g_font, g_h - 50);
    } else {
        /*
         * 默认动画。注意 -vf 里凡是表达式出现的逗号都要写成 \, 否则会被
         * ffmpeg 当成滤镜分隔符。
         */
        n = snprintf(vf, sizeof(vf),
                     "drawtext=fontfile=%s:text='100ASK  网络电视':"
                     "x=(w-text_w)/2:y=%d:fontsize=52:fontcolor=white:"
                     "alpha='if(lt(t\\,0.8)\\,t/0.8\\,1)'",
                     g_font, g_h / 3);

        n += snprintf(vf + n, sizeof(vf) - n,
                      ",drawtext=fontfile=%s:text='正在启动 IPTV 服务':"
                      "x=(w-text_w)/2:y=%d:fontsize=26:fontcolor=0x88ccff:"
                      "enable='gte(t\\,0.6)'",
                      g_font, g_h / 3 + 84);

        /* 进度条: 10 格, 每格到点才显示, 看起来就是一格格填充 */
        int bar_w = (g_w - 240) / BOOT_BARS;
        int bar_y = g_h * 2 / 3;
        for (int b = 0; b < BOOT_BARS; b++) {
            n += snprintf(vf + n, sizeof(vf) - n,
                          ",drawbox=x=%d:y=%d:w=%d:h=10:color=0x33cc66:t=fill:"
                          "enable='gte(t\\,%.2f)'",
                          120 + b * bar_w, bar_y, bar_w - 6,
                          (double)sec * (b + 1) / BOOT_BARS);
        }

        n += snprintf(vf + n, sizeof(vf) - n,
                      ",drawtext=fontfile=%s:text='按任意键跳过':"
                      "x=(w-text_w)/2:y=%d:fontsize=22:fontcolor=gray",
                      g_font, g_h - 50);
    }

    char src[256];
    char *argv[64];
    int i = 0;
    argv[i++] = (char *)"ffmpeg";
    argv[i++] = (char *)"-nostdin";
    argv[i++] = (char *)"-loglevel";
    argv[i++] = (char *)"error";
    if (has_media) {
        /* 图片要 -loop 1 才能持续显示, 视频不用 */
        if (strstr(media, ".png") || strstr(media, ".jpg")) {
            argv[i++] = (char *)"-loop";
            argv[i++] = (char *)"1";
        }
        argv[i++] = (char *)"-i";
        argv[i++] = media;
        argv[i++] = (char *)"-t";
        snprintf(src, sizeof(src), "%d", sec);
        argv[i++] = src;
        argv[i++] = (char *)"-an";
    } else {
        argv[i++] = (char *)"-f";
        argv[i++] = (char *)"lavfi";
        snprintf(src, sizeof(src), "color=c=0x0a1a3a:s=%dx%d:d=%d:r=15", g_w, g_h, sec);
        argv[i++] = (char *)"-i";
        argv[i++] = src;
    }
    argv[i++] = (char *)"-vf";
    argv[i++] = vf;
    argv[i++] = (char *)"-pix_fmt";
    argv[i++] = g_pixfmt;
    argv[i++] = (char *)"-f";
    argv[i++] = (char *)"fbdev";
    argv[i++] = (char *)"/dev/fb0";
    argv[i] = NULL;

    printf("[iptv] 开机动画 %d 秒%s\n", sec, has_media ? " (自定义广告)" : "");
    fflush(stdout);
    keep_screen_on();
    spawn_ffmpeg(argv);

    /*
     * 等动画播完。期间监听输入: 有触摸/按键就跳过 ——
     * 不想每次调试都被迫看满 5 秒。
     */
    time_t start = time(NULL);
    while (g_pid > 0 && !g_quit) {
        struct pollfd pf[2];
        int cnt = 0;
        if (tfd >= 0) { pf[cnt].fd = tfd; pf[cnt].events = POLLIN; cnt++; }
        if (kfd >= 0) { pf[cnt].fd = kfd; pf[cnt].events = POLLIN; cnt++; }

        /* 信号也能跳过(方便调试: kill -USR1 就不用干等 5 秒) */
        if (g_req) {
            g_req = 0;
            printf("[iptv] 开机动画被跳过(信号)\n");
            fflush(stdout);
            break;
        }

        int r = cnt ? poll(pf, cnt, 200) : -1;
        if (r > 0) {
            /*
             * 关键: 要把这批事件读掉丢掉。不然它们会留在队列里,
             * 动画一结束进入播放时被立刻当成"换台"处理。
             */
            for (int j = 0; j < cnt; j++) {
                if (pf[j].revents & POLLIN) {
                    struct input_event ev[32];
                    (void)!read(pf[j].fd, ev, sizeof(ev));
                }
            }
            printf("[iptv] 开机动画被跳过\n");
            fflush(stdout);
            break;
        }
        if (!alive())                       /* 播完了 */
            break;
        if (time(NULL) - start > sec + 3)   /* 兜底, 免得 ffmpeg 卡住不放手 */
            break;
    }
    stop_play();
}

/* ------------------------------- 输入处理 ------------------------------ */
static int open_evdev(const char *path)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        fprintf(stderr, "[iptv] 打开 %s 失败: %s\n", path, strerror(errno));
    return fd;
}

static void on_touch(int x, int y)
{
    printf("[iptv] 触摸 (%d,%d) %s\n", x, y, g_menu ? "[菜单]" : "[播放]");
    fflush(stdout);
    if (g_menu) {
        int row_h = (g_h - 90) / (g_nch > 12 ? 12 : g_nch);
        if (row_h > 44)
            row_h = 44;
        int idx = (y - 70) / row_h;
        if (idx >= 0 && idx < g_nch && idx < 12)
            play(idx);
        return;
    }
    if (x < g_w / 3)
        play(g_cur - 1);
    else if (x > g_w * 2 / 3)
        play(g_cur + 1);
    else
        show_menu();
}

int main(int argc, char **argv)
{
    /*
     * --setfb: 只按 IPTV_SCREEN 策略把 fb0 的分辨率改一遍就退出, 不启动播放。
     * 给开机脚本用 —— 在画开机图之前先改, 否则那张"正在启动"也会画在
     * 被 HDMI 顶掉的分辨率上(LCD 上同样是歪的)。
     */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--setfb") == 0) {
            const fb_mode *m = screen_mode();
            int r = fb_set_mode(m);

            printf("[iptv] --setfb: %s -> %s\n", m->name,
                   r > 0 ? "已切换" : (r == 0 ? "无需切换" : "失败"));
            return r < 0 ? 1 : 0;
        }
    }

    int opt;
    while ((opt = getopt(argc, argv, "c:t:k:f:w:H:a")) != -1) {
        switch (opt) {
        case 'c': g_conf = optarg;  break;
        case 't': g_touch = optarg; break;
        case 'k': g_key = optarg;   break;
        case 'f': g_font = optarg;  break;
        case 'w': g_w = atoi(optarg); break;
        case 'H': g_h = atoi(optarg); break;
        case 'a': g_audio = 1;      break;
        default:
            fprintf(stderr, "用法: %s [-c 频道文件] [-t 触摸设备] [-k 按键设备] "
                            "[-f 字体] [-w 宽] [-H 高] [-a 开音频]\n", argv[0]);
            return 1;
        }
    }

    /*
     * 先把分辨率按 IPTV_SCREEN 策略定下来, 再探测屏幕参数。
     * 插着 HDMI 开机时内核已经把 fb0 改成 1280x720 了(驱动按电视 EDID 改的),
     * 不先改回来的话 LCD 上就是左边一条黑边 —— 面板是固定 1024x600 不会缩放。
     */
    {
        const fb_mode *m = screen_mode();
        int r = fb_set_mode(m);

        if (r > 0)
            printf("[iptv] 屏幕模式 -> %s\n", m->name);
        else if (r < 0)
            printf("[iptv] 屏幕模式设置失败, 沿用当前分辨率\n");
    }

    detect_fb();

    {
        int c = hdmi_cable();
        if (c >= 0) {
            printf("[iptv] HDMI %s\n", c ? "已连接(与 LCD 同源, 同步显示 fb0 内容)"
                                        : "未连接");
            fflush(stdout);
        }
    }

    /*
     * 频道表是空的(刚装好还没加频道)时不要直接退出留个黑屏:
     * 屏幕上给出提示, 然后每隔几秒重读一次频道表,
     * 这样用 add_channel.sh 加完频道不用重启程序就能自动开始播。
     */
    load_channels(g_conf);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGUSR1, on_signal);
    signal(SIGUSR2, on_signal);
    signal(SIGHUP, on_signal);
    signal(SIGCHLD, SIG_IGN);

    /*
     * 频道表是空的(刚装好还没加频道)时不要退出留个黑屏:
     * 屏幕上给提示, 然后定时重读频道表 —— 用 add_channel.sh 加完频道
     * 不用重启程序, 最多 20 秒后自动开始播。
     */
    while (g_nch == 0 && !g_quit) {
        printf("[iptv] 频道表为空, 等待添加频道 ...\n");
        fflush(stdout);
        show_notice("频道表是空的",
                    "本机执行: ./tools/add_channel.sh 频道名 地址",
                    "或直接编辑板上的 /etc/iptv/channels.conf");
        for (int i = 0; i < 20 && !g_quit; i++)
            sleep(1);
        if (!g_quit)
            load_channels(g_conf);
    }
    if (g_quit)
        return 0;
    printf("[iptv] 载入 %d 个频道: %s\n", g_nch, g_conf);

    int tfd = open_evdev(g_touch);
    int kfd = open_evdev(g_key);

    /* 触摸坐标的原始量程, 不同屏不一样, 从驱动里读 */
    int xmin = 0, xmax = 1024, ymin = 0, ymax = 600;
    if (tfd >= 0) {
        struct input_absinfo ax, ay;
        if (ioctl(tfd, EVIOCGABS(ABS_X), &ax) == 0 && ax.maximum > ax.minimum) {
            xmin = ax.minimum;
            xmax = ax.maximum;
        }
        if (ioctl(tfd, EVIOCGABS(ABS_Y), &ay) == 0 && ay.maximum > ay.minimum) {
            ymin = ay.minimum;
            ymax = ay.maximum;
        }
        printf("[iptv] 触摸屏量程 x[%d,%d] y[%d,%d]\n", xmin, xmax, ymin, ymax);
    }

    show_boot(tfd, kfd);   /* 机顶盒式开机动画, 可按键跳过 */
    play(0);

    int retry = 0;
    struct pollfd pf[2];
    int last_x = -1, last_y = -1;

    time_t last_keep = 0;
    time_t last_hdmi = 0;
    int    last_cable = -1;

    while (!g_quit) {
        /* 每 10 秒维护一次防黑屏(内核的 consoleblank 可能被重新设回去) */
        time_t now = time(NULL);
        if (now - last_keep >= 10) {
            last_keep = now;
            keep_screen_on();
        }

        /*
         * 每 2 秒看一眼 HDMI: 插上时内核会按电视 EDID 改 fb0 分辨率,
         * 改了就得重新探测屏幕参数并重开 ffmpeg, 否则画面错位/写不进去。
         */
        if (now - last_hdmi >= 2) {
            int c, w = 0, h = 0;

            last_hdmi = now;
            c = hdmi_cable();
            if (c != last_cable) {
                last_cable = c;
                if (c >= 0) {
                    printf("[iptv] HDMI %s\n", c ? "已连接(与 LCD 同源, 同步显示 fb0)"
                                                : "已拔出");
                    fflush(stdout);
                }
            }
            fb_size_now(&w, &h);
            if (w > 0 && h > 0 && (w != g_w || h != g_h)) {
                const fb_mode *want = screen_mode();

                printf("[iptv] 屏幕变成 %dx%d (原 %dx%d)", w, h, g_w, g_h);
                if (w != want->w || h != want->h) {
                    /*
                     * 不是我们要的模式 —— 基本就是 HDMI 驱动按电视 EDID 改的
                     * (运行中插拔也会触发)。按 IPTV_SCREEN 策略改回来。
                     */
                    if (fb_set_mode(want) > 0)
                        printf(" -> 已按策略改回 %s\n", want->name);
                    else
                        printf(" -> 改不回 %s, 只能适配当前分辨率\n", want->name);
                } else {
                    printf(" -> 重新适配\n");
                }
                fflush(stdout);
                g_w = g_h = 0;      /* detect_fb() 只在为 0 时才重新测 */
                detect_fb();
                play(g_cur);
            }
        }

        /* 外部控制: kill -USR1/USR2/HUP <pid> = 下一台/上一台/频道列表 */
        if (g_req) {
            if (g_req == 1)      play(g_cur + 1);
            else if (g_req == 2) play(g_cur - 1);
            else if (g_menu)     play(g_cur);
            else                 show_menu();
            g_req = 0;
            continue;
        }

        int n = 0;
        if (tfd >= 0) { pf[n].fd = tfd; pf[n].events = POLLIN; n++; }
        if (kfd >= 0) { pf[n].fd = kfd; pf[n].events = POLLIN; n++; }

        int r = n ? poll(pf, n, 400) : -1;
        if (r < 0 && errno != EINTR)
            usleep(300000);

        /* 触摸事件 */
        if (n && (pf[0].revents & POLLIN)) {
            struct input_event ev[32];
            ssize_t sz = read(tfd, ev, sizeof(ev));
            int cnt = sz > 0 ? (int)(sz / sizeof(struct input_event)) : 0;
            int moved = 0;
            for (int i = 0; i < cnt; i++) {
                if (ev[i].type == EV_ABS && ev[i].code == ABS_X) {
                    last_x = (ev[i].value - xmin) * g_w / (xmax - xmin + 1);
                    moved = 1;
                } else if (ev[i].type == EV_ABS && ev[i].code == ABS_Y) {
                    last_y = (ev[i].value - ymin) * g_h / (ymax - ymin + 1);
                    moved = 1;
                } else if (ev[i].type == EV_KEY && ev[i].code == BTN_TOUCH && ev[i].value == 0) {
                    /* 抬手: 用最后坐标算点击; 若几乎没移动则视为点击 */
                    if (last_x >= 0 && last_y >= 0)
                        on_touch(last_x, last_y);
                    last_x = last_y = -1;
                } else if (ev[i].type == EV_SYN && ev[i].code == SYN_REPORT && moved &&
                           last_x >= 0 && last_y >= 0) {
                    /* 有些屏不报 BTN_TOUCH, 用 SYN_REPORT 兜底 */
                    static int seen = 0;
                    if (++seen % 2 == 0) {
                        on_touch(last_x, last_y);
                        last_x = last_y = -1;
                        seen = 0;
                    }
                }
            }
        }

        /* 按键事件 */
        if (n > 1 && (pf[1].revents & POLLIN)) {
            struct input_event ev[32];
            ssize_t sz = read(kfd, ev, sizeof(ev));
            int cnt = sz > 0 ? (int)(sz / sizeof(struct input_event)) : 0;
            for (int i = 0; i < cnt; i++) {
                if (ev[i].type != EV_KEY || ev[i].value != 1)
                    continue;
                switch (ev[i].code) {
                /* 板载 gpio-keys 的两个按键(KEY bitmap 0xc = KEY_1 + KEY_2) */
                case KEY_2:
                case KEY_UP:    case KEY_CHANNELUP:   case KEY_VOLUMEUP:
                case KEY_RIGHT: play(g_cur + 1); break;
                case KEY_1:
                case KEY_DOWN:  case KEY_CHANNELDOWN: case KEY_VOLUMEDOWN:
                case KEY_LEFT:  play(g_cur - 1); break;
                case KEY_ENTER: case KEY_OK: case KEY_SELECT: case KEY_MENU:
                    if (g_menu) play(g_cur); else show_menu();
                    break;
                default: break;
                }
            }
        }

        /* 直播断流/ffmpeg 异常退出 -> 自动重连 */
        if (!g_menu && !alive()) {
            if (g_pid > 0) {
                int st;
                waitpid(g_pid, &st, WNOHANG);
                g_pid = -1;
            }
            if (++retry <= 3) {
                printf("[iptv] 播放中断, %d 秒后重连(%d/3)\n", retry * 2, retry);
                fflush(stdout);
                sleep((unsigned)(retry * 2));
                play(g_cur);
            } else if (retry == 4) {
                printf("[iptv] 连续重连失败, 停在本频道\n");
                fflush(stdout);
                retry = 5;      /* 不再自动重试, 等用户换台 */
            }
        } else if (alive()) {
            retry = 0;
        }
    }

    stop_play();
    if (tfd >= 0) close(tfd);
    if (kfd >= 0) close(kfd);
    printf("[iptv] 退出\n");
    return 0;
}
