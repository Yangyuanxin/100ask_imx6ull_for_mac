#!/bin/sh
# sii902x(HDMI 发射芯片)当前状态: 有没有检测到显示器插入(HPD)
echo "=== 设备节点 ==="
ls /sys/bus/i2c/devices/1-0039/ 2>/dev/null
cat /sys/bus/i2c/devices/1-0039/uevent 2>/dev/null

echo "=== dmesg 里 sii902x 全部信息 ==="
dmesg | grep -i 'sii\|902x' | head -10

echo "=== 寄存器 dump (0x39) ==="
which i2cdump >/dev/null 2>&1 && i2cdump -y 1 0x39 2>/dev/null | head -18 || echo "(无 i2cdump)"

echo "=== 关键状态位 ==="
which i2cget >/dev/null 2>&1 && {
    echo -n "0x3D(中断/HPD 状态): "; i2cget -y 1 0x39 0x3d 2>/dev/null
    echo -n "0xC8(系统状态):      "; i2cget -y 1 0x39 0xc8 2>/dev/null
    echo -n "0x1E(芯片ID):        "; i2cget -y 1 0x39 0x1e 2>/dev/null
    echo -n "0x1A(电源/连接):     "; i2cget -y 1 0x39 0x1a 2>/dev/null
} || echo "(无 i2cget)"

echo "=== 有没有 HDMI 的中断 ==="
grep -iE 'sii|hdmi|902x' /proc/interrupts 2>/dev/null | head -3 || echo "(无 sii 中断)"
echo CHK_SII_DONE
