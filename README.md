# 100ask_imx6ull macOS 开发环境

Host: macOS (Apple Silicon, arm64) · 目标: 100ask_imx6ull (i.MX6ULL, Cortex-A7, ARMv7-A)
搭建日期: 2026-10-05

## 1. 实测到的板子参数

以下数据来自板子实物（串口 `uname`/`cat` 回显，可复现：`./cmd.sh "uname -a"`）：

| 项目 | 实测值 | 来源 |
|---|---|---|
| SoC | `Freescale i.MX6 ULL 14x14 EVK Board` | `cat /proc/device-tree/model` |
| CPU | ARMv7 Processor rev 5 (v7l), Cortex-A7, 单核 | `cat /proc/cpuinfo` |
| 内存 | 500940 kB (~489 MB) | `free -m` |
| 内核 | `Linux 4.9.88 #5 SMP PREEMPT Fri Apr 14 11:30:15 HKT 2023`（用 `gcc 9.1.0` 编译） | `uname -a` / `cat /proc/version` |
| 根文件系统 | Buildroot 2020.02 (`2020.02-gd1ee3fb`), glibc **2.30**, busybox 无 | `cat /etc/os-release` / `ldd --version` |
| 启动参数 | `console=ttymxc0,115200 root=/dev/mmcblk1p2 rootwait rw` (eMMC) | `cat /proc/cmdline` |
| LCD | fb0 = **1024x600, 32 bpp**, line_length 4096 | `/sys/class/graphics/fb0/*` |
| 存储 | `/` = 1.5G (已用 966M)，另有 mmcblk1p1/p2/p3 | `df -h` |
| 串口 | `/dev/cu.usbserial-01E74639`, 115200 8N1，回车即 root shell | 本机实检 |
| 其它外设 | video0, spidev0.0/0.1, i2c-0/1, gpiochip0~5, snd, watchdog, adbd, QtWebProcess | `ls /dev` |
| 网络 | eth0 / eth1 / wlan0 / wlan1 均未分配 IP（未连线） | `ifconfig` |

注意 `/etc/init.d/` 里有 `S99myirhmi2`，说明当前 eMMC 里跑的是**板厂定制镜像**（100ask Buildroot + 米尔 HMI 应用），
而不是纯净的 100ask 出厂系统。

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
│   └── 02_framebuffer/      # 帧缓冲直写: 彩虹动画 + 退出自动还原
│                            #   实测 1024x600x32: 逐像素 HSV 只有 ~5 FPS,
│                            #   改成 palette + 逐行 memcpy 后 ~39 FPS
├── tools/
│   ├── imx.py               # 串口工具集 (probe/cmd/monitor/push/run)
│   ├── toolchain/           # armv7-unknown-linux-musleabihf (GCC 15.2.0)
│   └── docker/              # 备用: 内核/驱动编译容器 (见第 6 节)
├── sdk/                     # 官方源码 (第 5 节)
└── docs/                    # 官方文档 (第 5 节)
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
```

`./build.sh <例程名> run` 的完整链路已实测通过，示例输出：

```
[100ask] sz 退出码 0
[100ask] 上传成功:
-rwxr-xr-x 1 root root 33748 Oct  5  2026 /root/01_hello
----- 板子输出 -----
===== Hello 100ask_imx6ull =====
  交叉编译工具链 : armv7-unknown-linux-musleabihf (musl, 静态链接)
  Linux 4.9.88 (armv7l)
  NAME=Buildroot
  Freescale i.MX6 ULL 14x14 EVK Board
  1024,600 / 32
  MemTotal:         500940 kB
```

## 4. 工具链说明

macOS 上没有官方适配的 `arm-linux-gnueabihf`，这里用的是
[messense/homebrew-macos-cross-toolchains](https://github.com/messense/homebrew-macos-cross-toolchains) v15.2.0 的
`armv7-unknown-linux-musleabihf-aarch64-darwin`（直接用 Rosetta 无关的原生 arm64 构建）：

```
tools/toolchain/armv7-unknown-linux-musleabihf/bin/armv7-unknown-linux-musleabihf-gcc  (GCC 15.2.0)
编译参数: -march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -static
```

**为什么选 musl + 静态链接**：板上 glibc 是 2.30，任何新版 Linux 主机工具链（glibc 2.35+）动态链出来的程序
在上面都会报 `version GLIBC_2.xx not found`。musl 静态二进制不依赖板上的 libc，直接 `./xxx` 就能跑，
这是 macOS 宿主下最省事、已验证可用的路径（01_hello / 02_framebuffer 均已实跑通过）。

工作环境下 macOS 自带的 `arm-none-eabi-gcc`（GCC 16.1.0，来自 brew）可用于 **裸机/NoOS** 例程（`sdk/noos`）。

## 5. 已下载的资料

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

## 6. 已知限制 / 待办

- **内核编译、.ko 驱动模块**：macOS 上编不了 4.9.88（缺 Linux 头文件 + GCC 15 太新）。
  预留了 `tools/docker/`（Ubuntu 22.04 + `gcc-arm-linux-gnueabihf`）作为下一步的容器方案。
  ⚠️ **这块还没验证** —— 你机器上的 Docker Desktop 现在是关着的（`Cannot connect to the Docker daemon`），
  镜像没构建过。要用的时候先启动 Docker Desktop，我再补 `buildtools.sh` 做完整验证。
- **网络下发**：板子的 eth/wlan 都没 IP，目前走串口 ZMODEM（约 70–150 KB/s）。
  如果要 scp/NFS/tftp，得先给板子配 IP（或直接网线连路由器）。
- **eMMC 烧写**：需要 NXP 的 `uuu` 工具，本次没装；当前系统跑得好就不用动它。

## 7. 开发新例程

在 `examples/` 下新建目录，写 `main.c` 和一个三行 Makefile：

```makefile
TARGET := my_demo
SRCS   := main.c
include ../common.mk
```

然后 `./build.sh my_demo run`。依赖管理的往后都由 `examples/common.mk` 处理
（自动通配符搜集源码、`-MMD` 头文件依赖、`build/<TARGET>/` 输出、`strip`、大小打印）。
