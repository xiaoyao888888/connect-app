#ifndef __A2DP_SOURCE_CTRL__
#define __A2DP_SOURCE_CTRL__

#include "../gdbus/gdbus.h"
#include "RkBtBase.h"
#include "RkBle.h"
#include "RkBtSource.h"

#ifdef __cplusplus
extern "C" {
#endif

extern uint16_t transport_volume;
extern RkBtContent *g_rkbt_content;

#define DEV_PLATFORM_UNKNOWN    0 /* unknown platform */
#define DEV_PLATFORM_IOS        1 /* Apple iOS */
#define IOS_VENDOR_SOURCE_BT    76 /* Bluetooth SIG, apple id = 0x4c */
#define IOS_VENDOR_SOURCE_USB   1452 /* USB Implementer's Forum, apple id = 0x05ac */

typedef enum _bt_devices_type {
	BT_DEVICES_A2DP_SINK,
	BT_DEVICES_A2DP_SOURCE,
	BT_DEVICES_BLE,
	BT_DEVICES_HFP,
	BT_DEVICES_SPP,
} BtDeviceType;

void cmd_power(bool enable);
void cmd_discoverable(bool enable);
void cmd_pairable(bool enable);
void bt_state_change(GDBusProxy *proxy, const char *change, RK_BT_STATE new_state);
void bt_media_state_change(char *path, const char *change, RK_BT_STATE new_state, void *data);


void bt_register_state_callback(RK_BT_STATE_CALLBACK cb);
void bt_deregister_state_callback();
void bt_state_send(struct remote_dev *rdev, RK_BT_STATE state);

int bt_open(RkBtContent *bt_content);
void bt_close(void);

int reconn_last_devices(BtDeviceType type);
int get_dev_platform(char *address);
int get_current_dev_platform();
bool bt_is_connected();

gboolean disconnect_current_devices(gpointer data);
gboolean connect_by_address(gpointer data);
gboolean pair_by_addr(gpointer data);
gboolean unpair_by_addr(gpointer data);
gboolean bt_start_discovery(gpointer data);
gboolean bt_cancel_discovery(gpointer data);

//void dev_found_send(GDBusProxy *proxy, RK_BT_DEV_FOUND_CALLBACK cb, int change);
struct GDBusProxy *find_device_by_address(char *address);
void set_default_attribute(GDBusProxy *proxy);
void source_set_reconnect_tag(bool reconnect);
void source_stop_connecting();
bool get_device_connected_properties(char *addr);
int a2dp_master_save_status(char *address);

//new
gboolean _bt_disconnect_by_address(gpointer data);
gboolean _bt_remove_by_address(gpointer data);

#ifdef __cplusplus
}
#endif

#endif /* __A2DP_SOURCE_CTRL__ */
