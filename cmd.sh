#!/usr/bin/env bash
# ============================================================
# 在开发板上执行一条命令并打印输出
#   ./cmd.sh "uname -a"
#   ./cmd.sh "dmesg | tail -20"
# ============================================================
ROOT="$(cd "$(dirname "$0")" && pwd)"
exec "$(command -v python3)" "$ROOT/tools/imx.py" cmd "$@"
