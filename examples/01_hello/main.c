#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>

static void head(const char *title)
{
    printf("\n===== %s =====\n", title);
}

/* 读取单行文本文件并回显 */
static void show_line(const char *path)
{
    FILE *fp = fopen(path, "r");
    char buf[256];

    if (!fp) {
        printf("  (%s 不存在或不可读)\n", path);
        return;
    }
    while (fgets(buf, sizeof(buf), fp))
        if (strchr(buf, '\n'))
            break;
    buf[strcspn(buf, "\r\n")] = '\0';
    printf("  %s\n", buf);
    fclose(fp);
}

int main(void)
{
    struct utsname u;

    uname(&u);
    head("Hello 100ask_imx6ull");
    printf("  交叉编译工具链 : armv7-unknown-linux-musleabihf (musl, 静态链接)\n");
    printf("  本地时间是     : %s\n", __DATE__ " " __TIME__);

    head("内核信息");
    printf("  %s %s (%s)\n", u.sysname, u.release, u.machine);

    head("系统版本");
    show_line("/etc/os-release");
    show_line("/proc/device-tree/model");

    head("显示与内存");
    show_line("/sys/class/graphics/fb0/virtual_size");
    show_line("/sys/class/graphics/fb0/bits_per_pixel");
    show_line("/proc/meminfo");

    printf("\n  => 例程跑起来了, 可以在 examples/ 下继续扩展.\n");
    return 0;
}
