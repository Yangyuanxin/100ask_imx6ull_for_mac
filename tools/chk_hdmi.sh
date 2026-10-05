#!/bin/sh
# 探查 HDMI / 多屏硬件能力
echo "=== /dev/fb* ==="
ls -l /dev/fb* 2>/dev/null || echo "只有一个 fb"

echo "=== 每个 fb 的信息 ==="
for f in /sys/class/graphics/fb*; do
    echo "--- $f"
    [ -f "$f/name" ] && echo "  name: $(cat $f/name)"
    [ -f "$f/virtual_size" ] && echo "  virtual_size: $(cat $f/virtual_size)"
    [ -f "$f/bits_per_pixel" ] && echo "  bpp: $(cat $f/bits_per_pixel)"
    [ -f "$f/modes" ] && echo "  modes: $(cat $f/modes)"
    [ -f "$f/state" ] && echo "  state: $(cat $f/state)"
done

echo "=== drm ==="
ls /sys/class/drm/ 2>/dev/null || echo "no drm"

echo "=== dmesg 里跟显示/插拔有关的 ==="
dmesg | grep -iE 'hdmi|drm|connector|fb[0-9]|elcdif|ldb|adv|si70|tda' | tail -12

echo "=== 有没有热插拔事件接口 ==="
ls /sys/class/switch 2>/dev/null; ls /sys/class/extcon 2>/dev/null || echo "no extcon"
echo "=== 板子型号 ==="
cat /proc/device-tree/model 2>/dev/null; echo
echo CHK_HDMI_DONE
