#!/bin/sh
# 本地素材(只用 6 秒)测各档放大的纯 CPU 成本。
# 教训: 素材别抓太长 —— 本地播放是"尽快处理", 15 秒素材每档要跑一分多钟。
export SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
U=https://fastlive.cctvplus.com/out/v1/ca6f9297b7314a63959435028af287fc/index_4.m3u8
echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null
killall -9 03_iptv ffmpeg 2>/dev/null
sleep 1

rm -f /tmp/t2.ts
ffmpeg -nostdin -loglevel error -probesize 10000000 -max_delay 800000 -t 6 \
    -i "$U" -map 0:v:0 -an -c copy /tmp/t2.ts 2>/dev/null
echo "素材=$(ls -l /tmp/t2.ts 2>/dev/null | awk '{print $5}')字节"

for W in 640 870 1024; do
    ffmpeg -nostdin -loglevel info -i /tmp/t2.ts -an -sws_flags fast_bilinear \
        -vf "scale=${W}:-2,pad=1024:600:(ow-iw)/2:(oh-ih)/2:color=black" \
        -pix_fmt bgra -f fbdev /dev/fb0 > /tmp/s.log 2>&1 &
    P=$!
    sleep 4
    printf "[放大到%s] CPU=" "$W"
    top -b -n 1 | grep '[f]fmpeg' | awk '{print $7"%"}'
    wait $P
    printf "     fps="
    tr '\r' '\n' < /tmp/s.log | grep -Eo 'fps=[ 0-9.]+' | tail -1
    sleep 1
done
echo PERF10_DONE
