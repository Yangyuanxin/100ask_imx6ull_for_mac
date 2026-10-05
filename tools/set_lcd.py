#!/usr/bin/env python3
"""把 fb0 改回 LCD 面板原生的 1024x600/32bpp(设备树里的时序)。

插上 HDMI 后 sii902x 驱动会按电视 EDID 把 fb0 改成 1280x720,
面板物理只有 1024x600 且时序不匹配, 于是出现黑边/画面偏移。
改回面板时序就能恢复 —— 代价是 HDMI 也跟着变成 1024x600(同源, 由电视自己缩放)。

用法:
    python3 set_lcd.py          # 改回 1024x600/32bpp (优先 LCD)
    python3 set_lcd.py 1280x720 # 改到 HDMI 的 1280x720 (优先电视)
"""
import fcntl, struct, sys

FBIOGET_VSCREENINFO = 0x4600
FBIOPUT_VSCREENINFO = 0x4601

# LCD 面板时序: 来自设备树 display#1/display-timings/timing0_1024x768
LCD = dict(xres=1024, yres=600, pixclock=20000, lm=140, rm=160, um=20, bm=12, hs=20, vs=3, bpp=32)
# HDMI 电视标准时序: CEA-861 1280x720@60
HDMI = dict(xres=1280, yres=720, pixclock=13468, lm=220, rm=110, um=20, bm=5, hs=40, vs=5, bpp=32)

mode = HDMI if (len(sys.argv) > 1 and '1280' in sys.argv[1]) else LCD
name = 'HDMI 1280x720' if mode is HDMI else 'LCD 1024x600'

f = open('/dev/fb0', 'r+b')
var = bytearray(fcntl.ioctl(f.fileno(), FBIOGET_VSCREENINFO, bytes(160)))
old_sync = struct.unpack_from('I', var, 128)[0]     # 保留 sync 极性, 别让驱动翻转

struct.pack_into('IIII', var, 0, mode['xres'], mode['yres'], mode['xres'], mode['yres'])
struct.pack_into('I', var, 24, mode['bpp'])
struct.pack_into('IIIIIII', var, 100, mode['pixclock'], mode['lm'], mode['rm'],
                 mode['um'], mode['bm'], mode['hs'], mode['vs'])
struct.pack_into('I', var, 128, old_sync)           # sync 极性
struct.pack_into('I', var, 132, 0)                  # vmode = noninterlaced
var[84:88] = struct.pack('I', 0)                    # activate = NOW

try:
    fcntl.ioctl(f.fileno(), FBIOPUT_VSCREENINFO, bytes(var))
except OSError as e:
    print('驱动拒绝:', e)
    sys.exit(1)

back = fcntl.ioctl(f.fileno(), FBIOGET_VSCREENINFO, bytes(160))
x, y, xv, yv = struct.unpack_from('IIII', back, 0)
bpp = struct.unpack_from('I', back, 24)[0]
pc, lm, rm, um, bm, hs, vs = struct.unpack_from('IIIIIII', back, 100)
print(f'目标: {name}')
print(f'生效: {x}x{y} (虚拟 {xv}x{yv}) {bpp}bpp')
print(f'  时序 pixclock={pc} lm={lm} rm={rm} um={um} bm={bm} hs={hs} vs={vs}')
h = x + lm + rm + hs
v = y + um + bm + vs
print(f'  -> {h}x{v}, {1e12/(pc*h*v):.2f} Hz')
