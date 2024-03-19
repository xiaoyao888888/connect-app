CFLAGS := -Wall -g
#CC := /home/xy/rk3326_release_baidu/buildroot/output/rockchip_rk3326_64/host/bin/aarch64-buildroot-linux-gnu-gcc
CC := /home/xy/rk3308_release/buildroot/output/rockchip_rk3308_release/host/bin/aarch64-rockchip-linux-gnu-gcc
#SYSROOT := --sysroot=/home/xy/rk3326_release_baidu/buildroot/output/rockchip_rk3326_64/host/aarch64-buildroot-linux-gnu/sysroot
SYSROOT := --sysroot=/home/xy/rk3308_release/buildroot/output/rockchip_rk3308_release/host/aarch64-rockchip-linux-gnu/sysroot

all: rkwifibt_test

OBJS := \
	test/main.o \
	test/rk_wifi_test.o \
	test/bt_test.o \
	test/rk_ble_app.o \
	test/softap/softap.o
#ARCH=arm
#CFLAGS += -lpthread -lasound -L lib -lrkwifibt_32 -I include/

#ARCH=arm64
CFLAGS += -lpthread -lasound -L lib64 -lrkwifibt -I include/

rkwifibt_test: $(OBJS)
	$(CC) -o rkwifibt_test $(OBJS) $(SYSROOT) $(CFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) rkwifibt_test
