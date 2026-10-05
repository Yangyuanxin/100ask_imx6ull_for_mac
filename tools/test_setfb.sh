#!/bin/sh
# 验证 IPTV_SCREEN 开关两个方向都能切
size() { tr ',' 'x' < /sys/class/graphics/fb0/virtual_size; }

echo "=== 起始 ==="
echo "  $(size) $(cat /sys/class/graphics/fb0/bits_per_pixel)bpp"

echo "=== 1) 默认(IPTV_SCREEN 未设 = 优先 LCD) ==="
/root/03_iptv --setfb
echo "  -> $(size) $(cat /sys/class/graphics/fb0/bits_per_pixel)bpp"

echo "=== 2) IPTV_SCREEN=hdmi (优先电视) ==="
IPTV_SCREEN=hdmi /root/03_iptv --setfb
echo "  -> $(size) $(cat /sys/class/graphics/fb0/bits_per_pixel)bpp"

echo "=== 3) IPTV_SCREEN=lcd (显式) ==="
IPTV_SCREEN=lcd /root/03_iptv --setfb
echo "  -> $(size) $(cat /sys/class/graphics/fb0/bits_per_pixel)bpp"

echo "=== 4) 重复调用不该有副作用 ==="
/root/03_iptv --setfb
echo "  -> $(size)"
echo TEST_SETFB_DONE
