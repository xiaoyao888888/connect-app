请使用最新版本 RKWIFIBT_APP_V1.3.7z

特别注意：如果之前用到Deviceio，现在统一更名为rkwifibt-app, 之前的libDeviceio.so 变为 librkwifibt.so

补丁说明：
RKWIFIBT_APP_V1.3.7z  #RK Linux平台WiFiBT的应用接口API，方便客户快速开发相关应用

#注意：由于文档里面的相关deviceio的相关术语没有更新，所以对应关系为：
libDeviceio.so 对应新的：librkwifibt.so
deviceio_test 对应新的：rkwifibt_test

#用法参考：
蓝牙:
/11-Linux平台/WIFIBT编程接口/Rockchip_Developer_Guide_DeviceIo_Bluetooth_CN.pdf
/11-Linux平台/WIFIBT编程接口/最新蓝牙接口说明.txt
WiFi:
/11-Linux平台/WIFIBT编程接口/最新WIFI接口说明.txt

WiFi SOFTAP配网参考：
Rockchip_Developer_Guide_Network_Config_CN.pdf --- 3.3 Softap 配网
示例程序：
RKWIFIBT_APP_V1.3\test\rk_wifi_test.c
RKWIFIBT_APP_V1.3\test\softap\softap.c

#RKWIFIBT_APP说明：
include/	 	#头文件
lib64/   		#64接口库
lib32/   		#32接口库
RV1109_RV1126/  #特别注意: RV1109/1126平台，请使用这个目录RV1109_RV1126里面的so
test/    		#API示例用法

Makefile 修改：CC 和 SYSROOT 改成你实际使用的！！！ 主要是这个目录的差别：rockchip_rk3326_64 
CC := /PATH/buildroot/output/rockchip_rk3326_64/host/bin/aarch64-buildroot-linux-gnu-gcc
SYSROOT := --sysroot=/PATH/buildroot/output/rockchip_rk3326_64/host/aarch64-buildroot-linux-gnu/sysroot

make会生成rkwifibt_test (具体可自行修改Makefile)


#运行
librkwifibt.so push到  usr/lib/
rkwifibt_test push 任意位置

WiFi测试: rkwifibt_test wificonfig  #WiFi测试/及相关配网测试
蓝牙测试：rkwifibt_test bluetooth   #蓝牙相关API测试


#蓝牙功能特别注意：
使用上述接口是请确保蓝牙功能正常，SDK集成一个蓝牙初始化bt_init.sh脚本，库启动时会依赖这个脚本去给蓝牙做初始化！
请确保配置正确的模组型号，参考文档/11-Linux平台/WIFIBT开发文档/Rockchip_Developer_Guide_Linux_WIFI_BT_CN.pdf的第2章节，问题排查参考第4.3章节
正常情况下，开机会有如下文件：
/usr/bin/bt_init.sh
#如果是Realtek WiFi：
#!/bin/sh

killall rtk_hciattach

echo 0 > /sys/class/rfkill/rfkill0/state
sleep 2
echo 1 > /sys/class/rfkill/rfkill0/state
sleep 2

insmod /usr/lib/modules/hci_uart.ko
rtk_hciattach -n -s 115200 BT_TTY_DEV rtk_h5 &
hciconfig hci0 up

如果是正基/海华(CYPRESS):
#!/bin/sh

killall brcm_patchram_plus1

echo 0 > /sys/class/rfkill/rfkill0/state
sleep 2
echo 1 > /sys/class/rfkill/rfkill0/state
sleep 2

brcm_patchram_plus1 --bd_addr_rand --enable_hci --no2bytes --use_baudrate_for_download  --tosleep  200000 --baudrate 1500000 --patchram  BTFIRMWARE_PATH BT_TTY_DEV &
hciconfig hci0 up