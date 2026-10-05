#!/bin/sh
# 端到端测试: 不同缩放策略下, 真写屏幕时的 fps 和 CPU 占用。
# 用法: ./push.sh tools/perf2.sh -d /tmp && ./cmd.sh "nohup sh /tmp/perf2.sh > /tmp/perf2.log 2>&1 & echo started"

export SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
U=https://fastlive.cctvplus.com/out/v1/ca6f9297b7314a63959435028af287fc/index_4.m3u8

killall -9 03_iptv ffmpeg 2>/dev/null
sleep 1

i=0
for s in 1024:600 800:450 640:360; do
    i=$((i + 1))
    ffmpeg -nostdin -loglevel info -probesize 10M -max_delay 800000 -t 10 -i "$U" -an \
        -sws_flags fast_bilinear \
        -vf "scale=$s:force_original_aspect_ratio=decrease,pad=1024:600:(ow-iw)/2:(oh-ih)/2" \
        -pix_fmt bgra -f fbdev /dev/fb0 > "/tmp/f$i.log" 2>&1 &
    P=$!
    sleep 7
    printf "[%s] CPU=" "$s"
    top -b -n 1 | grep '[f]fmpeg' | awk '{print $7}'
    wait $P
    printf "     fps="
    tr '\r' '\n' < "/tmp/f$i.log" | grep -Eo 'fps=[ 0-9.]+' | tail -1
    sleep 1
done
echo PERF_DONE
