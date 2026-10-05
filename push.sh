#!/usr/bin/env bash
# ============================================================
# 把编译产物/任意文件通过 ZMODEM 下发到开发板
#   ./push.sh build/01_hello/01_hello        -> /root/
#   ./push.sh a.out /tmp                     -> /tmp/
#   ./push.sh file1 file2 -d /mnt
# 板上已自带 rz/sz (lrzsz 0.12.20)
# ============================================================
ROOT="$(cd "$(dirname "$0")" && pwd)"
exec "$(command -v python3)" "$ROOT/tools/imx.py" push "$@"
