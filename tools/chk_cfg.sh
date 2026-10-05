#!/bin/sh
# EDID 分支是否编译进去(CONFIG_FB_MODE_HELPERS) + 当前 HDMI 状态
echo "=== HDMI/EDID 相关内核配置 ==="
zcat /proc/config.gz 2>/dev/null | grep -E 'FB_MODE_HELPERS|FB_MXC_EDID|FB_MXS_SII902X|FB_MXC_HDMI' || echo "(读不了 config.gz)"

echo "=== 当前 HDMI 状态 ==="
printf "cable_state: "; cat /sys/bus/i2c/devices/1-0039/cable_state
printf "fb0 分辨率:  "; cat /sys/class/graphics/fb0/virtual_size

echo "=== 热插拔中断计数(插拔时会 +1) ==="
grep -i 'SII902x_det' /proc/interrupts
echo CHK_CFG_DONE
