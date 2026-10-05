#!/usr/bin/env python3
"""读 LCDIF 输出时序, 跟标准 1280x720@60 (CEA-861) 对比。

黑边不在 framebuffer 里(xoffset=0、整行都有内容), 那多半是时序问题:
水平前肩(front porch)给大了, 电视会把消隐区也显示出来, 画面就整体右移 ——
表现正是"左边一条黑的, 右边被裁掉一点"。
"""
import fcntl, struct

FBIOGET_VSCREENINFO = 0x4600
f = open('/dev/fb0', 'rb')
var = fcntl.ioctl(f.fileno(), FBIOGET_VSCREENINFO, bytes(160))

xres, yres = struct.unpack_from('II', var, 0)
pixclock = struct.unpack_from('I', var, 100)[0]
lm, rm = struct.unpack_from('II', var, 104)      # left/right margin = 水平前肩/后肩
um, bm = struct.unpack_from('II', var, 112)      # upper/lower margin = 垂直前肩/后肩
hs, vs = struct.unpack_from('II', var, 120)      # 同步脉宽

htotal = xres + lm + rm + hs
vtotal = yres + um + bm + vs
fps = 1e12 / (pixclock * htotal * vtotal) if pixclock else 0

print(f"=== 当前时序 {xres}x{yres} ===")
print(f"  水平: 前肩(左)={lm}  后肩(右)={rm}  同步={hs}   total={htotal}")
print(f"  垂直: 前肩(上)={um}  后肩(下)={bm}  同步={vs}   total={vtotal}")
print(f"  pixclock={pixclock} ps -> 刷新率 {fps:.2f} Hz")
print(f"  水平消隐占比 {(lm+rm+hs)/htotal*100:.1f}%  垂直 {(um+bm+vs)/vtotal*100:.1f}%")

print("\n=== 标准 1280x720@60 (CEA-861) ===")
std = dict(lm=110, rm=220, hs=40, um=5, bm=20, vs=5, pixclock=13468)  # pixclock ps
s_htotal = 1280 + 110 + 220 + 40
s_vtotal = 720 + 5 + 20 + 5
print(f"  水平: 前肩={std['lm']}  后肩={std['rm']}  同步={std['hs']}   total={s_htotal}")
print(f"  垂直: 前肩={std['um']}  后肩={std['bm']}  同步={std['vs']}   total={s_vtotal}")
print(f"  pixclock={std['pixclock']} ps -> {1e12/(std['pixclock']*s_htotal*s_vtotal):.2f} Hz")

print("\n=== 差异 ===")
for name, cur, want in (("水平前肩", lm, std['lm']), ("水平后肩", rm, std['rm']),
                        ("水平同步", hs, std['hs']), ("垂直前肩", um, std['um']),
                        ("垂直后肩", bm, std['bm']), ("垂直同步", vs, std['vs'])):
    flag = "✔" if abs(cur - want) <= max(2, want * 0.1) else "✘ 偏离较大"
    print(f"  {name}: 当前 {cur:5d}  标准 {want:5d}   {flag}")
