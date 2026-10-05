#!/usr/bin/env bash
# 构建编译容器镜像 (需要 Docker Desktop 处于运行状态)
#   ./tools/docker/build_image.sh
# ⚠️ 该脚本尚未在本机验证过 Docker Desktop 未启动, 请先看 README 第 6 节
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

docker build -t 100ask-imx6ull-builder -f tools/docker/Dockerfile tools/docker
echo "[OK] 镜像 100ask-imx6ull-builder 就绪"
echo "进入容器编译内核:"
echo "  ./tools/docker/enter.sh"
echo "  make 100ask_imx6ull_defconfig && make zImage -j\$(nproc)"
