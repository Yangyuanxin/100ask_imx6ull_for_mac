#!/bin/sh
# 各国外源在板子上的实测: 能否起播 + 分辨率 + fps + CPU
export SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null
killall -9 03_iptv ffmpeg wget 2>/dev/null
sleep 1

run() {   # $1=名字 $2=地址
    printf "[%s] " "$1"
    ffmpeg -nostdin -loglevel info -rw_timeout 15000000 -probesize 10000000 \
        -max_delay 800000 -t 12 -i "$2" -map 0:v:0 -an -sws_flags fast_bilinear \
        -vf "scale=1024:600:force_original_aspect_ratio=decrease,pad=1024:600:(ow-iw)/2:(oh-ih)/2" \
        -pix_fmt bgra -f fbdev /dev/fb0 > /tmp/w.log 2>&1 &
    P=$!
    sleep 9
    printf "CPU="
    top -b -n 1 | grep '[f]fmpeg' | awk '{print $7"%"}'
    wait $P
    printf "  fps="
    tr '\r' '\n' < /tmp/w.log | grep -Eo 'fps=[ 0-9.]+' | tail -1
    printf "  分辨率="
    tr '\r' '\n' < /tmp/w.log | grep -Eo ', [0-9]{3,4}x[0-9]{3,4}' | head -1 | tr -d ', '
    printf "\n"
    sleep 1
}

run "France24 EN" "https://live.france24.com/hls/live/2037218/F24_EN_HI_HLS/master_900.m3u8"
run "CGTN EN    " "https://news.cgtn.com/resource/live/english/cgtn-news.m3u8"
run "DW EN      " "https://dwamdstream102.akamaized.net/hls/live/2015525/dwstream102/index.m3u8"
run "CCTV+ 360p " "https://fastlive.cctvplus.com/out/v1/ca6f9297b7314a63959435028af287fc/index_4.m3u8"
echo PERF12_DONE
