#!/bin/sh
# CGTN 到底哪个地址能用: master(news.cgtn.com) 和它引用的 360p 子地址(live.cgtn.com)
killall -9 wget 2>/dev/null
for u in \
  "https://news.cgtn.com/resource/live/english/cgtn-news.m3u8" \
  "https://live.cgtn.com/500/prog_index.m3u8" \
  "https://livedoc.cgtn.com/500d/prog_index.m3u8" \
  "https://livear.cgtn.com/500a/prog_index.m3u8"
do
    printf "%-58s " "$(echo "$u" | cut -c9-64)"
    wget -q -O /dev/null -T 6 "$u" 2>/dev/null && echo OK || echo FAIL
done
echo CHK2_DONE
