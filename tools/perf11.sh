#!/bin/sh
# 用本地素材(从 Mac 抓的 HLS 分片, 已 scp 到 /tmp/t.ts)测各档放大的纯 CPU 成本。
# 每档只处理 5 秒素材, 跑得快, 也不受网络波动影响。
killall -9 03_iptv ffmpeg 2>/dev/null
echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null
sleep 1
ls -l /tmp/t.ts

for W in 640 870 1024; do
    ffmpeg -nostdin -loglevel info -i /tmp/t.ts -t 5 -an -sws_flags fast_bilinear \
        -vf "scale=${W}:-2,pad=1024:600:(ow-iw)/2:(oh-ih)/2:color=black" \
        -pix_fmt bgra -f fbdev /dev/fb0 > /tmp/u.log 2>&1 &
    P=$!
    sleep 3
    printf "[放大到%s] CPU=" "$W"
    top -b -n 1 | grep '[f]fmpeg' | awk '{print $7"%"}'
    wait $P
    printf "     fps="
    tr '\r' '\n' < /tmp/u.log | grep -Eo 'fps=[ 0-9.]+' | tail -1
    sleep 1
done
echo PERF11_DONE
