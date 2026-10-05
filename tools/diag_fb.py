#!/usr/bin/env python3
"""诊断 framebuffer 布局: 黑边到底是 framebuffer 里的还是显示端造成的。

关键看两个数:
  xres_virtual vs xres   —— 虚拟屏比可视区宽的话, 右边会看不到
  xoffset / yoffset      —— 显示起点在 framebuffer 里的偏移, 不为 0 就会错位/留黑边
  line_length            —— 每行字节数, 必须 = xres_virtual * bpp/8, 否则整幅画会斜
"""
import fcntl, struct

FBIOGET_VSCREENINFO = 0x4600
FBIOGET_FSCREENINFO = 0x4602

f = open('/dev/fb0', 'rb')
var = fcntl.ioctl(f.fileno(), FBIOGET_VSCREENINFO, bytes(160))
fix = fcntl.ioctl(f.fileno(), FBIOGET_FSCREENINFO, bytes(68))

xres, yres, xres_v, yres_v = struct.unpack_from('IIII', var, 0)
xoff, yoff, bpp = struct.unpack_from('III', var, 16)
line_length = struct.unpack_from('I', fix, 44)[0]
smem_len = struct.unpack_from('I', fix, 20)[0]

print("=== fb0 几何 ===")
print(f"  可视区 xres={xres} yres={yres}")
print(f"  虚拟区 xres_virtual={xres_v} yres_virtual={yres_v}")
print(f"  显示偏移 xoffset={xoff} yoffset={yoff}")
print(f"  bpp={bpp}  line_length={line_length}")
print(f"  期望 line_length = xres_virtual*bpp/8 = {xres_v * bpp // 8}")
print(f"  {'✔ 一致' if line_length == xres_v * bpp // 8 else '✘ 不一致! 画面会斜/错位'}")
print(f"  framebuffer 大小 = {smem_len} 字节, 需要 {line_length * yres_v}")
if xoff or yoff:
    print(f"  ⚠️ 显示起点被偏移了 ({xoff},{yoff}) —— 这会在边上留出黑条")

# 取中间一行, 分段统计亮度, 找出黑边在哪
bypp = bpp // 8
f.seek((yres // 2) * line_length)
row = f.read(line_length)

print("\n=== 中间一行的亮度分布(每 1/16 采样) ===")
for i in range(16):
    s = int(len(row) / bypp * i / 16)
    e = int(len(row) / bypp * (i + 1) / 16)
    tot = cnt = 0
    for p in range(s, e, max(1, (e - s) // 20)):
        px = row[p * bypp:(p + 1) * bypp]
        if bypp == 2:
            v = struct.unpack('<H', px)[0]
            r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
            tot += (r + g + b); cnt += 1
        elif bypp >= 3:
            tot += px[0] + px[1] + px[2]; cnt += 1
    avg = tot / max(cnt, 1)
    bar = '#' * int(avg / 8)
    print(f"  段{i:2d} ({i*100//16:3d}%~{(i+1)*100//16:3d}%): 亮度{avg:6.1f} {bar}")
