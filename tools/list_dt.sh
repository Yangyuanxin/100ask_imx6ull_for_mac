#!/bin/sh
# 列出 lcdif 下所有节点, 找到 LCD 面板的时序在哪
LCDIF="/proc/device-tree/soc/aips-bus@02100000/lcdif@021c8000"
echo "=== lcdif 下的子节点 ==="
ls "$LCDIF"
echo
echo "=== display 节点下 ==="
ls "$LCDIF/display" 2>/dev/null
echo
echo "=== display#1 节点下 ==="
ls "$LCDIF/display#1" 2>/dev/null
echo
echo "=== 递归找 timings ==="
find "$LCDIF" -type d 2>/dev/null | head -20
echo
echo "=== 所有含 hactive 的节点 ==="
find "$LCDIF" -name 'hactive' 2>/dev/null | head -5
echo LIST_DT_DONE
