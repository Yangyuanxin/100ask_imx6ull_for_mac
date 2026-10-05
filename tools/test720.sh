#!/bin/sh
# HDMI 模式下(1280x720/16bpp)两种源的对比:
#   360p 源 -> 要放大 2 倍(糊) / 720p 源 -> 1:1 直通(清晰, 但解码更重)
export SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
killall -9 03_iptv ffmpeg 2>/dev/null
sleep 1

run() {
    printf "[%s] " "$1"
    ffmpeg -nostdin -loglevel info -rw_timeout 15000000 -probesize 10000000 \
        -max_delay 800000 -t 12 -i "$2" -map 0:v:0 -an -sws_flags fast_bilinear \
        -vf "scale=1280:720:force_original_aspect_ratio=decrease,pad=1280:720:(ow-iw)/2:(oh-ih)/2" \
        -pix_fmt rgb565le -f fbdev /dev/fb0 > /tmp/w.log 2>&1 &
    P=$!
    sleep 9
    printf "CPU="
    top -b -n 1 | grep '[f]fmpeg' | awk '{print $7"%"}'
    wait $P
    printf " fps="
    tr '\r' '\n' < /tmp/w.log | grep -Eo 'fps=[ 0-9.]+' | tail -1
    printf " 源分辨率="
    tr '\r' '\n' < /tmp/w.log | grep -Eo ', [0-9]{3,4}x[0-9]{3,4}' | head -1 | tr -d ', '
    printf "\n"
    sleep 1
}

run "360p源(放大2倍)" "https://fastlive.cctvplus.com/out/v1/ca6f9297b7314a63959435028af287fc/index_4.m3u8"
run "720p源(1:1)    " "https://fastlive.cctvplus.com/out/v1/ca6f9297b7314a63959435028af287fc/index_2.m3u8"
echo TEST720_DONE
