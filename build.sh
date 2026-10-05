#!/usr/bin/env bash
# ============================================================
# 编译 examples/ 下的例程
#   ./build.sh                     列出所有例程
#   ./build.sh 01_hello            交叉编译
#   ./build.sh 01_hello run        编译后通过串口下发并在板子上运行
#   ./build.sh 01_hello size
#   ./build.sh 01_hello clean
# ============================================================
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

if [ $# -eq 0 ]; then
    echo "可用例程:"
    ls -d examples/*/ | sed 's|examples/||; s|/||; s|^|  |'
    exit 0
fi

NAME="$1"; shift
if [ ! -d "examples/$NAME" ]; then
    echo "没有这个例程: $NAME"
    exit 1
fi

TARGET="${1:-all}"; shift 2>/dev/null || true
RUNARGS="$*"

make -C "examples/$NAME" "$TARGET" RUNARGS="$RUNARGS"
