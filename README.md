# 100ask_imx6ull macOS 开发环境

Host: macOS (Apple Silicon, arm64) · 目标: 100ask_imx6ull (i.MX6ULL, Cortex-A7, ARMv7-A)
搭建日期: 2026-10-05

## 1. 实测到的板子参数

以下数据来自板子实物（串口 `uname`/`cat` 回显，可复现：`./cmd.sh "uname -a"`）：

| 项目 | 实测值 | 来源 |
|---|---|---|
| SoC | `Freescale i.MX6 ULL 14x14 EVK Board` | `cat /proc/device-tree/model` |
| CPU | ARMv7 Processor rev 5 (v7l), Cortex-A7, 单核 528/696 MHz | `cat /proc/cpuinfo` |
| 内存 | 500940 kB (~489 MB) | `free -m` |
| 内核 | `Linux 4.9.88 #5 SMP PREEMPT Fri Apr 14 11:30:15 HKT 2023`（用 `gcc 9.1.0` 编译） | `uname -a` / `cat /proc/version` |
| 根文件系统 | Buildroot 2020.02 (`2020.02-gd1ee3fb`), glibc **2.30** | `cat /etc/os-release` / `ldd --version` |
| 启动参数 | `console=ttymxc0,115200 root=/dev/mmcblk1p2 rootwait rw` (eMMC) | `cat /proc/cmdline` |
| LCD | fb0 = **1024x600, 32 bpp**, line_length 4096 | `/sys/class/graphics/fb0/*` |
| 触摸屏 | `/dev/input/event1` (gt9xx/edt-ft5x06 类), 量程 x[0,1024] y[0,600] | `EVIOCGABS` |
| 存储 | `/` = 1.5G (已用 966M)，另有 mmcblk1p1/p2/p3 | `df -h` |
| 串口 | `/dev/cu.usbserial-01E74639`, 115200 8N1 | 本机实检 |
| 串口登录 | 重启后是 getty：`用户名 root，密码直接回车`（工具已自动处理） | `cat /etc/inittab` |
| WiFi | RTL8723BU (`8723bu`)，已连 SSID **602**，用 **wlan1**（wlan0 有 bug，见 5.4） | `iw dev wlan1 link` |
| 其它外设 | video0, spidev0.0/0.1, i2c-0/1, gpiochip0~5, snd(wm8960), watchdog, adbd | `ls /dev` |

板厂定制镜像：`/etc/init.d/S99myirhmi2`（米尔 HMI 应用）会跟电视抢屏幕，**已移到 `/root/disabled/`**；
`S89_network_manager` 会跟我们手动的 wpa_supplicant 抢 wlan1，**也已移到 `/root/disabled/`**。

## 2. 目录结构

```
100ask_imx6ull/
├── build.sh          # 编译 examples/ 下的例程: ./build.sh 01_hello [run]
├── monitor.sh        # 串口终端 (115200), Ctrl-] 退出
├── push.sh           # ZMODEM 下发文件到开发板 (无需网络)
├── cmd.sh            # 在开发板上执行一条命令
├── examples/
│   ├── common.mk            # 例程通用编译规则 (每个 Makefile 只需 include)
│   ├── toolchain.cmake      # CMake 用的交叉工具链描述
│   ├── 01_hello/            # 打印内核/rootfs/屏参信息
│   ├── 02_framebuffer/      # 帧缓冲直写: 彩虹动画 + 退出自动还原
│   │                        #   实测 1024x600x32: 逐像素 HSV 只有 ~5 FPS,
│   │                        #   改成 palette + 逐行 memcpy 后 ~39 FPS
│   └── 03_iptv/             # ★ 网络电视: 拉流 → ffmpeg 软解 → 直写 fb0
│                            #   main.c / channels.conf / S95iptv(开机自启动)
├── tools/
│   ├── imx.py               # 串口工具集 (probe/cmd/monitor/push/run) + 自动登录
│   ├── toolchain/           # armv7-unknown-linux-musleabihf (GCC 15.2.0)
│   └── docker/              # 备用: 内核/驱动编译容器 (见第 7 节)
├── sdk/                     # 官方源码 (第 6 节)
└── docs/                    # 官方文档 (第 6 节)
```

## 3. 快速上手

```bash
cd /Users/yangyuanxin/Desktop/tools/100ask_imx6ull

./monitor.sh                       # 串口终端 (Ctrl-] 退出)
./build.sh                         # 列出例程
./build.sh 01_hello                # 交叉编译
./build.sh 01_hello run            # 编译 + ZMODEM 下发 + 在板子上跑
./cmd.sh "uname -a"                # 单条命令
./push.sh build/01_hello/01_hello /tmp   # 传单个文件到 /tmp

# 网络电视
make -C examples/03_iptv deploy    # 编译 + 装程序/频道表/开机脚本
./cmd.sh "/etc/init.d/S95iptv start"     # 立即开始播放
```

## 4. 工具链说明

macOS 上没有官方适配的 `arm-linux-gnueabihf`，这里用的是
[messense/homebrew-macos-cross-toolchains](https://github.com/messense/homebrew-macos-cross-toolchains) v15.2.0 的
`armv7-unknown-linux-musleabihf-aarch64-darwin`（macOS 原生 arm64 构建）：

```
tools/toolchain/armv7-unknown-linux-musleabihf/bin/armv7-unknown-linux-musleabihf-gcc  (GCC 15.2.0)
编译参数: -march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -static
```

**为什么选 musl + 静态链接**：板上 glibc 是 2.30，任何新版 Linux 主机工具链（glibc 2.35+）动态链出来的程序
在上面都会报 `version GLIBC_2.xx not found`。musl 静态二进制不依赖板上的 libc，直接 `./xxx` 就能跑。

工作环境下 macOS 自带的 `arm-none-eabi-gcc`（GCC 16.1.0，来自 brew）可用于 **裸机/NoOS** 例程（`sdk/noos`）。

## 5. 网络电视 (03_iptv)

一个在板子上常驻的电视程序：拉网络流 → ffmpeg 软解 → 直接写 `/dev/fb0`，带频道列表和换台。

> 软件架构、数据流、进程模型和踩坑清单见 **[examples/03_iptv/ARCHITECTURE.md](examples/03_iptv/ARCHITECTURE.md)**（含 Mermaid 流程图）。

### 5.1 怎么用

屏幕上的操作（1024x600）：

| 操作 | 效果 |
|---|---|
| 点屏幕**上 1/3** | 上一台 |
| 点屏幕**下 1/3** | 下一台 |
| 点屏幕**中间 1/3** | 弹出频道列表，再点某一行选台 |
| 板载按键 **KEY1 / KEY2** | 上一台 / 下一台 |

换台后画面顶部会叠 8 秒频道名水印，然后自动消失。

命令行也能控制（方便远程/脚本）：

```bash
P=$(./cmd.sh "ps | grep '[0]3_iptv' | awk '{print \$1}'")
./cmd.sh "kill -USR1 $P"   # 下一台
./cmd.sh "kill -USR2 $P"   # 上一台
./cmd.sh "kill -HUP  $P"   # 显示/收起频道列表
```

### 5.1.1 开机动画（机顶盒式）

开机是**两段式**，从通电起屏幕就有画面，不会黑屏干等：

| 阶段 | 画面 | 时长 |
|---|---|---|
| 上电 → 联网对时完成 | 静态"正在启动…" | 联网通常几十秒 |
| 播放器启动 → 进电视 | Logo + 进度条动画 | 默认 5 秒，可跳过 |

跳过方式：触摸屏幕、按键，或 `kill -USR1 <pid>`。

想换成真正的"开机广告"，放一个视频/图片即可（不用改代码）：

```bash
# 放到板子 /etc/iptv/boot.mp4（或 boot.png / boot.jpg），下次开机自动播它
IPTV_BOOT_SEC=8    # 动画时长，默认 5 秒
IPTV_BOOT_MEDIA=/etc/iptv/boot.mp4   # 自定义广告路径
```

### 5.2 添加频道（频道表默认是空的）

频道表在板上 `/etc/iptv/channels.conf`（本地 `examples/03_iptv/channels.conf`），一行一个：

```
频道名|地址
```

**默认已配置 3 个频道**（开机播第一个）：

| # | 频道 | 变体 | 实测 |
|---|---|---|---|
| 1 | CCTV+ 直播 | 640x360 `index_4` | **9.7 FPS** |
| 2 | CGTN 财经 | 640x360 `index_4` | 6.7 FPS |
| 3 | CCTV+ 高清 | 1280x720 `index_2` | 很卡（对比用）|
频道表若为空，板子会在屏幕上显示"频道表是空的"并等待，加完频道**不用重启程序**，最多 20 秒自动开播。

加频道用一条命令（它会当场在板子上试播 12 秒，并把实际帧数报给你）：

```bash
./tools/add_channel.sh "央视一套" "rtp://239.3.1.xxx:8000"
./tools/add_channel.sh "某个台" "https://.../live.m3u8"
```

支持 http / https / m3u8(HLS) / rtmp / rtsp / rtp 组播等 ffmpeg 能解的地址。

**关于频道来源（重要）**：网上流传的 "CCTV/卫视 m3u8 聚合列表" 基本都是未授权转发的盗链源，
这类地址本项目不提供、也不建议放进来。请用你自己有接收权限的源：

| 来源 | 说明 |
|---|---|
| 家里运营商 IPTV 的组播地址 | 你已付费的业务，**最稳也最合规**：从自家 IPTV 盒子抓包，或查运营商公开的频道列表文件，得到 `rtp://239.x.x.x:xxxx` |
| 电视台自己对外免费公开的直播地址 | 电视台官网/官方 CDN 上免费开放的那部分 |
| 你从官方直播页面自己复制的地址 | 你自己获取的，自己负责 |

> 实测提醒：电视台自己公开的那批国际流（DW / NASA TV / NHK World）在普通家庭宽带下
> **基本连不上**（本机 curl 同样 000/404），别浪费时间找它们。中文电视走运营商 IPTV 组播最现实。

**卡的话**：启动时带 `IPTV_LOWCPU=1`（只解关键帧，画面会有跳动但明显流畅），或换更低码率的源。

### 5.3 性能（i.MX6ULL 没有 VPU，全靠 CPU 软解）

| 源分辨率 | 实测帧率 |
|---|---|
| 960x400 HLS | ~10 FPS |
| 720p | 3~5 FPS |

所以：**优先选 360p/480p 低码率源**；程序默认 `-an` 关掉音频（音频解码会再吃掉一截 CPU，
要声音用 `-a` 打开）。画面用 `force_original_aspect_ratio=decrease + pad` 保持比例并居中。

### 5.4 这块板上踩到的坑（都已绕过，改脚本前先看这里）

1. **wlan0 发不出数据帧**：`8723bu` 驱动下 wlan0 的 `carrier` 恒为 0，ping/DHCP 全丢，
   内核日志反复 `nolinked power save`。解法：`rmmod 8723bu` → `modprobe 8723bu rtw_power_mgnt=0 rtw_ips_mode=0` → **用 wlan1**。
2. **NetworkManager 抢接口**：开着它时 wlan1 一直 `Not connected`。解法：停掉并移出 `/etc/init.d/`。
3. **没有 RTC 电池**：每次上电时间回到 1970，https 报 `certificate is not yet valid`。
   解法：联网后对时。注意 busybox 的 `ntpd -q -n` 在这块板上**只监听不同步**，要用 `ntpdate -u -t 5 <server>`。
4. **★ 别阻塞 init**：busybox init 串行跑 `/etc/init.d/S*`，而串口上的 getty 要等 rcS 跑完才 respawn。
   之前把联网/对时写在前台，ntpd 没网时卡住 → init 被堵死 → **串口再也要不到 shell**（只有回显、命令不执行）。
   解法：`S95iptv` 的整个流程必须 `( ... ) &` 放后台，`start` 立即返回。
5. **https 需要 CA**：已装 `ca-certificates.crt`，运行时要带 `SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt`。
6. **低延迟参数会打挂起播**：`nobuffer` / 极小 `analyzeduration` 对 HLS 和 http mp4 都会导致
   `Error when loading first segment` 或直接退出。现在只对 rtmp/rtsp/udp/rtp 启用。
   其余源统一用 `-probesize 10M -max_delay 800000`（就是 ffplay 的默认值）。
7. **★ fbdev 的像素格式必须跟 fb0 位深对上**。写死 `bgra` 时 ffmpeg 报
   `Pixel format bgra is not supported, use rgb565le` + `av_interleaved_write_frame(): Invalid argument`，
   **进程照样在跑、日志也正常，但画面根本没写进屏幕** —— 非常隐蔽，只能靠
   `md5sum /dev/fb0` 连采两次看变没变来确认。而且这块板的 fb0 参数**每次重启都可能变**
   （实测出现过 `1024x600/32bpp` 和 `1280x720/16bpp` 两种）。
   现在程序启动读 `/sys/class/graphics/fb0/virtual_size` + `bits_per_pixel` 自动决定
   输出尺寸和 `pix_fmt`（16bpp→rgb565le，32bpp→bgra）。
8. **HLS 会默认挑最高码率那个变体**。比如 CCTV+ 那个 master.m3u8 里最高是 1080p/4.6Mbps，
   单核软解实测 **0 帧**；必须直接写低码率子地址（`index_4.m3u8` = 640x360 约 9.7 FPS）。
9. **★ 看一会儿自己黑屏（"自动休眠"）**：这是内核的 console blanking，
   默认 `consoleblank=600`（console 空闲 10 分钟就 blank 掉 framebuffer）。
   背光其实是好的（实测 `bl_power=0`、`brightness=8/8`），所以别去调背光。
   注意 `/sys/module/kernel/parameters/consoleblank` 在这块板是**只读**的
   （`-r--r--r--`，写它只会 `Permission denied`），sysfs 这条路走不通。
   实际用了三层：
   - **永久**：`fw_setenv mmcargs 'setenv bootargs console=${console},${baudrate} root=${mmcroot} consoleblank=0'`
     （重启后 `/proc/cmdline` 里能看到，这层是根治）
   - **开机脚本**：`printf '\033[9;0]' > /dev/tty1` + `echo 0 > /sys/class/graphics/fb0/blank`
     （`ESC[9;0]` 等价于 `setterm -blank 0`，tty 这条路是可写的）
   - **播放器兜底**：主循环每 10 秒重写一遍上面两个（防止被 tty 切换/getty 重设回去）
10. **★ 画面上有个光标一直在闪**：fbcon 和 ffmpeg 在抢同一块显存，光标是 fbcon 画上去的，
    （printk / getty 的登录提示同样会直接叠在视频上）。所以要
    `echo 0 > /sys/class/graphics/fbcon/cursor_blink`，并把 fbcon 从屏幕解绑：
    `echo 0 > /sys/class/vtconsole/**vtcon1**/bind`。
    ⚠️ 是 **vtcon1** 不是 vtcon0 —— 实测 `vtcon0 = "(S) dummy device"`，
    `vtcon1 = "(M) frame buffer device"` 才是画到屏幕的那个。
    解绑后内核会打印 `Console: switching to colour dummy device 80x30`（正常现象），
    console 输出照常在串口 `ttymxc0`，不影响调试。

恢复方法（万一再被 init 堵死）：重启时在 U-Boot 倒计时敲回车停下，然后

```
setenv mmcargs 'setenv bootargs console=${console},${baudrate} root=${mmcroot} init=/bin/sh'
run bootcmd
```

这样直接拿到 root shell（不跑任何 init 脚本），修完 `sync; reboot -f` 即可。

### 5.5 开机自动播放

`examples/03_iptv/S95iptv`（装在板子 `/etc/init.d/S95iptv`）开机自动：
关 HMI → 重载 WiFi 驱动 → 连 wlan1 → DHCP → ntpdate 对时 → 启动播放器。
全程后台运行，网络失败也不会卡住启动。

```bash
./cmd.sh "/etc/init.d/S95iptv start|stop|restart"
./cmd.sh "cat /var/log/iptv-init.log"    # 联网/对时过程
./cmd.sh "tail -f /var/log/iptv.log"     # 播放器日志
```

已实测：断电重启后约 60 秒自动开始播放频道 1，串口 shell 正常可用。

## 6. 已下载的资料

全部来自 GitHub 组织 `100askTeam`，`--depth 1` 浅克隆，共约 5.2 GB：

| 路径 | 仓库 | HEAD commit | 日期 | 用途 |
|---|---|---|---|---|
| `sdk/linux-4.9.88` | [100askTeam/imx-linux4.9.88](https://github.com/100askTeam/imx-linux4.9.88) | `0123644c94d7f25082e6af7d56bc1c329edb8273` | 2020-03-06 | **与你板上的 4.9.88 对应**，含 `arch/arm/configs/100ask_imx6ull_defconfig` |
| `sdk/qemu_imx6ull_kernel` | [100askTeam/qemu_imx6ull_kernel](https://github.com/100askTeam/qemu_imx6ull_kernel) | `8f6c88def04f5c8bf44a895fa8481e7c02bb53bf` | 2019-11-27 | qemu 仿真版内核（可选，用不到可删，约 962 MB） |
| `sdk/noos` | [100askTeam/100ask_imx6ull_NoosProgramProject](https://github.com/100askTeam/100ask_imx6ull_NoosProgramProject) | `bd607452ea488d906cec472ad1a4569a30026ab1` | 2020-04-17 | 裸机例程：GPIO/UART/LCD/I2C/SPI/定时器/中断/各传感器模块 |
| `docs/01_all_series_quickstart` | [100askTeam/01_all_series_quickstart](https://github.com/100askTeam/01_all_series_quickstart) | `dffcdc718a4e8340c8331a1e2c83efcc3d14b309` | 2020-05-29 | 全系列快速入门手册 + PDF（含《嵌入式Linux应用开发完全手册第2版》） |
| `docs/Linux-doc_and_source_for_drivers` | [100askTeam/Linux-doc_and_source_for_drivers](https://github.com/100askTeam/Linux-doc_and_source_for_drivers) | `0ebc722754315e783de1b6db17539936e0badddd` | 2022-03-14 | 驱动实验源码 + 文档（约 2.1 GB） |

`100askTeam` 组织下确认**没有** `100ask_imx6ull-uboot-imx4.1.15` 这类仓库；u-boot 仓库只有
`uboot-imx`（2020-12，156 MB）和空的 `imx-uboot2017.03`，本次没下载 —— 需要的时候再说。

## 7. 已知限制 / 待办

- **内核编译、.ko 驱动模块**：macOS 上编不了 4.9.88（缺 Linux 头文件 + GCC 15 太新）。
  预留了 `tools/docker/`（Ubuntu 22.04 + `gcc-arm-linux-gnueabihf`）。
  ⚠️ **这块还没验证** —— 你机器上的 Docker Desktop 是关着的（`Cannot connect to the Docker daemon`），
  镜像没构建过。要用的时候先启动 Docker Desktop。
- **下发方式**：现在板子有 IP（192.168.1.64），sshd/telnetd 都在跑，可以 `ssh root@192.168.1.64`
  或 `telnet` 登录，scp 也能用；串口 ZMODEM（70–150 KB/s）仍然是无网时的保底手段。
- **eMMC 烧写**：需要 NXP 的 `uuu` 工具，本次没装；当前系统跑得好就不用动它。

## 8. 开发新例程

在 `examples/` 下新建目录，写 `main.c` 和一个三行 Makefile：

```makefile
TARGET := my_demo
SRCS   := main.c
include ../common.mk
```

然后 `./build.sh my_demo run`。依赖管理都由 `examples/common.mk` 处理
（自动通配符搜集源码、`-MMD` 头文件依赖、`build/<TARGET>/` 输出、`strip`、大小打印）。
