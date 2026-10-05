#!/usr/bin/env python3
"""
100ask_imx6ull 串口工具集

子命令:
  probe                       查询开发板基础信息 (kernel/rootfs/glibc/LCD/网络)
  cmd      "<shell 命令>"     在板上执行一条命令并打印输出
  monitor                     交互式串口终端 (Ctrl-A 后按 Q 退出, 或直接 Ctrl-C 两次)
  push     FILE [DEST]        通过 ZMODEM 把本地文件上传到板子 (默认 /root/)
  run      FILE [ARGS...]     上传 + chmod +x + 执行, 打印输出
  script   FILE               上传并执行本地 shell 脚本

所有子命令支持 -p/--port 与 -b/--baud 覆盖默认串口参数。
"""
import argparse
import os
import subprocess
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("缺少 pyserial, 请先执行: python3 -m pip install pyserial")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_PORT = "/dev/cu.usbserial-01E74639"
DEFAULT_BAUD = 115200
DEFAULT_DEST = "/root"

# 板上 Buildroot 的 shell 提示符
PROMPT = "[root@100ask:"


def open_port(port, baud, timeout=1.0):
    return serial.Serial(port, baud, timeout=timeout)


def drain(s, settle=0.15):
    """读空串口缓冲区"""
    time.sleep(settle)
    buf = b""
    while True:
        n = s.in_waiting
        if not n:
            break
        buf += s.read(n)
        time.sleep(settle)
    return buf


def send_cmd(s, cmd, wait=1.0, timeout=20.0):
    """
    发送一条命令并返回输出。
    会一直读到输出稳定为止(连续 0.4s 无新数据且已过最短等待时间),
    这样耗时几秒的程序也能拿到完整输出。
    """
    drain(s, 0.1)
    s.write(cmd.encode() + b"\n")
    buf = b""
    start = time.time()
    last = start
    while True:
        now = time.time()
        n = s.in_waiting
        if n:
            buf += s.read(n)
            last = now
        else:
            time.sleep(0.05)
            if now - start >= wait and now - last >= 0.4:
                break
        if now - start > timeout:
            break
    return buf.decode("utf-8", "replace")


def enter_shell(s):
    """敲回车确保进入 shell (板子上电直接是 root shell)"""
    s.write(b"\n")
    time.sleep(0.3)
    drain(s, 0.1)


def clean(text):
    """去掉 ANSI 转义"""
    import re
    return re.sub(r"\x1b\[[0-9;?]*[a-zA-Z]", "", text)


# --------------------------------------------------------------------------
def cmd_probe(args):
    s = open_port(args.port, args.baud)
    enter_shell(s)
    cmds = [
        "uname -a",
        "cat /etc/os-release | head -3",
        "cat /proc/device-tree/model 2>/dev/null; echo",
        "cat /proc/cpuinfo | grep -E 'Hardware|model name|Features' | head -3",
        "cat /proc/cmdline",
        "free -m | head -2",
        "df -h / | tail -1",
        "cat /sys/class/graphics/fb0/virtual_size; cat /sys/class/graphics/fb0/bits_per_pixel",
        "ip -o link show 2>/dev/null | awk -F': ' '{print $2}' | tr '\\n' ' '; echo",
    ]
    for c in cmds:
        send_cmd(s, "\r")  # 清空行编辑器的历史补全
        out = send_cmd(s, c)
        print(clean(out).strip())
    s.close()


def cmd_run_cmd(args):
    s = open_port(args.port, args.baud)
    enter_shell(s)
    print(clean(send_cmd(s, args.command, wait=args.wait)).strip())
    s.close()


def cmd_monitor(args):
    """交互式终端: 直接把 stdin 透传到串口"""
    s = open_port(args.port, args.baud, timeout=0.05)
    import select
    import termios
    import tty

    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    print(f"[100ask] 已连接 {args.port} @ {args.baud}, Ctrl-] 退出")
    try:
        tty.setraw(fd)
        s.write(b"\n")
        while True:
            r, _, _ = select.select([fd, s], [], [], 0.05)
            if fd in r:
                ch = os.read(fd, 1)
                if ch == b"\x1d":  # Ctrl-]
                    break
                s.write(ch)
            if s in r:
                data = s.read(s.in_waiting or 1)
                if data:
                    os.write(fd, data)
    except KeyboardInterrupt:
        pass
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)
        s.close()
        print("\r\n[100ask] 已断开")


# --------------------------------------------------------------------------
def _zmodem_send(port, baud, files):
    """
    用 sz 通过 ZMODEM 发送文件。
    串口 fd 直接交给 sz 做 stdin/stdout, 这样流控由 sz 自己处理。
    """
    sz = "/opt/homebrew/bin/sz" if os.path.exists("/opt/homebrew/bin/sz") else "sz"
    p = open_port(port, baud, timeout=0.2)
    try:
        with os.fdopen(os.dup(p.fileno()), "r+b", buffering=0) as raw:
            proc = subprocess.run(
                [sz, "-b", "--zmodem", *files],
                stdin=raw, stdout=raw, stderr=subprocess.PIPE,
            )
        time.sleep(0.5)
        tail = drain(p, 0.2)
    finally:
        p.close()
    return proc, clean(tail.decode("utf-8", "replace"))


def cmd_push(args):
    files = [os.path.abspath(f) for f in args.files]
    for f in files:
        if not os.path.isfile(f):
            sys.exit(f"文件不存在: {f}")
    dest = args.dest

    s = open_port(args.port, args.baud)
    enter_shell(s)
    # 清理历史行编辑, 确保处于干净的提示符
    send_cmd(s, "\r")
    names = " ".join(os.path.basename(f) for f in files)
    print(f"[100ask] 板子准备接收 -> {dest}")
    s.write(f"cd {dest} && rz -y -E\r".encode())
    time.sleep(1.0)
    ready = drain(s, 0.2).decode("utf-8", "replace")
    if "waiting" not in ready.lower() and "\x18B" not in ready:
        print("[WARN] 未检测到 rz 就绪信号, 原始输出:", clean(ready)[:200])
    s.close()  # 释放串口给 sz

    proc, tail = _zmodem_send(args.port, args.baud, files)
    print(f"[100ask] sz 退出码 {proc.returncode}; {tail.strip()[:300]}")
    if proc.stderr:
        print("[sz stderr]", proc.stderr.decode("utf-8", "replace")[:300])

    # 复查文件是否落盘
    time.sleep(0.5)
    s = open_port(args.port, args.baud)
    enter_shell(s)
    out = clean(send_cmd(s, f"ls -l {dest}/{names.split()[0]}", wait=1.0))
    s.close()
    if names.split()[0] in out:
        print("[100ask] 上传成功:")
        print(out.strip())
    else:
        print("[FAIL] 未在目标目录找到文件:\n", out.strip()[:300])
        return 1
    return 0


def cmd_run(args):
    src = os.path.abspath(args.file)
    base = os.path.basename(src)
    dest = args.dest
    if cmd_push(argparse.Namespace(port=args.port, baud=args.baud,
                                   files=[src], dest=dest)) != 0:
        return 1
    s = open_port(args.port, args.baud)
    enter_shell(s)
    remote = f"{dest}/{base}"
    out = clean(send_cmd(s, f"chmod +x {remote} && {remote} {args.args}", wait=args.wait))
    s.close()
    print("----- 板子输出 -----")
    print(out.strip())
    print("--------------------")
    return 0


# --------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description="100ask_imx6ull 串口工具")
    ap.add_argument("-p", "--port", default=os.environ.get("IMX6ULL_PORT", DEFAULT_PORT))
    ap.add_argument("-b", "--baud", type=int, default=DEFAULT_BAUD)
    sub = ap.add_subparsers(dest="action", required=True)

    sub.add_parser("probe", help="查询开发板信息").set_defaults(func=cmd_probe)

    c = sub.add_parser("cmd", help="执行一条命令")
    c.add_argument("command")
    c.add_argument("-w", "--wait", type=float, default=1.0)
    c.set_defaults(func=cmd_run_cmd)

    sub.add_parser("monitor", help="交互式串口终端").set_defaults(func=cmd_monitor)

    pu = sub.add_parser("push", help="ZMODEM 上传文件")
    pu.add_argument("files", nargs="+")
    pu.add_argument("-d", "--dest", default=DEFAULT_DEST)
    pu.set_defaults(func=cmd_push)

    r = sub.add_parser("run", help="上传并运行可执行文件")
    r.add_argument("file")
    r.add_argument("-d", "--dest", default=DEFAULT_DEST)
    r.add_argument("-w", "--wait", type=float, default=2.0)
    r.add_argument("-a", "--args", default="", help="传给程序的参数")
    r.set_defaults(func=cmd_run)

    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main() or 0)
