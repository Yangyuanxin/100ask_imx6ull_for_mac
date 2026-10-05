#!/bin/sh
# 验收: ① 满屏播放无黑边 ② 换台时有没有"正在切换"提示 ③ 切到 France 24 能播
export SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
killall -9 03_iptv ffmpeg 2>/dev/null
sleep 1
/root/03_iptv > /tmp/iptv.log 2>&1 &
sleep 3
P=$(pidof 03_iptv)
echo "播放器 pid=$P"

shot() { ffmpeg -hide_banner -loglevel error -f fbdev -i /dev/fb0 -frames:v 1 \
             -vf scale=768:-1 -q:v 4 -y "$1" 2>/dev/null; }

sleep 10
shot /tmp/v1.jpg            # ① CCTV+ 满屏

kill -USR1 $P               # 换台 → 应该先出"正在切换"
sleep 2
shot /tmp/v2.jpg            # ② 换台提示画面

sleep 12
kill -USR1 $P; sleep 13     # → CCTV+ 720p
kill -USR1 $P; sleep 14     # → France 24 English
shot /tmp/v3.jpg            # ③ France 24 画面

echo "--- 播放器日志 ---"
tail -5 /tmp/iptv.log
ls -l /tmp/v1.jpg /tmp/v2.jpg /tmp/v3.jpg
echo VERIFY_DONE
