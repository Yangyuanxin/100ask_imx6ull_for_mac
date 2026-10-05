#!/usr/bin/env python3
"""读 LCD 面板时序(LCD 原生模式), 用于把 fb0 从 HDMI 的 1280x720 改回去"""
import os, struct

D = '/proc/device-tree/soc/aips-bus@02100000/lcdif@021c8000/display#1/display-timings'
print('display-timings 下:', os.listdir(D))

for name in os.listdir(D):
    p = os.path.join(D, name)
    if not os.path.isdir(p):
        continue
    print(f'\n--- {name} ---')
    vals = {}
    for k in sorted(os.listdir(p)):
        raw = open(os.path.join(p, k), 'rb').read()
        try:
            v = struct.unpack('>I', raw)[0] if len(raw) == 4 else int.from_bytes(raw, 'big')
            vals[k] = v
            print(f'  {k:18s} = {v}')
        except Exception:
            print(f'  {k:18s} = {raw!r}')

    ha, va = vals.get('hactive'), vals.get('vactive')
    if ha and va:
        clk = vals.get('clock-frequency', 0)
        pix = int(1e12 / clk) if clk else 0
        print(f'\n  ★ 面板原生: {ha}x{va} @ {clk/1e6:.2f}MHz')
        print(f'  fb 参数: xres={ha} yres={va} pixclock={pix}ps')
        print(f'    left_margin(后肩)={vals.get("hback-porch")}'
              f' right_margin(前肩)={vals.get("hfront-porch")}'
              f' hsync_len={vals.get("hsync-len")}')
        print(f'    upper_margin={vals.get("vback-porch")}'
              f' lower_margin={vals.get("vfront-porch")}'
              f' vsync_len={vals.get("vsync-len")}')
        htotal = ha + vals.get('hback-porch', 0) + vals.get('hfront-porch', 0) + vals.get('hsync-len', 0)
        vtotal = va + vals.get('vback-porch', 0) + vals.get('vfront-porch', 0) + vals.get('vsync-len', 0)
        if pix:
            print(f'    -> {htotal}x{vtotal}, {1e12/(pix*htotal*vtotal):.2f} Hz')
