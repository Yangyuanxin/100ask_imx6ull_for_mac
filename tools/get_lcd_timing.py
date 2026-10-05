#!/usr/bin/env python3
"""从设备树里读 LCD 面板的原始时序, 用来把 fb0 改回 1024x600。

HDMI 驱动插上时会把 fb0 从 1024x600 改成 1280x720, LCD 面板物理只有 1024x600,
拿不到匹配的时序就会出现黑边/画面偏移。改回去必须用面板自己的时序参数,
这些参数就在设备树的 display-timings 里。
"""
import os, struct

def rd(p):
    try:
        with open(p, 'rb') as f:
            return f.read()
    except Exception:
        return None

def u32(b):
    return struct.unpack('>I', b)[0] if b and len(b) == 4 else None

base = '/proc/device-tree'
# 找 lcdif 节点
lcdif = None
for root, dirs, files in os.walk(base):
    if 'lcdif@' in os.path.basename(root):
        lcdif = root
        break
print('lcdif 节点:', lcdif)

timings = os.path.join(lcdif, 'display-timings') if lcdif else None
if not timings or not os.path.isdir(timings):
    print('没有 display-timings, 列出 lcdif 下内容:')
    print(os.listdir(lcdif) if lcdif else '(无)')
    raise SystemExit(1)

print('display-timings 下的模式:', os.listdir(timings))
# 找 native-mode 或第一个 timing 节点
native = rd(os.path.join(timings, 'native-mode'))
print('native-mode:', native.hex() if native else '(无)')

for name in os.listdir(timings):
    p = os.path.join(timings, name)
    if not os.path.isdir(p):
        continue
    print(f'\n--- 模式 {name} ---')
    keys = ('clock-frequency', 'hactive', 'vactive', 'hfront-porch', 'hback-porch',
            'hsync-len', 'vfront-porch', 'vback-porch', 'vsync-len')
    vals = {}
    for k in keys:
        v = u32(rd(os.path.join(p, k)))
        vals[k] = v
        if v is not None:
            print(f'  {k:16s} = {v}')
    if vals['hactive'] == 1024 and vals['vactive'] == 600:
        print('  ★ 这就是 LCD 面板的原生时序')
        # 换算成 fb 时序: pixclock(ps) = 1e12 / clock-frequency
        pix = int(1e12 / vals['clock-frequency'])
        print(f'\n  fb 参数: xres={vals["hactive"]} yres={vals["vactive"]}')
        print(f'    left_margin(后肩)={vals["hback-porch"]}'
              f' right_margin(前肩)={vals["hfront-porch"]}'
              f' hsync_len={vals["hsync-len"]}')
        print(f'    upper_margin={vals["vback-porch"]}'
              f' lower_margin={vals["vfront-porch"]}'
              f' vsync_len={vals["vsync-len"]}')
        print(f'    pixclock={pix} ps')
