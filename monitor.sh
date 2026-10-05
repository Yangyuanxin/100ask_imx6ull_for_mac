#!/usr/bin/env bash
# ============================================================
# 串口终端 (115200)
#   ./monitor.sh             连接默认串口 /dev/cu.usbserial-01E74639
#   ./monitor.sh -p /dev/xx  指定串口
#   ./monitor.sh -b 1500000  指定波特率
# 退出: Ctrl-]
# ============================================================
ROOT="$(cd "$(dirname "$0")" && pwd)"
exec "$(command -v python3)" "$ROOT/tools/imx.py" monitor "$@"
