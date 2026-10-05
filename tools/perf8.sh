#!/bin/sh
# A. 真正的放大成本对比(用固定宽度, 小源也会被放大上去)
# B. 起播延迟: -probesize 给小一点能不能更快出画面(用 8 秒内输出的总帧数衡量,
#    帧数越多说明起播越快 —— 因为起播那 1~2 秒是纯空档)
export SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
U=https://fastlive.cctvplus.com/out/v1/ca6f9297b7314a63959435028af287fc/index_4.m3u8
echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null
killall -9 03_iptv ffmpeg 2>/dev/null
sleep 1

PAD="pad=1024:600:(ow-iw)/2:(oh-ih)/2:color=black"

run() {
    ffmpeg -nostdin -loglevel info -rw_timeout 15000000 -probesize 10000000 \
        -max_delay 800000 -t 12 -i "$U" -map 0:v:0 -an -sws_flags fast_bilinear \
        -vf "$2" -pix_fmt bgra -f fbdev /dev/fb0 > /tmp/r.log 2>&1 &
    P=$!
    sleep 8
    printf "[%s] CPU=" "$1"
    top -b -n 1 | grep '[f]fmpeg' | awk '{print $7"%"}'
    wait $P
    printf "      fps="
    tr '\r' '\n' < /tmp/r.log | grep -Eo 'fps=[ 0-9.]+' | tail -1
    sleep 1
}

echo "--- A. 放大到多宽还扛得住 ---"
for W in 640 800 870 1024; do
    run "放大到${W}px宽" "scale=${W}:-2,$PAD"
done

echo "--- B. 起播速度(8 秒内的总帧数, 越多说明起播越快) ---"
frames() {
    ffmpeg -nostdin -loglevel info -rw_timeout 15000000 -probesize "$2" \
        -max_delay 800000 -t 8 -i "$U" -map 0:v:0 -an -sws_flags fast_bilinear \
        -vf "scale=870:-2,$PAD" -pix_fmt bgra -f null - 2>&1 |
        tr '\r' '\n' | grep -Eo 'frame=[ 0-9]+' | tail -1
}
printf "[probesize 10M] "; frames x 10000000
printf "[probesize  2M] "; frames x 2000000
echo PERF8_DONE
