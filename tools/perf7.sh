#!/bin/sh
# 找"画面够大又不卡"的那一档: 屏宽 1024, 试几档放大比例
export SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
U=https://fastlive.cctvplus.com/out/v1/ca6f9297b7314a63959435028af287fc/index_4.m3u8
echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null
killall -9 03_iptv ffmpeg 2>/dev/null
sleep 1

run() {
    ffmpeg -nostdin -loglevel info -rw_timeout 15000000 -probesize 10000000 \
        -max_delay 800000 -t 12 -i "$U" -map 0:v:0 -an -sws_flags fast_bilinear \
        -vf "$2" -pix_fmt bgra -f fbdev /dev/fb0 > /tmp/q.log 2>&1 &
    P=$!
    sleep 8
    printf "[%s] CPU=" "$1"
    top -b -n 1 | grep '[f]fmpeg' | awk '{print $7"%"}'
    wait $P
    printf "      fps="
    tr '\r' '\n' < /tmp/q.log | grep -Eo 'fps=[ 0-9.]+' | tail -1
    sleep 1
}

for W in 640 800 870 942 1024; do
    run "宽 ${W}px" "scale=w='min(iw\,$W)':h=-2,pad=1024:600:(ow-iw)/2:(oh-ih)/2:color=black"
done
echo PERF7_DONE
