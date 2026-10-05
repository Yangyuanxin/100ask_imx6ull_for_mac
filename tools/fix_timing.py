#!/usr/bin/env python3
"""把 1280x720 的时序改成标准 CEA-861 值, 看 HDMI 上的黑边消不消。

用法:
    python3 fix_timing.py        # 改成标准时序
    python3 fix_timing.py reset  # 改回驱动原来给的值
"""
import fcntl, struct, sys

FBIOGET_VSCREENINFO = 0x4600
FBIOPUT_VSCREENINFO = 0x4601

f = open('/dev/fb0', 'r+b')
var = bytearray(fcntl.ioctl(f.fileno(), FBIOGET_VSCREENINFO, bytes(160)))

xres, yres = struct.unpack_from('II', var, 0)
old = struct.unpack_from('IIIIIII', var, 100)   # pixclock,lm,rm,um,bm,hs,vs
print(f"当前: xres={xres} yres={yres}")
print(f"  pixclock={old[0]} lm={old[1]} rm={old[2]} um={old[3]} bm={old[4]} hs={old[5]} vs={old[6]}")

if len(sys.argv) > 1 and sys.argv[1] == 'reset':
    # 驱动原来给的值(1280x720)
    new = (13401, 194, 61, 22, 1, 133, 3)
else:
    # CEA-861 标准 1280x720@60: hfront=110 hback=220 hsync=40 / vfront=5 vback=20 vsync=5
    # 注意 fb 术语: left_margin=后肩, right_margin=前肩
    new = (13468, 220, 110, 20, 5, 40, 5)

struct.pack_into('IIIIIII', var, 100, *new)
var[88:92] = struct.pack('I', 0)     # height/width 清掉, 别让驱动按物理尺寸重算

try:
    fcntl.ioctl(f.fileno(), FBIOPUT_VSCREENINFO, bytes(var))
    print("已写入新时序")
except OSError as e:
    print("驱动拒绝:", e)
    sys.exit(1)

# 读回确认
back = fcntl.ioctl(f.fileno(), FBIOGET_VSCREENINFO, bytes(160))
x2, y2 = struct.unpack_from('II', back, 0)
got = struct.unpack_from('IIIIIII', back, 100)
print(f"生效后: xres={x2} yres={y2}")
print(f"  pixclock={got[0]} lm={got[1]} rm={got[2]} um={got[3]} bm={got[4]} hs={got[5]} vs={got[6]}")
htotal = x2 + got[1] + got[2] + got[5]
vtotal = y2 + got[3] + got[4] + got[6]
print(f"  -> {htotal}x{vtotal}, {1e12/(got[0]*htotal*vtotal):.2f} Hz")
print("驱动是否原样接受:", "是" if got == new else f"否(被驱动改成了 {got})")
