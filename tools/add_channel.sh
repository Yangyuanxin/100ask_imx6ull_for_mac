#!/bin/sh
#
# 给板子加一个电视频道, 并当场试播验证能不能放、能跑多少帧。
#
#   用法: ./tools/add_channel.sh "频道名" "地址"
#   例  : ./tools/add_channel.sh "CCTV-1" "http://.../xxx.m3u8"
#         ./tools/add_channel.sh "电信IPTV-央视一套" "rtp://239.3.1.xxx:8000"
#
# 做了 4 件事:
#   1) 本机先 curl 一下, 确认地址能访问(rtmp/rtp/udp 跳过这步)
#   2) 追加到 examples/03_iptv/channels.conf 并上传到板子 /etc/iptv/
#   3) 在板子上用 ffmpeg 试播 12 秒, 抓实际帧率 —— 这块板是纯软解,
#      帧率低于 ~8 FPS 的源基本就是幻灯片, 脚本会明确提示你
#   4) 重启播放服务让新频道生效
#
# ⚠️ 只加你自己有接收权限的源: 家里运营商 IPTV 的组播地址、
#    电视台官方免费公开的直播地址, 或者你自己从官方页面拿到的地址。
#
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CONF="$ROOT/examples/03_iptv/channels.conf"
NAME="${1:-}"
URL="${2:-}"

if [ -z "$NAME" ] || [ -z "$URL" ]; then
    echo "用法: $0 \"频道名\" \"地址\""
    exit 1
fi

case "$URL" in
rtmp://*|rtsp://*|rtp://*|udp://*|udp:*)
    echo "[1/4] $URL 是实时流, 跳过本机可达性检查"
    ;;
*)
    code=$(curl -s -o /dev/null -w '%{http_code}' --max-time 15 "$URL" 2>/dev/null || echo 000)
    echo "[1/4] 本机检查: HTTP $code"
    if [ "$code" = "000" ] || [ "$code" = "404" ] || [ "$code" = "403" ]; then
        echo "  ⚠️  这个地址在本机都访问不了, 板子大概率也放不了。"
        echo "     仍然继续添加, 你可以自己确认要不要保留。"
    fi
    ;;
esac

echo "[2/4] 写入频道表: $CONF"
printf '%s|%s\n' "$NAME" "$URL" >> "$CONF"
"$ROOT/push.sh" "$CONF" -d /etc/iptv >/dev/null 2>&1
echo "  已上传"

echo "[3/4] 板子试播 12 秒, 测实际帧率 ..."
"$ROOT/cmd.sh" "killall -9 03_iptv ffmpeg 2>/dev/null; sleep 1; \
export SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt; \
ffmpeg -nostdin -loglevel info -t 12 -rw_timeout 15000000 -i '$URL' \
  -map 0:v:0 -an -sws_flags fast_bilinear \
  -vf 'scale=1024:600:force_original_aspect_ratio=decrease,pad=1024:600:(ow-iw)/2:(oh-ih)/2:color=black' \
  -pix_fmt bgra -f fbdev /dev/fb0 2>&1 | tr '\r' '\n' \
  | grep -Eo 'frame=[0-9]+|fps=[0-9.]+|Invalid data|Error|error|404|403' | tail -8" -t 60 2>&1 | tail -9

echo
echo "[4/4] 重启播放服务 ..."
"$ROOT/cmd.sh" "/etc/init.d/S95iptv restart >/dev/null 2>&1; echo restarted" -t 25 2>&1 | tail -2
echo
echo "完成。频道 \"$NAME\" 已加入。"
echo "  - 觉得卡: 板上启动时带 IPTV_LOWCPU=1 (只解关键帧), 或换个更低码率的源"
echo "  - 删频道: 直接编辑 $CONF 后 ./push.sh $CONF -d /etc/iptv"
