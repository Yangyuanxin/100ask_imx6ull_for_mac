#!/bin/sh
# 底板上到底有没有 HDMI 发射芯片? 这类芯片(ADV7511/sii902x/TFP410/TC358743)一定挂在 I2C 上
echo "=== 已注册的 I2C 设备 ==="
ls /sys/bus/i2c/devices/ 2>/dev/null
for d in /sys/bus/i2c/devices/*; do
    [ -f "$d/name" ] && echo "  $(basename $d): $(cat $d/name)"
done

echo "=== i2c 总线 ==="
ls /dev/i2c* 2>/dev/null || echo "(无 /dev/i2c*)"

echo "=== i2cdetect 扫描 ==="
which i2cdetect >/dev/null 2>&1 && {
    for b in 0 1 2 3; do
        echo "--- bus $b"
        i2cdetect -y $b 2>/dev/null
    done
} || echo "(没有 i2cdetect)"

echo "=== 有没有 HPD/热插拔相关 GPIO 或中断 ==="
grep -iE 'hdmi|hpd|plug' /proc/interrupts 2>/dev/null | head -5 || echo "(无)"
echo CHK_I2C_DONE
