TARGET := 03_iptv
SRCS   := main.c

include ../common.mk

# 把程序 + 频道表 + 开机自启动脚本装到板子上
deploy: all
	@# 先停掉正在跑的播放器: 它一直在往串口打日志, 会把后面的 ZMODEM 传输打断
	$(ROOT)/tools/imx.py cmd "killall -9 03_iptv ffmpeg 2>/dev/null; echo stopped"
	$(ROOT)/tools/imx.py push $(BUILD_DIR)/$(TARGET) -d /root
	$(ROOT)/tools/imx.py push channels.conf -d /etc/iptv
	$(ROOT)/tools/imx.py push S95iptv -d /etc/init.d
	@# ZMODEM 传过去会丢掉执行位, 不补 chmod 的话服务脚本跑不起来(Exit 126)
	$(ROOT)/tools/imx.py cmd "chmod +x /root/$(TARGET) /etc/init.d/S95iptv; ls -l /etc/init.d/S95iptv"
	@echo "[OK] 已部署。板上手动启动: /etc/init.d/S95iptv start"

# 装在板子上并立刻重启验证开机自启动
deploy-reboot: deploy
	$(ROOT)/tools/imx.py cmd "chmod +x /etc/init.d/S95iptv; reboot"
	@echo "[OK] 板子正在重启, 约 60 秒后会自动开始播放"
