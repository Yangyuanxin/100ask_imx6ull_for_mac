#!/bin/sh
# 0x39 到底是什么芯片? 有没有被当作显示输出(bridge)用起来?
echo "=== 所有 i2c 设备及其驱动 ==="
for d in /sys/bus/i2c/devices/*; do
    n=$(basename $d)
    case "$n" in i2c-*) continue;; esac
    printf "%-10s name=%-16s driver=%s\n" "$n" "$(cat $d/name 2>/dev/null)" \
        "$(readlink $d/driver 2>/dev/null | xargs -r basename)"
done

echo "=== dmesg 搜 HDMI 发射芯片/bridge ==="
dmesg | grep -iE 'adv75|sii|tda|bridge|encoder|connector|tda998|hdmi' | head -10 || echo "(无)"

echo "=== drm card0 下有什么 ==="
ls /sys/class/drm/card0/ 2>/dev/null
for c in /sys/class/drm/card0/*; do
    [ -f "$c/status" ] && echo "  $(basename $c): status=$(cat $c/status)"
done

echo "=== fb1 是第二个图层还是独立输出: 看 mxsfb 注册了几个 ==="
dmesg | grep -i 'mxsfb\|FG' | head -8

echo "=== 内核配置里有没有 HDMI/DRM bridge ==="
zcat /proc/config.gz 2>/dev/null | grep -iE 'HDMI|ADV75|SII902|DRM_BRIDGE|TDA998' | head -10 || echo "(无 /proc/config.gz)"
echo CHK_I2C2_DONE
