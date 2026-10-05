#!/bin/sh
# 插入 HDMI 后 fb0 会被切到什么模式? 看设备树给 sii902x 配的 mode_str
echo "=== sii902x 设备树节点 ==="
D=$(find /proc/device-tree -name 'sii902x*' 2>/dev/null | head -1)
echo "path: $D"
[ -n "$D" ] && {
    ls $D
    echo "--- mode_str:";     cat $D/mode_str 2>/dev/null; echo
    echo "--- bits-per-pixel:"; cat $D/bits-per-pixel 2>/dev/null | xxd 2>/dev/null | head -1
    echo "--- compatible:";   cat $D/compatible 2>/dev/null; echo
}

echo "=== 当前 fb0 real/virtual size ==="
cat /sys/class/graphics/fb0/virtual_size

echo "=== 缓存的 EDID(上一次插的显示器) 前 3 行 ==="
head -3 /sys/bus/i2c/devices/1-0039/edid 2>/dev/null | cut -c1-120

echo "=== EDID 里解析出的分辨率(DTD) ==="
python3 - <<'EOF' 2>/dev/null || awk 'BEGIN{print "(无 python3)"}'
try:
    raw = open('/sys/bus/i2c/devices/1-0039/edid','rb').read()
    print("edid 文件", len(raw), "字节, 前 16 字节:", raw[:16].hex())
except Exception as e:
    print("读不了:", e)
EOF
echo CHK_DTS_DONE
