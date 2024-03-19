buildroot系统编译：
1. 修改 
vim SDK/buildroot/package/rockchip/Config.in

diff --git a/package/rockchip/Config.in b/package/rockchip/Config.in
index 6735cdcef6..dc64849d52 100644
--- a/package/rockchip/Config.in
+++ b/package/rockchip/Config.in
@@ -216,6 +216,7 @@ source "package/rockchip/mtp/Config.in"
 source "package/rockchip/alsa-config/Config.in"
 source "package/rockchip/libcapsimage/Config.in"
 source "package/rockchip/rkscript/Config.in"
+source "package/rockchip/rkwifibt-app/Config.in"
mkdir buildroot/package/rockchip/rkwifibt-app

2. 
创建：buildroot/package/rockchip/rkwifibt-app/Config.in，内容如下：

config BR2_PACKAGE_RKWIFIBT_APP
        bool "rkwifibt wireless applicantion"
        select BR2_PACKAGE_ALSA_LIB
        select BR2_PACKAGE_BLUEZ_ALSA
        select BR2_PACKAGE_BLUEZ5_UTILS

创建：buildroot/package/rockchip/rkwifibt-app/rkwifibt-app.mk，内容如下：

RKWIFIBT_APP_SITE = $(TOPDIR)/../external/rkwifibt-app   //切记这里改成实际下载的目录：git clone ssh://10.10.10.29:29418/linux/ipc/test/wifibt
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


3. 编译：
buildroot目录（注意先完整编译SDK，参考http://10.10.10.215:3000/zh/developer_guide/sdk_development/sdk_projects/rockchip_linux_sdk_dl_cn）
make rkwifibt-app
make rkwifibt-app-dirclean
make rkwifibt-app-rebuld