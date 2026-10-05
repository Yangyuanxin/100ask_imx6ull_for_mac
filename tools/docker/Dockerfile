# 内核 / 驱动模块编译容器
#
# ⚠️ 状态: 未验证 —— 搭建环境时本机 Docker Desktop 没有在运行, 镜像没构建过。
#    使用流程: 先启动 Docker Desktop, 再执行 ./build_image.sh
#
# 选择在 arm64 基础的 Ubuntu 上装 armhf 交叉工具链, 在 Apple Silicon 上是原生速度,
# 不需要 Rosetta 模拟 x86_64。

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        bc bison flex libssl-dev libncurses-dev libelf-dev \
        build-essential python3 python3-distutils rsync cpio file kmod \
        wget ca-certificates u-boot-tools device-tree-compiler \
        gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf \
    && rm -rf /var/lib/apt/lists/*

ENV ARCH=arm
ENV CROSS_COMPILE=arm-linux-gnueabihf-

# 容器内的工作目录挂载点: 宿主机项目根目录
RUN mkdir -p /work
WORKDIR /work

CMD ["/bin/bash"]
