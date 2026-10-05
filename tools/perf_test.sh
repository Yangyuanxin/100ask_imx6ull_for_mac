#!/bin/sh
#
# 板子上的性能测试: 量化"解码 / 缩放 / 写屏"各占多少, 以及各档缩放策略的收益。
# 用法: ./push.sh tools/perf_test.sh -d /tmp && ./cmd.sh "nohup sh /tmp/perf_test.sh > /tmp/perf.log 2>&1 & echo started"
# 跑完约 90 秒, 然后: ./cmd.sh "cat /tmp/perf.log"
#
# 这套数据就是 ARCHITECTURE.md 里性能章节的来源, 改参数后可以再跑一遍对比。

export SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
U=https://fastlive.cctvplus.com/out/v1/ca6f9297b7314a63959435028af287fc/index_4.m3u8

# 锁最高频 —— 默认 ondemand 经常赖在 198/528MHz, 测出来的数会很低
echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null
killall -9 03_iptv ffmpeg 2>/dev/null
sleep 1

# 一段 12 秒, 第 8 秒时采样 CPU, 结束后取平均 fps
run() {
    ffmpeg -nostdin -loglevel info -rw_timeout 15000000 -probesize 10000000 \
        -max_delay 800000 -t 12 -i "$U" -map 0:v:0 -an -sws_flags fast_bilinear \
        -vf "$2" -pix_fmt bgra -f fbdev /dev/fb0 > /tmp/p.log 2>&1 &
    P=$!
    sleep 8
    printf "[%s] CPU=" "$1"
    top -b -n 1 | grep '[f]fmpeg' | awk '{print $7"%"}'
    wait $P
    printf "      fps="
    tr '\r' '\n' < /tmp/p.log | grep -Eo 'fps=[ 0-9.]+' | tail -1
    sleep 1
}

echo "频率=$(cat /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq) bpp=$(cat /sys/class/graphics/fb0/bits_per_pixel)"

echo "--- 解码到底是不是瓶颈(输出到 null, 不缩放不写屏) ---"
run "纯解码        " "null"
ffmpeg -nostdin -loglevel info -probesize 10000000 -max_delay 800000 -t 10 -i "$U" \
    -an -f null - 2>&1 | tr '\r' '\n' | grep -Eo 'fps=[ 0-9.]+' | tail -1

echo "--- 各档缩放策略(端到端, 真写屏幕) ---"
run "full  满屏放大"  "scale=1024:600:force_original_aspect_ratio=decrease,pad=1024:600:(ow-iw)/2:(oh-ih)/2"
run "fit   0.78屏宽 "  "scale=w='min(iw\,800)':h=-2,pad=1024:600:(ow-iw)/2:(oh-ih)/2"
run "native 不放大  "  "scale=w='min(iw\,1024)':h=-2,pad=1024:600:(ow-iw)/2:(oh-ih)/2"

echo "--- 限帧能省多少 CPU ---"
run "native 限15帧 "  "fps=15,scale=w='min(iw\,1024)':h=-2,pad=1024:600:(ow-iw)/2:(oh-ih)/2"
echo PERF_DONE
