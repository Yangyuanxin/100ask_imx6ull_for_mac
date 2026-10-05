#!/usr/bin/env bash
# 进入编译容器 (项目根目录挂载到容器里的 /work)
#   ./tools/docker/enter.sh [命令...]
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

exec docker run --rm -it \
    -v "$ROOT:/work" \
    -e ARCH=arm \
    -e CROSS_COMPILE=arm-linux-gnueabihf- \
    -w /work \
    100ask-imx6ull-builder "${@:-/bin/bash}"
