#!/bin/sh
# fb1("FG") 到底是什么? 有没有真正的 HDMI 输出芯片?
echo "=== /proc/fb ==="
cat /proc/fb

echo "=== fb0 / fb1 背后的驱动 ==="
for f in fb0 fb1; do
    echo "--- $f"
    readlink /sys/class/graphics/$f/device 2>/dev/null
    cat /sys/class/graphics/$f/device/uevent 2>/dev/null | head -3
    cat /sys/class/graphics/$f/device/of_node/compatible 2>/dev/null; echo
done

echo "=== 已加载的显示相关模块 ==="
lsmod 2>/dev/null | grep -iE 'hdmi|adv|sii|tda|fb|drm|lcdif|mxs' || echo "(无)"

echo "=== 内核里编进去的 HDMI 相关驱动 ==="
ls /lib/modules/*/kernel/drivers/video/ 2>/dev/null | head -20
ls /lib/modules/*/kernel/drivers/gpu/drm/ 2>/dev/null | head -20

echo "=== dmesg 全部 fb 相关 ==="
dmesg | grep -i 'fb\|framebuffer\|lcdif\|mxs' | head -12

echo "=== 设备树里有没有 hdmi 节点 ==="
ls /proc/device-tree/soc/ 2>/dev/null | grep -iE 'hdmi|lcdif|lcd|ldb' 
find /proc/device-tree -iname '*hdmi*' 2>/dev/null | head -5 || echo "(无 hdmi 节点)"
echo CHK_HDMI2_DONE
