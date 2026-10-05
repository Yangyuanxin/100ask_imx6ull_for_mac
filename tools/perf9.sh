#!/bin/sh
# 用本地素材测纯 CPU 能力 —— 网络直播源的测量结果噪声太大(同一档能测出 11 和 20),
# 先把一段视频抓到板子本地, 再测各档缩放, 得到的才是可信的相对成本。
export SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
U=https://fastlive.cctvplus.com/out/v1/ca6f9297b7314a63959435028af287fc/index_4.m3u8
echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null
killall -9 03_iptv ffmpeg 2>/dev/null
sleep 1

[ -f /tmp/t.ts ] || ffmpeg -nostdin -loglevel error -probesize 10000000 -max_delay 800000 \
    -t 15 -i "$U" -map 0:v:0 -an -c copy /tmp/t.ts 2>/dev/null
echo "素材: $(ls -l /tmp/t.ts 2>/dev/null | awk '{print $5}') 字节"

echo "--- 本地素材, 各档放大(端到端真写屏幕) ---"
for W in 640 800 870 1024; do
    ffmpeg -nostdin -loglevel info -i /tmp/t.ts -an -sws_flags fast_bilinear \
        -vf "scale=${W}:-2,pad=1024:600:(ow-iw)/2:(oh-ih)/2:color=black" \
        -pix_fmt bgra -f fbdev /dev/fb0 > /tmp/s.log 2>&1 &
    P=$!
    sleep 6
    printf "[放大到%s] CPU=" "$W"
    top -b -n 1 | grep '[f]fmpeg' | awk '{print $7"%"}'
    wait $P
    printf "     fps="
    tr '\r' '\n' < /tmp/s.log | grep -Eo 'fps=[ 0-9.]+' | tail -1
    sleep 1
done
echo PERF9_DONE
