#!/usr/bin/env python3
"""
把板子屏幕(framebuffer)的真实画面抓成图片存到本地。

板子端用 ffmpeg 的 fbdev 输入直接读 /dev/fb0 存成 jpg,
再 base64 通过串口传回来(纯 ASCII, 不怕串口回显把中文打乱)。

用法:
    # 板子当前屏幕上是什么就抓什么
    python3 tools/fbshot.py -o examples/03_iptv/docs/images/play.jpg

    # 抓之前先在板子上跑一段命令(比如启动播放器、等 8 秒再抓开机动画)
    python3 tools/fbshot.py -o boot.jpg --pre "killall -9 03_iptv; /root/03_iptv &" --delay 3

    # 只取回板子上已经抓好的文件
    python3 tools/fbshot.py -o play.jpg --fetch /tmp/s2.jpg
"""
import argparse
import base64
import os
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("需要 pyserial: pip3 install pyserial")

PORT = os.environ.get("IMX_PORT", "/dev/cu.usbserial-01E74639")
BAUD = int(os.environ.get("IMX_BAUD", "115200"))
REMOTE = "/tmp/fbshot.jpg"

# busybox base64 每行 76 字符, 全是可打印 ASCII —— 用这个特征把真正的
# 数据行从 shell 回显里挑出来(回显含空格/斜杠, 匹配不上)
B64_LINE = re.compile(r"^[A-Za-z0-9+/=]{40,}$")


def read_all(s, quiet=1.5, max_wait=120.0):
    """一直读, 直到连续 quiet 秒没有新数据"""
    buf = b""
    last = time.time()
    start = time.time()
    while time.time() - start < max_wait:
        d = s.read(s.in_waiting or 1)
        if d:
            buf += d
            last = time.time()
        else:
            time.sleep(0.05)
            if time.time() - last > quiet:
                break
    return buf.decode("utf-8", "replace")


def run(s, cmd, wait=2.0, timeout=120.0):
    s.write(cmd.encode() + b"\n")
    time.sleep(wait)
    return read_all(s, quiet=1.5, max_wait=timeout)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", required=True, help="本地保存路径")
    ap.add_argument("--pre", default="", help="抓图前在板子上执行的命令")
    ap.add_argument("--delay", type=float, default=0, help="--pre 之后等待几秒再抓")
    ap.add_argument("--scale", type=int, default=768, help="图片宽度, 默认 768")
    ap.add_argument("--q", type=int, default=4, help="jpg 质量 2(最好)~6, 默认 4")
    ap.add_argument("--fetch", default="", help="不重新抓, 直接取回板子上这个文件")
    args = ap.parse_args()

    s = serial.Serial(PORT, BAUD, timeout=0.3)
    s.write(b"\r")
    time.sleep(0.3)
    read_all(s, quiet=0.8, max_wait=5)

    remote = args.fetch or REMOTE

    if not args.fetch:
        if args.pre:
            print(f"[fbshot] 前置: {args.pre}")
            run(s, args.pre, wait=1.5)
        if args.delay:
            print(f"[fbshot] 等待 {args.delay}s ...")
            time.sleep(args.delay)
        shot = (f"ffmpeg -hide_banner -loglevel error -f fbdev -i /dev/fb0 "
                f"-frames:v 1 -vf scale={args.scale}:-1 -q:v {args.q} -y {remote}")
        print("[fbshot] 抓图中 ...")
        out = run(s, shot, wait=6, timeout=90)
        if "Error" in out or "error" in out.lower():
            print("[!] 板子抓图报错:\n", out[-800:])

    print(f"[fbshot] 取回 {remote} ...")
    out = run(s, f"base64 {remote}", wait=3, timeout=120)

    lines = [ln.strip() for ln in out.splitlines()]
    b64 = "".join(ln for ln in lines if B64_LINE.match(ln))
    if len(b64) < 1000:
        sys.exit(f"[!] 没拿到有效数据(只有 {len(b64)} 字节), 串口可能被打断了")

    data = base64.b64decode(b64 + "=" * (-len(b64) % 4))
    os.makedirs(os.path.dirname(os.path.abspath(args.out)) or ".", exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(data)

    ok = data[:2] == b"\xff\xd8"
    print(f"[fbshot] 已保存 {args.out}  ({len(data)} 字节, JPEG头{'正确' if ok else '异常!'})")
    s.close()
    if not ok:
        sys.exit(1)


if __name__ == "__main__":
    main()
