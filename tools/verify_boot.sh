#!/bin/sh
# 重启后验证: 插着 HDMI 开机是否自动改回 LCD 分辨率 (ASCII only, 串口传中文会坏)
echo "=== boot log (first 14 lines) ==="
head -14 /var/log/iptv-init.log
echo "=== fb0 now ==="
echo "  size = $(tr ',' 'x' < /sys/class/graphics/fb0/virtual_size)"
echo "  bpp  = $(cat /sys/class/graphics/fb0/bits_per_pixel)"
echo "=== hdmi cable ==="
cat /sys/bus/i2c/devices/1-0039/cable_state
echo "=== player ==="
ps | grep -c '[0]3_iptv'
echo VERIFY_BOOT_DONE
