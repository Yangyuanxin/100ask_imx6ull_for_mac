#!/bin/sh
# 在板子上实测这几个国外源通不通(DNS + 取 m3u8)
# 用法: ./push.sh tools/chk_src.sh -d /tmp && ./cmd.sh "nohup sh /tmp/chk_src.sh > /tmp/chk.log 2>&1 & echo started"

echo "=== DNS ==="
for h in live.cgtn.com dwamdstream102.akamaized.net live.france24.com; do
    printf "[%s] " "$h"
    nslookup "$h" 2>&1 | grep -q 'Address' && echo ok || echo FAIL
done

echo "=== 取 m3u8(T=8s) ==="
for u in \
  "https://live.france24.com/hls/live/2037218/F24_EN_HI_HLS/master_900.m3u8" \
  "https://dwamdstream102.akamaized.net/hls/live/2015525/dwstream102/index.m3u8" \
  "https://news.cgtn.com/resource/live/english/cgtn-news.m3u8"
do
    printf "%-60s " "$(echo "$u" | cut -c9-66)"
    wget -q -O /dev/null -T 8 "$u" 2>/dev/null && echo OK || echo FAIL
done
echo CHK_DONE
