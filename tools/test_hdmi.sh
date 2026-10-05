#!/bin/sh
# 插着 HDMI 时 fb0 被改成 1280x720/16bpp, 测这个组合下还能跑多少帧
export SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
killall -9 03_iptv ffmpeg 2>/dev/null
sleep 1

W=$(cut -d, -f1 /sys/class/graphics/fb0/virtual_size)
H=$(cut -d, -f2 /sys/class/graphics/fb0/virtual_size)
BPP=$(cat /sys/class/graphics/fb0/bits_per_pixel)
echo "fb0 = ${W}x${H} ${BPP}bpp"
PF=rgb565le; [ "$BPP" = "32" ] && PF=bgra

ffmpeg -nostdin -loglevel info -rw_timeout 15000000 -probesize 10000000 \
    -max_delay 800000 -t 12 -i \
    "https://fastlive.cctvplus.com/out/v1/ca6f9297b7314a63959435028af287fc/index_4.m3u8" \
    -map 0:v:0 -an -sws_flags fast_bilinear \
    -vf "scale=$W:$H:force_original_aspect_ratio=decrease,pad=$W:$H:(ow-iw)/2:(oh-ih)/2" \
    -pix_fmt $PF -f fbdev /dev/fb0 > /tmp/h.log 2>&1 &
P=$!
sleep 9
printf "CPU="
top -b -n 1 | grep '[f]fmpeg' | awk '{print $7"%"}'
wait $P
printf "fps="
tr '\r' '\n' < /tmp/h.log | grep -Eo 'fps=[ 0-9.]+' | tail -1

# 抓一张 HDMI 上正在显示的画面
ffmpeg -hide_banner -loglevel error -f fbdev -i /dev/fb0 -frames:v 1 \
    -vf scale=768:-1 -q:v 4 -y /tmp/hdmi_shot.jpg 2>/dev/null
ls -l /tmp/hdmi_shot.jpg
echo TEST_HDMI_DONE
