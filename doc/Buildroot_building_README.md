# rkwifibt-app
Rockchip WiFiBT applicantion base wpa_supplicant and blueZ.

RK_Buildroot_SDK

cd external/

git clone https://github.com/xiaoyao888888/rkwifibt-app.git

vim buildroot/package/rockchip/Config.in

```diff
diff --git a/package/rockchip/Config.in b/package/rockchip/Config.in
index 6735cdcef6..dc64849d52 100644
--- a/package/rockchip/Config.in
+++ b/package/rockchip/Config.in
@@ -216,6 +216,7 @@ source "package/rockchip/mtp/Config.in"
 source "package/rockchip/alsa-config/Config.in"
 source "package/rockchip/libcapsimage/Config.in"
 source "package/rockchip/rkscript/Config.in"
+source "package/rockchip/rkwifibt-app/Config.in"
```

mkdir buildroot/package/rockchip/rkwifibt-app

touch buildroot/package/rockchip/rkwifibt-app/Config.in

```Makefile
config BR2_PACKAGE_RKWIFIBT_APP
        bool "rkwifibt wireless applicantion"
        select BR2_PACKAGE_ALSA_LIB
        select BR2_PACKAGE_BLUEZ_ALSA
        select BR2_PACKAGE_BLUEZ5_UTILS
```


touch buildroot/package/rockchip/rkwifibt-app/rkwifibt-app.mk

```makefile
RKWIFIBT_APP_SITE = $(TOPDIR)/../external/rkwifibt-app
RKWIFIBT_APP_SITE_METHOD = local
RKWIFIBT_APP_INSTALL_STAGING = YES

RKWIFIBT_APP_CONF_OPTS += -DBLUEZ5_UTILS=TRUE
RKWIFIBT_APP_CONF_OPTS += -DBLUEZ=TRUE
RKWIFIBT_APP_CONF_OPTS += -DREALTEK=TRUE
RKWIFIBT_APP_DEPENDENCIES += readline bluez5_utils libglib2

ifeq ($(call qstrip,$(BR2_ARCH)), arm)
RKWIFIBT_APP_BUILD_TYPE = arm
else ifeq ($(call qstrip, $(BR2_ARCH)), aarch64)
RKWIFIBT_APP_BUILD_TYPE = arm64
endif

RKWIFIBT_APP_CONF_OPTS += -DCPU_ARCH=$(BR2_ARCH) -DBUILD_TYPE=$(RKWIFIBT_APP_BUILD_TYPE)

RKWIFIBT_APP_DEPENDENCIES += wpa_supplicant alsa-lib

RKWIFIBT_APP_CONF_OPTS += -DCMAKE_INSTALL_STAGING=$(STAGING_DIR)

$(eval $(cmake-package))
```

Build:

make rkwifibt-app

make rkwifibt-app-dirclean

make rkwifibt-app-rebuld

Output:

SDK/buildroot/output/rockchip_rk3xxx/target/usr/lib/librkwifibt.a

SDK/buildroot/output/rockchip_rk3xxx/target/usr/bin/rkwifibt_app_test



Build SO:

SDK/buildroot/output/rockchip_rk3xxx/target/usr/lib/librkwifibt.so

```diff
rkwifibt-app ] git diff .
diff --git a/CMakeLists.txt b/CMakeLists.txt
index 170229e..1167f12 100644
--- a/CMakeLists.txt
+++ b/CMakeLists.txt
@@ -12,8 +12,8 @@ add_definitions(-DBLUEZ5_UTILS -DFIXED_POINT=16)

file(GLOB_RECURSE BLUZ_SRC "bluez/*.c")

-#add_library(rkwifibt SHARED
-add_library(rkwifibt STATIC
+add_library(rkwifibt SHARED
+#add_library(rkwifibt STATIC
                ${WIFI_SRC}
                ${UTILITY}
                ${BLUZ_SRC}
```



To customer:

```shell
# 提供下面文件给客户
test/           # 删掉这个目录rm test/rk3326-wangyi-dictionary-pan/
include/
lib64/librkwifibt.so  
lib/librkwifibt.so
Makefile

# 参考编译的Makefile，需要修改为实际的路径
Mailefile
CC := /PATH/buildroot/output/rockchip_rk3326_64/host/bin/aarch64-buildroot-linux-gnu-gcc
SYSROOT := --sysroot=/PATH/buildroot/output/rockchip_rk3326_64/host/aarch64-buildroot-linux-gnu/sysroot

# 生成测试程序（deviceio_test）
rkwifibt_test 
```

