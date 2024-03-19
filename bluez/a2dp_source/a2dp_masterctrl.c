// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2012  Intel Corporation. All rights reserved.
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

//#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <glib.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <stdbool.h>
#include <wordexp.h>
#include <sys/un.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <linux/input.h>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>

#include "a2dp_masterctrl.h"
#include "shell.h"
#include "util.h"
#include "agent.h"
#include "gatt.h"
#include "advertising.h"
#include "../bluez_ctrl.h"
#include "../gatt_config.h"
#include "../gatt_client.h"
#include "../bluez_ctrl.h"
#include "utility.h"
#include "slog.h"

#define BT_RECONNECT_CFG "/data/cfg/lib/bluetooth/reconnect_cfg"

/* operands in passthrough commands */
#define AVC_VOLUME_UP        0x41
#define AVC_VOLUME_DOWN      0x42
#define AVC_PLAY             0x44
#define AVC_STOP             0x45
#define AVC_PAUSE            0x46
#define AVC_FORWARD          0x4b
#define AVC_BACKWARD         0x4c

/* String display constants */
#define COLORED_NEW COLOR_GREEN "NEW" COLOR_OFF
#define COLORED_CHG COLOR_YELLOW "CHG" COLOR_OFF
#define COLORED_DEL COLOR_RED "DEL" COLOR_OFF

#define PROMPT_ON   COLOR_BLUE "[bluetooth]" COLOR_OFF "# "
#define PROMPT_OFF  "Waiting to connect to bluetoothd..."

#define DISTANCE_VAL_INVALID    0x7FFF

//DBUS CONN
DBusConnection *dbus_conn = NULL;
//security agent
static GDBusProxy *agent_manager;

/* Default NoInputNoOutput  DisplayYesNo
	case 0x00:
		str = "DisplayOnly";
		break;
	case 0x01:
		str = "DisplayYesNo";
		break;
	case 0x02:
		str = "KeyboardOnly";
		break;
	case 0x03:
		str = "NoInputNoOutput";
		break;
	case 0x04:
		str = "KeyboardDisplay";

kernel/BT SPEC: I/O capabilities
#define HCI_IO_DISPLAY_ONLY	0x00
#define HCI_IO_DISPLAY_YESNO	0x01
#define HCI_IO_KEYBOARD_ONLY	0x02
#define HCI_IO_NO_INPUT_OUTPUT	0x03
*/
static char *auto_register_agent = "KeyboardDisplay";

struct adapter {
	GDBusProxy *proxy;
	GDBusProxy *ad_proxy;
	GList *devices;
	GList *app_devs;
};

typedef struct {
	RK_BT_STATE_CALLBACK bt_state_cb;
	//RK_BLE_GATT_CALLBACK ble_state_cb;
} rk_bt_callback;

struct adapter *default_ctrl = NULL;
GDBusProxy *default_dev = NULL;
GDBusProxy *default_attr = NULL;
GList *ctrl_list;

GDBusClient *btsrc_client = NULL;

//gobal bt context
RkBtContent in_rkbt_content;
RkBtContent *g_rkbt_content = NULL;

//save tmp scan list
struct remote_dev g_scan_array[100];

/* pthread_mutex_init(&mutex, NULL);
 * pthread_mutex_destroy(&mutex);
 * pthread_mutex_lock(&mutex);
 * pthread_mutex_unlock(&mutex)
 */
static pthread_mutex_t mutex;

static bool BT_OPENED = 0;
//static bool BT_CLOSED = 0;

static rk_bt_callback g_bt_callback;

//func declaration
void print_fixed_iter(const char *label, const char *name,
						DBusMessageIter *iter);
void print_iter(const char *label, const char *name,
						DBusMessageIter *iter);
static void print_uuid(const char *uuid);
static void filter_clear_transport();
static int remove_device(GDBusProxy *proxy);

extern void register_app(GDBusProxy *proxy);
extern void unregister_app(GDBusProxy *proxy);

extern void a2dp_sink_proxy_removed(GDBusProxy *proxy, void *user_data);
extern void a2dp_sink_proxy_added(GDBusProxy *proxy, void *user_data);
extern void a2dp_sink_property_changed(GDBusProxy *proxy, const char *name, DBusMessageIter *iter, void *user_data);
//extern void adapter_changed(GDBusProxy *proxy, DBusMessageIter *iter, void *user_data);
extern void device_changed(GDBusProxy *proxy, DBusMessageIter *iter, void *user_data);
static void generic_callback(const DBusError *error, void *user_data);

__attribute__((visibility("default")))
int bt_get_dev_info(struct remote_dev *pdev, char *t_addr)
{
	DBusMessageIter iter;
	struct remote_dev *rdev;
	GList *list;
	GDBusProxy *proxy;
	const char *type;
	const char *address;
	int ret = -1;

	if ((default_ctrl == NULL) || (t_addr == NULL))
		return -1;

	if (strlen(t_addr) != 17) {
		pr_err("t_addr is invaild!\n");
		ret = -1;
		goto fail;
	}
	printf("Device %s\n", t_addr);

	pthread_mutex_lock(&mutex);
	for (list = default_ctrl->app_devs; list;
					list = g_list_next(list)) {
		rdev = (struct remote_dev*)list->data;

		proxy = (GDBusProxy *)rdev->data;

		if (g_dbus_proxy_get_property(proxy, "Address", &iter) == TRUE) {

			dbus_message_iter_get_basic(&iter, &address);
		}
		pr_info("Device %s, tmp: %s\n", t_addr, address);

		if (strcmp(rdev->remote_address, t_addr) != 0)
			continue;

		if (g_dbus_proxy_get_property(proxy, "AddressType", &iter) == TRUE) {
		
			dbus_message_iter_get_basic(&iter, &type);
			strcpy(rdev->remote_address_type, (char *)type);
		}
		pr_info("Device %s (%s)\n", address, type);

		if (g_dbus_proxy_get_property(proxy, "Name", &iter) == TRUE) {
			const char *Name;
			dbus_message_iter_get_basic(&iter, &Name);
			strcpy(rdev->remote_name, (char *)Name);
			pr_info("Name %s\n", Name);
		}

		if (g_dbus_proxy_get_property(proxy, "Alias", &iter) == TRUE) {
			const char *alias;
			dbus_message_iter_get_basic(&iter, &alias);
			strcpy(rdev->remote_alias, (char *)alias);
			pr_info("Alias %s\n", alias);
		}

		if (g_dbus_proxy_get_property(proxy, "Class", &iter) == TRUE) {
			uint32_t valu32;
			dbus_message_iter_get_basic(&iter, &valu32);
			rdev->cod = valu32;
			pr_info("Class 0x%x\n", valu32);
		}

		if (g_dbus_proxy_get_property(proxy, "Appearance", &iter) == TRUE) {
			uint16_t valu16;
			dbus_message_iter_get_basic(&iter, &valu16);
			rdev->appearance = valu16;
			pr_info("Appearance 0x%x\n", valu16);
		}

		if (g_dbus_proxy_get_property(proxy, "RSSI", &iter) == TRUE) {
			int16_t valu16;
			dbus_message_iter_get_basic(&iter, &valu16);
			rdev->rssi = valu16;
			pr_info("RSSI %d\n", valu16);
		}

		if (g_dbus_proxy_get_property(proxy, "Icon", &iter) == TRUE) {
			const char *Icon;
			dbus_message_iter_get_basic(&iter, &Icon);
			strcpy(rdev->icon, (char *)Icon);
			pr_info("Icon %s\n", Icon);
		}

		if (g_dbus_proxy_get_property(proxy, "Paired", &iter) == TRUE) {
			bool valbool;
			dbus_message_iter_get_basic(&iter, &valbool);
			rdev->paired = valbool;
			pr_info("Paired %d\n", valbool);
		}

		if (g_dbus_proxy_get_property(proxy, "Connected", &iter) == TRUE) {
			bool valbool;
			dbus_message_iter_get_basic(&iter, &valbool);
			rdev->connected = valbool;
			pr_info("Connected %d\n", valbool);
		}

		if (g_dbus_proxy_get_property(proxy, "AdvertisingFlags", &iter)) {
			DBusMessageIter array;
			uint8_t *flags;
			int flags_len = 0;

			dbus_message_iter_recurse(&iter, &array);
			dbus_message_iter_get_fixed_array(&array, &flags, &flags_len);

			rdev->flags = flags[0];
			pr_info("AdvertisingFlags 0x%x\n", flags[0]);
		}

		if (g_dbus_proxy_get_property(proxy, "AdvertisingData", &iter)) {
			DBusMessageIter subiter;
			//case DBUS_TYPE_ARRAY:
			dbus_message_iter_recurse(&iter, &subiter);

			if (dbus_type_is_fixed(
					dbus_message_iter_get_arg_type(&subiter))) {
				print_fixed_iter("AD", "AdvertisingData", &subiter);
			}
		
			while (dbus_message_iter_get_arg_type(&subiter) !=
								DBUS_TYPE_INVALID) {
				print_iter("AD", "AdvertisingData", &subiter);
				dbus_message_iter_next(&subiter);
			}
		}

		if (g_dbus_proxy_get_property(proxy, "ManufacturerData", &iter)) {
			DBusMessageIter subiter;
			//case DBUS_TYPE_ARRAY:
			dbus_message_iter_recurse(&iter, &subiter);

			if (dbus_type_is_fixed(
					dbus_message_iter_get_arg_type(&subiter))) {
				print_fixed_iter("AD FIXED ", "ManufacturerData", &subiter);
			}
		
			while (dbus_message_iter_get_arg_type(&subiter) !=
								DBUS_TYPE_INVALID) {
				print_iter("AD ", "ManufacturerData", &subiter);
				dbus_message_iter_next(&subiter);
			}
		}

		if (g_dbus_proxy_get_property(proxy, "UUIDs", &iter)) {
			DBusMessageIter value;
			dbus_message_iter_recurse(&iter, &value);
		
			int index = 0;
			while (dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_STRING) {
				const char *uuid;
		
				dbus_message_iter_get_basic(&value, &uuid);
		
				strcpy(rdev->remote_uuids[index++], uuid);
				print_uuid(uuid);
		
				dbus_message_iter_next(&value);
			}
			strcpy(rdev->remote_uuids[index], "NULL");
		}

		//TODO MEDIA TRANSPORT ENDPORINT
		memcpy(pdev, rdev, sizeof(struct remote_dev));
		ret = 0;
		break;
	}

fail:
	pthread_mutex_unlock(&mutex);

	return ret;
}

__attribute__((visibility("default")))
int bt_get_devices(struct remote_dev **scan_list, int *len)
{
	int i = 0;
	struct remote_dev *dev_tmp = NULL;
	GList *list, *devs;

	if (default_ctrl == NULL)
		return -1;

	devs = default_ctrl->app_devs;

	pthread_mutex_lock(&mutex);

	for (list = devs; list; list = g_list_next(list)) {
		dev_tmp = (struct remote_dev *)list->data;
		if (dev_tmp) {
			memcpy(&g_scan_array[i], dev_tmp, sizeof(struct remote_dev));
			i++;

			pr_info("%s Device %s (%s:%s:%d)\n", dev_tmp->paired ? "Paired" : "Scaned",
				dev_tmp->remote_address,
				dev_tmp->remote_address_type,
				dev_tmp->remote_alias, i);
		}
	}

	*len = i;
	*scan_list = g_scan_array;

	pr_info("bt_get_devices %d:%d\n", i, *len);

	pthread_mutex_unlock(&mutex);

	return 0;
}

void bt_state_send(struct remote_dev *rdev, RK_BT_STATE state)
{
	if (g_bt_callback.bt_state_cb)
		g_bt_callback.bt_state_cb(rdev, state);
}

void bt_state_change(GDBusProxy *proxy, const char *change, RK_BT_STATE new_state)
{
	DBusMessageIter iter, value;
	GList *list;
	bool paired, connected, bonded;
	struct remote_dev *rdev;
	RK_BT_STATE state = RK_BT_STATE_SCAN_CHG_REMOTE_DEV;
	bool found = false;

	pr_info("[CHG]: %s\n", change);

	if (proxy == NULL) {
		pr_info("[CHG]: %s, NO PROXY ARREST!!!\n", change);
		return;
	}

	for (list = default_ctrl->app_devs; list;
					list = g_list_next(list)) {
		rdev = (struct remote_dev*)list->data;

		//pr_info("[DBG]: %p:%p\n", (GDBusProxy *)rdev->data, proxy);
		if ((GDBusProxy *)rdev->data == proxy) {
			found = true;
			break;
		}
	}

	if (found == false) {
		pr_info("proxy %s not found!\n", g_dbus_proxy_get_path(proxy));
		return;
	}

	//prefix
	if (g_dbus_proxy_get_property(proxy, "Address", &iter) == TRUE) {
		const char *type, *alias;
		const char *address;
		int16_t rssi;

		dbus_message_iter_get_basic(&iter, &address);
		strcpy(rdev->remote_address, (char *)address);

		if (g_dbus_proxy_get_property(proxy, "AddressType", &iter) == TRUE) {

			dbus_message_iter_get_basic(&iter, &type);
			strcpy(rdev->remote_address_type, (char *)type);
		}
		if (g_dbus_proxy_get_property(proxy, "Alias", &iter) == TRUE) {

			dbus_message_iter_get_basic(&iter, &alias);
			strcpy(rdev->remote_alias, (char *)alias);
		}
		if (g_dbus_proxy_get_property(proxy, "RSSI", &iter) == TRUE) {
			dbus_message_iter_get_basic(&iter, &rssi);
			rdev->rssi = rssi;
		}
		pr_info("Device %s (%s:%s:%d)\n", address, type, alias, rssi);
	}

	if ((strcmp(change, "UUIDs") == 0) && 
			g_dbus_proxy_get_property(proxy, change, &iter)) {

		dbus_message_iter_recurse(&iter, &value);

		int index = 0;
		while (dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_STRING) {
			const char *uuid;

			dbus_message_iter_get_basic(&value, &uuid);

			strcpy(rdev->remote_uuids[index++], uuid);
			print_uuid(uuid);

			dbus_message_iter_next(&value);
		}
		strcpy(rdev->remote_uuids[index], "NULL");
	}

	if ((strcmp(change, "Modalias") == 0) && 
			g_dbus_proxy_get_property(proxy, change, &iter)) {
		const char *str;
		dbus_message_iter_get_basic(&iter, &str);
		strcpy(rdev->modalias, (char *)str);
	}

	if ((strcmp(change, "Class") == 0) && 
			g_dbus_proxy_get_property(proxy, change, &iter)) {
		uint32_t cod;
		dbus_message_iter_get_basic(&iter, &cod);
		rdev->cod = cod;
	}

	if ((strcmp(change, "Icon") == 0) && 
			g_dbus_proxy_get_property(proxy, change, &iter)) {
		const char *str;
		dbus_message_iter_get_basic(&iter, &str);
		strcpy(rdev->icon, (char *)str);
	}

	if ((strcmp(change, "Paired") == 0) && 
			g_dbus_proxy_get_property(proxy, change, &iter)) {
		dbus_message_iter_get_basic(&iter, &paired);
		rdev->paired = paired;
		state = paired ? RK_BT_STATE_PAIRED : RK_BT_STATE_PAIR_NONE;
	}

	if ((strcmp(change, "Bonded") == 0) && 
			g_dbus_proxy_get_property(proxy, change, &iter)) {
		dbus_message_iter_get_basic(&iter, &bonded);
		rdev->bonded = bonded;
		state = bonded ? RK_BT_STATE_BONDED : RK_BT_STATE_BOND_NONE;
	}

	if ((strcmp(change, "Connected") == 0) && 
			g_dbus_proxy_get_property(proxy, change, &iter)) {
		dbus_message_iter_get_basic(&iter, &connected);
		rdev->connected = connected;
		state = connected ? RK_BT_STATE_CONNECTED : RK_BT_STATE_DISCONN;
	}

	if ((state == RK_BT_STATE_SCAN_CHG_REMOTE_DEV) &&
		(new_state != RK_BT_STATE_NONE)) {
		state = new_state;
	}

	strcpy(rdev->change_name, change);

	pr_info("change name: %s\n", rdev->change_name);
	bt_state_send(rdev, state);
}
#define MIN(a,b) (a<b?a:b)
void bt_media_state_change(char *path, const char *change, RK_BT_STATE new_state, void *data)
{
	GDBusProxy *rp = NULL;
	GList *list;
	struct remote_dev *rdev;

	if (!default_ctrl) {
		pr_info("bt_media_state_change %p\n", default_ctrl);
		return;
	}
	//find remote dev
	for (list = default_ctrl->app_devs; list;
					list = g_list_next(list)) {
		rdev = (struct remote_dev*)list->data;
		if (!rdev)
			return;
			
		pr_info("media dbg: [%s:%s]\n", rdev->dev_path, path);
		if (!strcmp(rdev->dev_path, path)) {
			rp = (GDBusProxy *)rdev->data;
			break;
		}
	}

	if (!rp) {
		pr_info("not found media dev\n");
		return;
	}

	//update player status
	if (strcmp(change, "player") == 0)
		rdev->player_state = new_state;

	//update SBC Configure status
	if (strcmp(change, "Media") == 0) {
		memcpy(&rdev->media, (uint8_t *)data, sizeof(RkBtMedia));
	}
	if (strcmp(change, "position") == 0) {
		struct _song_lens{
			unsigned int position;
			unsigned int total;
		};
		rdev->player_position = ((struct _song_lens *)data)->position;
		rdev->player_total_len = ((struct _song_lens *)data)->total;
	}
	if (strcmp(change, "track") == 0) {
		BtTrackInfo* track = (BtTrackInfo*) data;
		if(track) {
			memset(&rdev->title[0], 0, MAX_NAME_LENGTH + 1);
			memset(&rdev->artist[0], 0, MAX_NAME_LENGTH + 1);
			strncpy(&rdev->title[0], (char*)(track->title), MIN(strlen(track->title), MAX_NAME_LENGTH));
			strncpy(&rdev->artist[0], (char*)(track->artist), MIN(strlen(track->artist), MAX_NAME_LENGTH));
		}
	}

	bt_state_change(rp, change, new_state);
}

void bt_register_state_callback(RK_BT_STATE_CALLBACK cb)
{
	g_bt_callback.bt_state_cb = cb;
}

void bt_deregister_state_callback()
{
	g_bt_callback.bt_state_cb = NULL;
}

static void proxy_leak(gpointer data)
{
	pr_info("Leaking proxy %p\n", data);
}

static void connect_handler(DBusConnection *connection, void *user_data)
{
	pr_info("connect_handler %p\n", user_data);
}

static void disconnect_handler(DBusConnection *connection, void *user_data)
{
	g_list_free_full(ctrl_list, proxy_leak);
	ctrl_list = NULL;
	default_ctrl = NULL;
	pr_info("disconnect_handler\n");
}

static void print_adapter(GDBusProxy *proxy, const char *description)
{
	DBusMessageIter iter;
	const char *address, *name;

	if (g_dbus_proxy_get_property(proxy, "Address", &iter) == FALSE)
		return;

	dbus_message_iter_get_basic(&iter, &address);

	if (g_dbus_proxy_get_property(proxy, "Alias", &iter) == TRUE)
		dbus_message_iter_get_basic(&iter, &name);
	else
		name = "<unknown>";

	pr_info("Controller %s %s %s\n",
				address, name,
				default_ctrl &&
				default_ctrl->proxy == proxy ?
				"[default]" : "");
}

static void print_device(GDBusProxy *proxy, const char *description)
{
	DBusMessageIter iter;
	const char *address, *name;

	if (g_dbus_proxy_get_property(proxy, "Address", &iter) == FALSE) {
		pr_info("%s: can't get Address\n", __func__);
		return;
	}

	dbus_message_iter_get_basic(&iter, &address);

	if (g_dbus_proxy_get_property(proxy, "Alias", &iter) == TRUE)
		dbus_message_iter_get_basic(&iter, &name);
	else
		name = "<unknown>";

	//printf("Device %s %s\n", address, name);
}

void print_fixed_iter(const char *label, const char *name,
						DBusMessageIter *iter)
{
	dbus_bool_t *valbool;
	dbus_uint32_t *valu32;
	dbus_uint16_t *valu16;
	dbus_int16_t *vals16;
	unsigned char *byte;
	int len;

	switch (dbus_message_iter_get_arg_type(iter)) {
	case DBUS_TYPE_BOOLEAN:
		dbus_message_iter_get_fixed_array(iter, &valbool, &len);

		if (len <= 0)
			return;

		pr_info("DBUS_TYPE_BOOLEAN %s%s:\n", label, name);
		bt_shell_hexdump((void *)valbool, len * sizeof(*valbool));

		break;
	case DBUS_TYPE_UINT32:
		dbus_message_iter_get_fixed_array(iter, &valu32, &len);

		if (len <= 0)
			return;

		pr_info("DBUS_TYPE_UINT32 %s%s:\n", label, name);
		bt_shell_hexdump((void *)valu32, len * sizeof(*valu32));

		break;
	case DBUS_TYPE_UINT16:
		dbus_message_iter_get_fixed_array(iter, &valu16, &len);

		if (len <= 0)
			return;

		pr_info("DBUS_TYPE_UINT16 %s%s:\n", label, name);
		bt_shell_hexdump((void *)valu16, len * sizeof(*valu16));

		break;
	case DBUS_TYPE_INT16:
		dbus_message_iter_get_fixed_array(iter, &vals16, &len);

		if (len <= 0)
			return;

		pr_info("DBUS_TYPE_INT16 %s%s:\n", label, name);
		bt_shell_hexdump((void *)vals16, len * sizeof(*vals16));

		break;
	case DBUS_TYPE_BYTE:
		dbus_message_iter_get_fixed_array(iter, &byte, &len);

		if (len <= 0)
			return;

		pr_info("DBUS_TYPE_BYTE %s%s:\n", label, name);
		bt_shell_hexdump((void *)byte, len * sizeof(*byte));

		break;
	default:
		return;
	};
}

void print_iter(const char *label, const char *name,
						DBusMessageIter *iter)
{
	dbus_bool_t valbool;
	dbus_uint32_t valu32;
	dbus_uint16_t valu16;
	dbus_int16_t vals16;
	unsigned char byte;
	const char *valstr;
	DBusMessageIter subiter;
	char *entry;

	if (iter == NULL) {
		pr_info("%s%s is nil\n", label, name);
		return;
	}

	switch (dbus_message_iter_get_arg_type(iter)) {
	case DBUS_TYPE_INVALID:
		pr_info("DBUS_TYPE_INVALID %s%s is invalid\n", label, name);
		break;
	case DBUS_TYPE_STRING:
	case DBUS_TYPE_OBJECT_PATH:
		dbus_message_iter_get_basic(iter, &valstr);
		pr_info("DBUS_TYPE_STRING/DBUS_TYPE_OBJECT_PATH %s%s: %s\n", label, name, valstr);
		if (!strncmp(name, "Status", 6)) {
			if (strstr(valstr, "playing"))
				;//report_avrcp_event(BT_EVENT_START_PLAY, NULL, 0);
			else if (strstr(valstr, "paused"))
				;//report_avrcp_event(BT_EVENT_PAUSE_PLAY, NULL, 0);
			else if (strstr(valstr, "stopped"))
				;//report_avrcp_event(BT_EVENT_STOP_PLAY, NULL, 0);
		}
		break;
	case DBUS_TYPE_BOOLEAN:
		dbus_message_iter_get_basic(iter, &valbool);
		pr_info("DBUS_TYPE_BOOLEAN %s%s: %s\n", label, name,
					valbool == TRUE ? "yes" : "no");
		break;
	case DBUS_TYPE_UINT32:
		dbus_message_iter_get_basic(iter, &valu32);
		pr_info("DBUS_TYPE_UINT32 %s%s: 0x%08x\n", label, name, valu32);
		break;
	case DBUS_TYPE_UINT16:
		dbus_message_iter_get_basic(iter, &valu16);
		pr_info("DBUS_TYPE_UINT16 %s%s: 0x%04x\n", label, name, valu16);
		break;
	case DBUS_TYPE_INT16:
		dbus_message_iter_get_basic(iter, &vals16);
		pr_info("DBUS_TYPE_INT16 %s%s: %d\n", label, name, vals16);
		break;
	case DBUS_TYPE_BYTE:
		dbus_message_iter_get_basic(iter, &byte);
		pr_info("DBUS_TYPE_BYTE %s%s: 0x%02x\n", label, name, byte);
		break;
	case DBUS_TYPE_VARIANT:
		pr_info("DBUS_TYPE_VARIANT %s%s\n", label, name);
		dbus_message_iter_recurse(iter, &subiter);
		print_iter(label, name, &subiter);
		break;
	case DBUS_TYPE_ARRAY:
		pr_info("DBUS_TYPE_ARRAY %s%s\n", label, name);
		dbus_message_iter_recurse(iter, &subiter);

		if (dbus_type_is_fixed(
				dbus_message_iter_get_arg_type(&subiter))) {
			pr_info("%s%s is dbus_type_is_fixed\n", label, name);
			print_fixed_iter(label, name, &subiter);
			break;
		}

		while (dbus_message_iter_get_arg_type(&subiter) !=
							DBUS_TYPE_INVALID) {
			print_iter(label, name, &subiter);
			dbus_message_iter_next(&subiter);
		}
		break;
	case DBUS_TYPE_DICT_ENTRY:
		pr_info("DBUS_TYPE_DICT_ENTRY %s%s\n", label, name);
		dbus_message_iter_recurse(iter, &subiter);
		entry = g_strconcat(name, " Key", NULL);
		print_iter(label, entry, &subiter);
		g_free(entry);

		entry = g_strconcat(name, " Value", NULL);
		dbus_message_iter_next(&subiter);
		print_iter(label, entry, &subiter);
		g_free(entry);
		break;
	default:
		pr_info("%s%s has unsupported type\n", label, name);
		break;
	}
}

void print_property(GDBusProxy *proxy, const char *name)
{
	DBusMessageIter iter;

	if (g_dbus_proxy_get_property(proxy, name, &iter) == FALSE)
		return;

	print_iter("\t", name, &iter);
}

static void print_uuid(const char *uuid)
{
	const char *text;

	text = bt_uuidstr_to_str(uuid);
	if (text) {
		char str[26];
		unsigned int n;

		str[sizeof(str) - 1] = '\0';

		n = snprintf(str, sizeof(str), "%s", text);
		if (n > sizeof(str) - 1) {
			str[sizeof(str) - 2] = '.';
			str[sizeof(str) - 3] = '.';
			if (str[sizeof(str) - 4] == ' ')
				str[sizeof(str) - 4] = '.';

			n = sizeof(str) - 1;
		}

		pr_info("\tUUID: %s%*c(%s)\n", str, 26 - n, ' ', uuid);
	} else
		pr_info("\tUUID: %*c(%s)\n", 26, ' ', uuid);
}

static void print_uuids(GDBusProxy *proxy)
{
	DBusMessageIter iter, value;

	if (g_dbus_proxy_get_property(proxy, "UUIDs", &iter) == FALSE)
		return;

	dbus_message_iter_recurse(&iter, &value);

	while (dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_STRING) {
		const char *uuid;

		dbus_message_iter_get_basic(&value, &uuid);

		print_uuid(uuid);

		dbus_message_iter_next(&value);
	}
}

static gboolean device_is_child(GDBusProxy *device, GDBusProxy *master)
{
	DBusMessageIter iter;
	const char *adapter, *path;

	if (!master)
		return FALSE;

	if (g_dbus_proxy_get_property(device, "Adapter", &iter) == FALSE)
		return FALSE;

	dbus_message_iter_get_basic(&iter, &adapter);
	path = g_dbus_proxy_get_path(master);

	if (!strcmp(path, adapter))
		return TRUE;

	return FALSE;
}

static GDBusProxy * service_is_child(GDBusProxy *service)
{
	DBusMessageIter iter;
	const char *device;

	if (g_dbus_proxy_get_property(service, "Device", &iter) == FALSE)
		return NULL;

	dbus_message_iter_get_basic(&iter, &device);

	if (!default_ctrl)
		return NULL;

	return g_dbus_proxy_lookup(default_ctrl->devices, NULL, device,
					"org.bluez.Device1");
}

static struct adapter *find_parent(GDBusProxy *device)
{
	GList *list;

	for (list = g_list_first(ctrl_list); list; list = g_list_next(list)) {
		struct adapter *adapter = (struct adapter *)list->data;

		if (device_is_child(device, adapter->proxy) == TRUE)
			return adapter;
	}
	return NULL;
}

static void set_default_device(GDBusProxy *proxy, const char *attribute)
{
	char *desc = NULL;
	DBusMessageIter iter;
	const char *path;

	pr_info("%s: default_dev %p, proxy: %p\n", __func__, default_dev, proxy);

	if ((proxy != NULL) && (default_dev != NULL) && (default_dev != proxy)) {
		pr_info("check proxy: ref: %d:%d\n", *((unsigned int *)proxy), *((unsigned int *)default_dev));
		return;
	}

	default_dev = proxy;

	if (proxy == NULL) {
		default_attr = NULL;
		goto done;
	}

	if (!g_dbus_proxy_get_property(proxy, "Alias", &iter)) {
		if (!g_dbus_proxy_get_property(proxy, "Address", &iter))
			goto done;
	}

	path = g_dbus_proxy_get_path(proxy);

	dbus_message_iter_get_basic(&iter, &desc);
	desc = g_strdup_printf(COLOR_BLUE "[%s%s%s]" COLOR_OFF "# ", desc,
				attribute ? ":" : "",
				attribute ? attribute + strlen(path) : "");
done:
	bt_shell_set_prompt(desc ? desc : PROMPT_ON);
	g_free(desc);
}

static void device_added(GDBusProxy *proxy)
{
	DBusMessageIter iter;
	struct adapter *adapter = find_parent(proxy);
	struct remote_dev *rdev;

	if (!adapter) {
		/* TODO: Error */
		return;
	}

	pthread_mutex_lock(&mutex);

	adapter->devices = g_list_append(adapter->devices, proxy);

	/*
	 * SCAN ADD DEV
	 */
	rdev = (struct remote_dev *)g_malloc0(sizeof(struct remote_dev));
	rdev->data = (void *)proxy;
	adapter->app_devs = g_list_append(adapter->app_devs, rdev);

	//device path
	strcpy(rdev->dev_path, g_dbus_proxy_get_path(proxy));

	//base info
	if (g_dbus_proxy_get_property(proxy, "Address", &iter) == TRUE) {
		const char *type, *alias, *name;
		const char *address;
		int16_t rssi;
		dbus_bool_t paired = FALSE;

		dbus_message_iter_get_basic(&iter, &address);
		strcpy(rdev->remote_address, (char *)address);

		if (g_dbus_proxy_get_property(proxy, "AddressType", &iter) == TRUE) {

			dbus_message_iter_get_basic(&iter, &type);
			strcpy(rdev->remote_address_type, (char *)type);
		}
		if (g_dbus_proxy_get_property(proxy, "Alias", &iter) == TRUE) {

			dbus_message_iter_get_basic(&iter, &alias);
			strcpy(rdev->remote_alias, (char *)alias);
		}
		if (g_dbus_proxy_get_property(proxy, "Name", &iter) == TRUE) {

			dbus_message_iter_get_basic(&iter, &name);
			strcpy(rdev->remote_name, (char *)name);
		}
		if (g_dbus_proxy_get_property(proxy, "RSSI", &iter) == TRUE) {

			dbus_message_iter_get_basic(&iter, &rssi);
			rdev->rssi = rssi;
		}

		if (g_dbus_proxy_get_property(proxy, "Paired", &iter)) {
			dbus_message_iter_get_basic(&iter, &paired);
			rdev->paired = paired;
		}

		strcpy(rdev->obj_path, g_dbus_proxy_get_path(proxy));
		pr_info("Device %s (%s:%s:%d)\n", address, type, alias, rssi);
	}

	pthread_mutex_unlock(&mutex);

	//if (default_dev)
	//	return;

	if (g_dbus_proxy_get_property(proxy, "Connected", &iter)) {
		dbus_bool_t connected;

		dbus_message_iter_get_basic(&iter, &connected);

		if (connected && !default_dev)
			set_default_device(proxy, NULL);

		if (connected && !default_dev) {
			//callback
			rdev->connected = connected;
			strcpy(rdev->change_name, "Connected");
			bt_state_send(rdev, RK_BT_STATE_CONNECTED);
		} else
			bt_state_send(rdev, RK_BT_STATE_SCAN_NEW_REMOTE_DEV);
	}
}

static struct adapter *find_ctrl(GList *source, const char *path);

static struct adapter *adapter_new(GDBusProxy *proxy)
{
	struct adapter *adapter = g_malloc0(sizeof(struct adapter));

	ctrl_list = g_list_append(ctrl_list, adapter);

	if (!default_ctrl)
		default_ctrl = adapter;

	return adapter;
}

static void adapter_added(GDBusProxy *proxy)
{
	struct adapter *adapter;

	pr_info("=== %s ===\n", __func__);

	adapter = find_ctrl(ctrl_list, g_dbus_proxy_get_path(proxy));
	if (!adapter)
		adapter = adapter_new(proxy);

	adapter->proxy = proxy;
	print_adapter(proxy, COLORED_NEW);
}

static void ad_manager_added(GDBusProxy *proxy)
{
	struct adapter *adapter;
	adapter = find_ctrl(ctrl_list, g_dbus_proxy_get_path(proxy));
	if (!adapter)
		adapter = adapter_new(proxy);

	adapter->ad_proxy = proxy;
}

static void proxy_added(GDBusProxy *proxy, void *user_data)
{
	const char *interface;

	interface = g_dbus_proxy_get_interface(proxy);
	pr_info("[proxy_added Enter] iface: %s, path: %s\n", interface, g_dbus_proxy_get_path(proxy));

	if (!strcmp(interface, "org.bluez.Device1")) {
		device_added(proxy);
	} else if (!strcmp(interface, "org.bluez.Adapter1")) {
		adapter_added(proxy);
		//set bredr name
		pr_debug("default_ctrl: %p, g_rkbt_content: %p\n", default_ctrl, g_rkbt_content);
		if (default_ctrl && g_rkbt_content)
			g_dbus_proxy_set_property_basic(default_ctrl->proxy, "Alias",
						DBUS_TYPE_STRING, &g_rkbt_content->bt_name,
						generic_callback, (void *)g_rkbt_content->bt_name, NULL);
	} else if (!strcmp(interface, "org.bluez.AgentManager1")) {
		if (!agent_manager) {
			agent_manager = proxy;
			agent_register(dbus_conn, agent_manager,
						auto_register_agent);
		}
	} else if (!strcmp(interface, "org.bluez.GattService1")) {
		if (service_is_child(proxy))
			gatt_add_service(proxy);
	} else if (!strcmp(interface, "org.bluez.GattCharacteristic1")) {
		gatt_add_characteristic(proxy);
	} else if (!strcmp(interface, "org.bluez.GattDescriptor1")) {
		gatt_add_descriptor(proxy);
	} else if (!strcmp(interface, "org.bluez.GattManager1")) {
		gatt_add_manager(proxy);
		register_app(proxy);
	} else if (!strcmp(interface, "org.bluez.LEAdvertisingManager1")) {
		ad_manager_added(proxy);
	} else
		a2dp_sink_proxy_added(proxy, user_data);

	pr_info("[proxy_added Exit] iface: %s\n", interface);
}

void set_default_attribute(GDBusProxy *proxy)
{
	const char *path;

	//default_local_attr = NULL;
	default_attr = proxy;

	path = g_dbus_proxy_get_path(proxy);

	set_default_device(default_dev, path);
}

static void device_removed(GDBusProxy *proxy)
{
	DBusMessageIter iter;

	pr_info("%s: path: %s\n", __func__, g_dbus_proxy_get_path(proxy));
	struct adapter *adapter = (struct adapter *)find_parent(proxy);
	if (!adapter) {
		pr_info("%s: adapter is NULL\n", __func__);
		/* TODO: Error */
		return;
	}

	pthread_mutex_lock(&mutex);
	adapter->devices = g_list_remove(adapter->devices, proxy);

	if (g_dbus_proxy_get_property(proxy, "Address", &iter) == TRUE) {
		const char *addr;
		GList *list;
		struct remote_dev *rdev = NULL;

		dbus_message_iter_get_basic(&iter, &addr);

		for (list = adapter->app_devs; list;
						list = g_list_next(list)) {
			rdev = (struct remote_dev *)list->data;
			if (strncmp(rdev->remote_address, addr, 17) == 0)
				break;
		}

		if (rdev) {
			if (rdev->paired)
				bt_state_send(rdev, RK_BT_STATE_DEL_DEV_OK);
			else
				bt_state_send(rdev, RK_BT_STATE_SCAN_DEL_REMOTE_DEV);
			adapter->app_devs = g_list_remove(adapter->app_devs, rdev);
			g_free(rdev);
		}
	}
	pthread_mutex_unlock(&mutex);

	print_device(proxy, COLORED_DEL);
	bt_shell_set_env(g_dbus_proxy_get_path(proxy), NULL);

	if (default_dev == proxy)
		set_default_device(NULL, NULL);
}

static void adapter_removed(GDBusProxy *proxy)
{
	GList *ll;
	pthread_mutex_lock(&mutex);
	pr_info("adapter_removed\n");

	for (ll = g_list_first(ctrl_list); ll; ll = g_list_next(ll)) {
		struct adapter *adapter = (struct adapter *)ll->data;

		if (adapter->proxy == proxy) {
			print_adapter(proxy, COLORED_DEL);
			bt_shell_set_env(g_dbus_proxy_get_path(proxy), NULL);

			if (default_ctrl && default_ctrl->proxy == proxy) {
				default_ctrl = NULL;
				set_default_device(NULL, NULL);
			}

			ctrl_list = g_list_remove_link(ctrl_list, ll);
			g_list_free(adapter->devices);
			g_list_free(adapter->app_devs);
			g_free(adapter);
			g_list_free(ll);
			pthread_mutex_unlock(&mutex);
			return;
		}
	}

	pthread_mutex_unlock(&mutex);
}

static void proxy_removed(GDBusProxy *proxy, void *user_data)
{
	const char *interface;

	interface = g_dbus_proxy_get_interface(proxy);
	pr_info("[proxy_removed Enter] iface: %s, path: %s\n", interface, g_dbus_proxy_get_path(proxy));

	if (!strcmp(interface, "org.bluez.Device1")) {
		device_removed(proxy);
	} else if (!strcmp(interface, "org.bluez.Adapter1")) {
		adapter_removed(proxy);
	} else if (!strcmp(interface, "org.bluez.AgentManager1")) {
		if (agent_manager == proxy) {
			agent_manager = NULL;
			if (auto_register_agent) {
				agent_unregister(dbus_conn, NULL);
			}
		}
	} else if (!strcmp(interface, "org.bluez.GattService1")) {
		gatt_remove_service(proxy);

		if (default_attr == proxy)
			set_default_attribute(NULL);

		//le_proxy_removed(proxy);
	} else if (!strcmp(interface, "org.bluez.GattCharacteristic1")) {
		gatt_remove_characteristic(proxy);

		if (default_attr == proxy)
			set_default_attribute(NULL);
	} else if (!strcmp(interface, "org.bluez.GattDescriptor1")) {
		gatt_remove_descriptor(proxy);

		if (default_attr == proxy)
			set_default_attribute(NULL);
	} else if (!strcmp(interface, "org.bluez.GattManager1")) {
		gatt_remove_manager(proxy);
		unregister_app(proxy);
	} else if (!strcmp(interface, "org.bluez.LEAdvertisingManager1")) {
		ad_unregister(dbus_conn, NULL);
	} else 
		a2dp_sink_proxy_removed(proxy, user_data);

	pr_info("[proxy_removed Exit] iface: %s\n", interface);
}

static struct adapter *find_ctrl(GList *source, const char *path)
{
	GList *list;

	for (list = g_list_first(source); list; list = g_list_next(list)) {
		struct adapter *adapter = (struct adapter *)list->data;

		if (!strcasecmp(g_dbus_proxy_get_path(adapter->proxy), path))
			return adapter;
	}

	return NULL;
}

static void property_changed(GDBusProxy *proxy, const char *name,
					DBusMessageIter *iter, void *user_data)
{
	const char *interface;
	struct adapter *ctrl;

	interface = g_dbus_proxy_get_interface(proxy);

	pr_info("[%s Enter] iface: %s, path: %s, name: %s\n", __func__,
		interface, g_dbus_proxy_get_path(proxy), name);

	if (!strcmp(interface, "org.bluez.Device1")) {
		if (default_ctrl && device_is_child(proxy,
					default_ctrl->proxy) == TRUE) {
			DBusMessageIter addr_iter;
			char *str;

			if (g_dbus_proxy_get_property(proxy, "Address",
							&addr_iter) == TRUE) {
				const char *address;

				dbus_message_iter_get_basic(&addr_iter,
								&address);
				str = g_strdup_printf("Device %s ", address);
			} else
				str = g_strdup("");

			if (strcmp(name, "Connected") == 0) {
				dbus_bool_t connected;

				dbus_message_iter_get_basic(iter, &connected);

				if (connected && default_dev == NULL)
					set_default_device(proxy, NULL);
				else if (!connected && default_dev == proxy)
					set_default_device(NULL, NULL);
			}

			//callback
			bt_state_change(proxy, name, RK_BT_STATE_NONE);

			//printf("type: %d\n", dbus_message_iter_get_arg_type(iter));
			print_iter(str, name, iter);
			g_free(str);
		}
	} else if (!strcmp(interface, "org.bluez.Adapter1")) {
		DBusMessageIter addr_iter;
		char *str;

		if (g_dbus_proxy_get_property(proxy, "Address",
						&addr_iter) == TRUE) {
			const char *address;

			dbus_message_iter_get_basic(&addr_iter, &address);
			str = g_strdup_printf("Controller %s ", address);
		} else
			str = g_strdup("");

		//if (!strcmp(name, "Powered"))
		//	adapter_changed(proxy, iter, user_data);

		if (!strcmp(name, "Discovering")) {
			dbus_bool_t val;
			dbus_message_iter_get_basic(iter, &val);
			pr_info("Adapter SCANNING changed to %s", val ? "TRUE" : "FALSE");
			if (!val) {
				filter_clear_transport();
				//g_bt_scan_info.is_scaning = false;
				//bt_discovery_state_send(RK_BT_DISC_STOPPED_BY_USER);
			}
			if (val) {
				bt_state_send(NULL, RK_BT_STATE_ADAPTER_SCANNING);
				g_rkbt_content->scanning = true;
			} else {
				bt_state_send(NULL, RK_BT_STATE_ADAPTER_NO_SCANNING);
				g_rkbt_content->scanning = false;
			}
		}

		//Controller 10:2C:6B:76:B4:2F Discoverable: yes
		if (!strcmp(name, "Discoverable")) {
			dbus_bool_t val;
			dbus_message_iter_get_basic(iter, &val);
			pr_info("Adapter Discoverable changed to %s", val ? "TRUE" : "FALSE");
			if (val)
				bt_state_send(NULL, RK_BT_STATE_ADAPTER_DISCOVERYABLED);
			else
				bt_state_send(NULL, RK_BT_STATE_ADAPTER_NO_DISCOVERYABLED);
		}

		if (!strcmp(name, "Powered")) {
			dbus_bool_t val;
			dbus_message_iter_get_basic(iter, &val);
			pr_info("Adapter Powered changed to %s", val ? "TRUE" : "FALSE");
			if (val)
				bt_state_send(NULL, RK_BT_STATE_ADAPTER_POWER_ON);
			else
				bt_state_send(NULL, RK_BT_STATE_ADAPTER_POWER_OFF);
		}

		if (!strcmp(name, "Pairable")) {
			dbus_bool_t val;
			dbus_message_iter_get_basic(iter, &val);
			pr_info("Adapter Pairable changed to %s", val ? "TRUE" : "FALSE");
			if (val)
				bt_state_send(NULL, RK_BT_STATE_ADAPTER_PAIRABLED);
			else
				bt_state_send(NULL, RK_BT_STATE_ADAPTER_NO_PAIRABLED);
		}

		print_iter(str, name, iter);
		g_free(str);
	} else if (!strcmp(interface, "org.bluez.LEAdvertisingManager1")) {
		DBusMessageIter addr_iter;
		char *str;

		ctrl = find_ctrl(ctrl_list, g_dbus_proxy_get_path(proxy));
		if (!ctrl)
			return;

		if (g_dbus_proxy_get_property(ctrl->proxy, "Address",
						&addr_iter) == TRUE) {
			const char *address;

			dbus_message_iter_get_basic(&addr_iter, &address);
			str = g_strdup_printf("Controller %s ", address);
		} else
			str = g_strdup("");

		print_iter(str, name, iter);
		g_free(str);
	}else if (!strcmp(interface, "org.bluez.GattCharacteristic1")
		|| !strcmp(interface, "org.bluez.GattDescriptor1")) {
		char *str;

		str = g_strdup_printf("Attribute %s ",
						g_dbus_proxy_get_path(proxy));

		print_iter(str, name, iter);
		g_free(str);

		if (!strcmp(name, "Value"))
			gatt_client_recv_data_send(proxy, iter);
	} else
		a2dp_sink_property_changed(proxy, name, iter, user_data);

	pr_info("[%s Exit] iface: %s\n", __func__, interface);
}

static void message_handler(DBusConnection *connection,
					DBusMessage *message, void *user_data)
{
	pr_info("[SIGNAL] %s.%s\n", dbus_message_get_interface(message),
					dbus_message_get_member(message));
}

static struct adapter *find_ctrl_by_address(GList *source, const char *address)
{
	GList *list;

	for (list = g_list_first(source); list; list = g_list_next(list)) {
		struct adapter *adapter = (struct adapter *)list->data;
		DBusMessageIter iter;
		const char *str;

		if (g_dbus_proxy_get_property(adapter->proxy,
					"Address", &iter) == FALSE)
			continue;

		dbus_message_iter_get_basic(&iter, &str);

		if (!strcasecmp(str, address))
			return adapter;
	}

	return NULL;
}

static GDBusProxy *find_proxy_by_address(GList *source, const char *address)
{
	GList *list;

	for (list = g_list_first(source); list; list = g_list_next(list)) {
		GDBusProxy *proxy = (GDBusProxy *)list->data;
		DBusMessageIter iter;
		const char *str;

		if (g_dbus_proxy_get_property(proxy, "Address", &iter) == FALSE)
			continue;

		dbus_message_iter_get_basic(&iter, &str);
		if (!strcasecmp(str, address))
			return proxy;
	}

	return NULL;
}

static gboolean check_default_ctrl(void)
{
	if (!default_ctrl) {
		pr_info("%s: No default controller available\n", __func__);
		return FALSE;
	}

	return TRUE;
}

static gboolean parse_argument(int argc, char *argv[], const char **arg_table,
					const char *msg, dbus_bool_t *value,
					const char **option)
{
	const char **opt;

	if (!strcmp(argv[1], "on") || !strcmp(argv[1], "yes")) {
		*value = TRUE;
		if (option)
			*option = "";
		return TRUE;
	}

	if (!strcmp(argv[1], "off") || !strcmp(argv[1], "no")) {
		*value = FALSE;
		return TRUE;
	}

	for (opt = arg_table; opt && *opt; opt++) {
		if (strcmp(argv[1], *opt) == 0) {
			*value = TRUE;
			*option = *opt;
			return TRUE;
		}
	}

	pr_info("Invalid argument %s\n", argv[1]);
	return FALSE;
}

static void cmd_show(int argc, char *argv[])
{
	struct adapter *adapter;
	GDBusProxy *proxy;
	DBusMessageIter iter;
	const char *address;

	if (argc < 2 || !strlen(argv[1])) {
		if (check_default_ctrl() == FALSE)
			return bt_shell_noninteractive_quit(EXIT_FAILURE);

		proxy = default_ctrl->proxy;
	} else {
		adapter = find_ctrl_by_address(ctrl_list, argv[1]);
		if (!adapter) {
			pr_info("Controller %s not available\n",
								argv[1]);
			return bt_shell_noninteractive_quit(EXIT_FAILURE);
		}
		proxy = adapter->proxy;
	}

	if (g_dbus_proxy_get_property(proxy, "Address", &iter) == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	dbus_message_iter_get_basic(&iter, &address);

	if (g_dbus_proxy_get_property(proxy, "AddressType", &iter) == TRUE) {
		const char *type;

		dbus_message_iter_get_basic(&iter, &type);

		pr_info("Controller %s (%s)\n", address, type);
	} else {
		pr_info("Controller %s\n", address);
	}

	print_property(proxy, "Name");
	print_property(proxy, "Alias");
	print_property(proxy, "Class");
	print_property(proxy, "Powered");
	print_property(proxy, "Discoverable");
	print_property(proxy, "Pairable");
	print_uuids(proxy);
	print_property(proxy, "Modalias");
	print_property(proxy, "Discovering");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_devices(void)
{
	GList *ll;

	if (check_default_ctrl() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);

	for (ll = g_list_first(default_ctrl->devices); ll; ll = g_list_next(ll)) {
		GDBusProxy *proxy = (GDBusProxy *)ll->data;
		print_device(proxy, NULL);
	}

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void generic_callback(const DBusError *error, void *user_data)
{
	char *str = user_data;

	if (dbus_error_is_set(error)) {
		pr_display("Failed to set %s: %s\n", str, error->name);
		bt_state_send(NULL, RK_BT_STATE_COMMAND_RESP_ERR);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	} else {
		/*
		if (strstr(str, "Discoverable")) {
			if (strstr(str, "on"))
				bt_state_send(NULL, RK_BT_STATE_ADAPTER_DISCOVERYABLED);
			else if (strstr(str, "off"))
				bt_state_send(NULL, RK_BT_STATE_ADAPTER_NO_DISCOVERYABLED);
		}
		if (strstr(str, "Pairable")) {
			if (strstr(str, "on"))
				bt_state_send(NULL, RK_BT_STATE_ADAPTER_PAIRABLED);
			else if (strstr(str, "off"))
				bt_state_send(NULL, RK_BT_STATE_ADAPTER_NO_PAIRABLED);
		}
		if (strstr(str, "Powered")) {
			if (strstr(str, "on"))
				bt_state_send(NULL, RK_BT_STATE_ADAPTER_POWER_ON);
			else if (strstr(str, "off"))
				bt_state_send(NULL, RK_BT_STATE_ADAPTER_POWER_OFF);
		}
		*/
		pr_display("Changing %s succeeded\n", str);
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);
	}
}

void cmd_power(bool enable)
{
	dbus_bool_t powered = enable;
	char *str;

	if (check_default_ctrl() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	str = g_strdup_printf("Powered %s", powered == TRUE ? "on" : "off");

	if (g_dbus_proxy_set_property_basic(default_ctrl->proxy, "Powered",
					DBUS_TYPE_BOOLEAN, &powered,
					generic_callback, str, g_free) == TRUE)
		return;

	g_free(str);
}

void cmd_pairable(bool enable)
{
	dbus_bool_t pairable = enable;
	char *str;

	if (check_default_ctrl() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	str = g_strdup_printf("Pairable %s", pairable == TRUE ? "on" : "off");

	if (g_dbus_proxy_set_property_basic(default_ctrl->proxy, "Pairable",
					DBUS_TYPE_BOOLEAN, &pairable,
					generic_callback, str, g_free) == TRUE)
		return;

	g_free(str);

	return bt_shell_noninteractive_quit(EXIT_FAILURE);
}

void cmd_discoverable(bool enable)
{
	dbus_bool_t discoverable = enable;
	char *str;

	if (check_default_ctrl() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	str = g_strdup_printf("Discoverable %s",
				discoverable == TRUE ? "on" : "off");

	if (g_dbus_proxy_set_property_basic(default_ctrl->proxy, "Discoverable",
					DBUS_TYPE_BOOLEAN, &discoverable,
					generic_callback, str, g_free) == TRUE)
		return;

	g_free(str);

	return bt_shell_noninteractive_quit(EXIT_FAILURE);
}

static void discovery_reply(DBusMessage *message, void *user_data)
{
	dbus_bool_t enable = GPOINTER_TO_UINT(user_data);
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		pr_display("%s: Failed to %s(id: %d) discovery: %s\n", __func__,
				enable == TRUE ? "start" : "stop", rk_gettid(), error.name);

		bt_state_send(NULL, RK_BT_STATE_COMMAND_RESP_ERR);

		dbus_error_free(&error);
		return;
	}

	pr_display("%s: Discovery %s(id: %d)\n", __func__, enable ? "started" : "stopped", rk_gettid());
	/* Leave the discovery running even on noninteractive mode */
}

static struct set_discovery_filter_args {
	char *transport;
	dbus_uint16_t rssi;
	dbus_int16_t pathloss;
	char **uuids;
	size_t uuids_len;
	dbus_bool_t duplicate;
	bool set;
} filter = {
	.rssi = DISTANCE_VAL_INVALID,
	.pathloss = DISTANCE_VAL_INVALID,
	.set = true,
};

static void set_discovery_filter_setup(DBusMessageIter *iter, void *user_data)
{
	struct set_discovery_filter_args *args = (struct set_discovery_filter_args *)user_data;
	DBusMessageIter dict;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
				DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
				DBUS_TYPE_STRING_AS_STRING
				DBUS_TYPE_VARIANT_AS_STRING
				DBUS_DICT_ENTRY_END_CHAR_AS_STRING, &dict);

	if (args->uuids_len > 0)
		g_dbus_dict_append_array(&dict, "UUIDs", DBUS_TYPE_STRING,
								&args->uuids,
								args->uuids_len);

	if (args->pathloss != DISTANCE_VAL_INVALID)
		g_dbus_dict_append_entry(&dict, "Pathloss", DBUS_TYPE_UINT16,
						&args->pathloss);

	if (args->rssi != DISTANCE_VAL_INVALID)
		g_dbus_dict_append_entry(&dict, "RSSI", DBUS_TYPE_INT16,
						&args->rssi);

	if (args->transport != NULL) {
		pr_info("%s: scan transport: %s\n", __func__, args->transport);
		g_dbus_dict_append_entry(&dict, "Transport", DBUS_TYPE_STRING,
						&args->transport);
	}

	if (args->duplicate)
		g_dbus_dict_append_entry(&dict, "DuplicateData",
						DBUS_TYPE_BOOLEAN,
						&args->duplicate);

	dbus_message_iter_close_container(iter, &dict);
}


static void set_discovery_filter_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);
	if (dbus_set_error_from_message(&error, message) == TRUE) {
		pr_info("SetDiscoveryFilter failed: %s\n", error.name);
		dbus_error_free(&error);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	filter.set = true;

	pr_info("SetDiscoveryFilter success\n");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void set_discovery_filter(void)
{
	if (check_default_ctrl() == FALSE || filter.set)
		return;

	if (g_dbus_proxy_method_call(default_ctrl->proxy, "SetDiscoveryFilter",
		set_discovery_filter_setup, set_discovery_filter_reply,
		&filter, NULL) == FALSE) {
		pr_info("Failed to set discovery filter\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	filter.set = true;
}

static int cmd_scan(const char *cmd, RK_BT_SCAN_TYPE scan)
{
	dbus_bool_t enable;
	const char *method;
	const char *type = "auto";

	if (strcmp(cmd, "on") == 0) {
		enable = TRUE;
	} else if (strcmp(cmd, "off") == 0){
		enable = FALSE;
	} else {
		pr_info("ERROR: %s cmd(%s) is invalid!\n", __func__, cmd);
		return -1;
	}

	if (check_default_ctrl() == FALSE)
		return -1;

	switch (scan) {
	case SCAN_TYPE_AUTO:
		type = "auto";
		break;
	case SCAN_TYPE_BREDR:
		type = "bredr";
		break;
	case SCAN_TYPE_LE:
		type = "le";
		break;
	default:
		break;
	}

	if (enable == TRUE) {
		if (filter.transport) {
			g_free(filter.transport);
			filter.transport = NULL;
		}
		filter.transport = g_strdup(type);
		filter.set = false;

		set_discovery_filter();
		method = "StartDiscovery";
	} else
		method = "StopDiscovery";

	pr_info("%s method = %s\n", __func__, method);

	if (g_dbus_proxy_method_call(default_ctrl->proxy, method,
				NULL, discovery_reply,
				GUINT_TO_POINTER(enable), NULL) == FALSE) {
		pr_info("Failed to %s discovery\n",
					enable == TRUE ? "start" : "stop");
		return -1;
	}

	return 0;
}

static void filter_clear_transport()
{
	if (filter.transport) {
		pr_info("%s\n", __func__);
		g_free(filter.transport);
		filter.transport = NULL;
	}
}

struct clear_entry {
	const char *name;
	void (*clear) (void);
};

static gboolean data_clear(const struct clear_entry *entry_table,
							const char *name)
{
	const struct clear_entry *entry;
	bool all = false;

	if (!name || !strlen(name) || !strcmp("all", name))
		all = true;

	for (entry = entry_table; entry && entry->name; entry++) {
		if (all || !strcmp(entry->name, name)) {
			entry->clear();
			if (!all)
				goto done;
		}
	}

	if (!all) {
		pr_info("Invalid argument %s\n", name);
		return FALSE;
	}

done:
	return TRUE;
}

struct GDBusProxy *find_device_by_address(char *address)
{
	GDBusProxy *proxy;

	if (!address || strlen(address) != 17) {
		if (default_dev)
			return default_dev;
		pr_info("Missing device address argument\n");
		return NULL;
	}

	if (check_default_ctrl() == FALSE)
		return NULL;

	proxy = find_proxy_by_address(default_ctrl->devices, address);
	if (!proxy) {
		pr_info("%s: Device %s not available\n", __func__, address);
		return NULL;
	}

	return proxy;
}

static void pair_reply(DBusMessage *message, void *user_data)
{
	DBusError error;
	GDBusProxy *proxy = user_data;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		pr_info("Failed to pair: %s\n", error.name);
		dbus_error_free(&error);

		char buff[128];
		sprintf(buff, "Failed to pair: %s:%s", error.name, error.message);
		bt_state_change(proxy, buff, RK_BT_STATE_PAIR_FAILED);

		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	pr_info("Pairing successful\n");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static const char *proxy_address(GDBusProxy *proxy)
{
	DBusMessageIter iter;
	const char *addr;

	if (!g_dbus_proxy_get_property(proxy, "Address", &iter))
		return NULL;

	dbus_message_iter_get_basic(&iter, &addr);

	return addr;
}

static int cmd_pair(GDBusProxy *proxy)
{
	if (!proxy)
		return -1;

	if (g_dbus_proxy_method_call(proxy, "Pair", NULL, pair_reply,
							proxy, NULL) == FALSE) {
		pr_info("%s: Failed to pair\n", __func__);
		return -1;
	}

	pr_info("%s: Attempting to pair with %s\n", __func__, proxy_address(proxy));
	return 0;
}

static void remove_device_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		pr_info("Failed to remove device: %s\n", error.name);
		bt_state_send(NULL, RK_BT_STATE_DEL_DEV_FAILED);
		dbus_error_free(&error);
		return;
	}

	bt_state_send(NULL, RK_BT_STATE_COMMAND_RESP_OK);
	pr_info("%s: Device has been removed\n", __func__);
	return;
}

static void remove_device_setup(DBusMessageIter *iter, void *user_data)
{
	char *path = (char *)user_data;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH, &path);
}

static int remove_device(GDBusProxy *proxy)
{
	char *path;

	path = g_strdup(g_dbus_proxy_get_path(proxy));

	if (check_default_ctrl() == FALSE)
		return false;

	pr_info("%s: Attempting to remove device with %s\n", __func__, proxy_address(proxy));
	if (g_dbus_proxy_method_call(default_ctrl->proxy, "RemoveDevice",
						remove_device_setup,
						remove_device_reply,
						path, g_free) == FALSE) {
		pr_info("%s: Failed to remove device\n", __func__);
		g_free(path);
		return false;
	}

	return false;
}

void iBle_advertise(int enable)
{
	const char *type  = "peripheral";

	if (!default_ctrl || !default_ctrl->ad_proxy) {
		pr_info("LEAdvertisingManager not found\n");
		return;
	}

	if (enable == TRUE)
		ad_register(dbus_conn, default_ctrl->ad_proxy, type);
	else
		ad_unregister(dbus_conn, default_ctrl->ad_proxy);
}

static void ad_clear_uuids(void)
{
	ad_disable_uuids(dbus_conn);
}

static void ad_clear_service(void)
{
	ad_disable_service(dbus_conn);
}

static void ad_clear_manufacturer(void)
{
	ad_disable_manufacturer(dbus_conn);
}

static void ad_clear_data(void)
{
	ad_disable_data(dbus_conn);
}

static void ad_clear_tx_power(void)
{
	dbus_bool_t powered = false;

	ad_advertise_tx_power(dbus_conn, &powered);
}

static void ad_clear_name(void)
{
	ad_advertise_name(dbus_conn, false);
}

static void ad_clear_appearance(void)
{
	ad_advertise_appearance(dbus_conn, false);
}

static void ad_clear_duration(void)
{
	long int value = 0;

	ad_advertise_duration(dbus_conn, &value);
}

static void ad_clear_timeout(void)
{
	long int value = 0;

	ad_advertise_timeout(dbus_conn, &value);
}

static const struct clear_entry ad_clear[] = {
	{ "uuids",		ad_clear_uuids },
	{ "service",		ad_clear_service },
	{ "manufacturer",	ad_clear_manufacturer },
	{ "data",		ad_clear_data },
	{ "tx-power",		ad_clear_tx_power },
	{ "name",		ad_clear_name },
	{ "appearance",		ad_clear_appearance },
	{ "duration",		ad_clear_duration },
	{ "timeout",		ad_clear_timeout },
	{}
};
static void cmd_ad_clear(int argc, char *argv[])
{
	bool all = false;

	if (argc < 2 || !strlen(argv[1]))
		all = true;

	if (!data_clear(ad_clear, all ? "all" : argv[1]))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
}

static void disconn_reply(DBusMessage *message, void *user_data)
{
	GDBusProxy *proxy = (GDBusProxy *)user_data;
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		pr_info("Failed to disconnect: %s\n", error.name);
		dbus_error_free(&error);

		char buff[128];
		sprintf(buff, "Failed to disconnect device: %s:%s", error.name, error.message);
		bt_state_change(proxy, buff, RK_BT_STATE_DISCONN_FAILED);

		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	//TODO 当connect连接中，一直没返回，此时调用disconnect api，会有如下log：
	/*
	 rk_bt_disconnect_by_addr
	 disconnect_by_proxy: Attempting to disconnect from D4:9C:DD:F5:C9:E1

	 //此时才返回connect连接失败的 原因
	 connect_by_address_reply: Failed to connect: org.bluez.Error.Failed
	 [CHG]: Failed to connect: (null):(null)
	 [DBG]: 0x7f7400d1d0:0x7f7400d1d0
	 Device D4:9C:DD:F5:C9:E1 (public:SCO_AUDIO:0)
	 change name: Failed to connect: (null):(null)

	 //断开命令本身成功了，是否要反馈给应用层？因为正常是会在proxy_changed那边反馈断开成功
	 disconn_reply: Successful disconnected

	 [EXEC_DEBUG]: [hcitool con]
	 tmp_buf[13]: [Connections: ]
	 len: 1011
	 [EXEC_DEBUG] execute_r[13]: [Connections: ]

	 */
	bt_state_send(NULL, RK_BT_STATE_COMMAND_RESP_OK);
	pr_info("%s: Successful disconnected\n", __func__);

	//check disconnect
	if (bt_is_connected())
		pr_info("\n\n%s: The ACL link still exists!\n\n\n", __func__);

	if (proxy == default_dev)
		set_default_device(NULL, NULL);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static gboolean bluetooth_open(void *user_data)
{
	pr_display("%s id: %d\n", __func__, rk_gettid());
	//RkBtContent *bt_content = (RkBtContent *)user_data;

	//init var
	dbus_conn = NULL;
	agent_manager = NULL;
	default_ctrl = NULL;
	ctrl_list = NULL;
	default_dev = NULL;
	default_attr = NULL;
	btsrc_client = NULL;
	pthread_mutex_init(&mutex, NULL);

	dbus_conn = g_dbus_setup_bus(DBUS_BUS_SYSTEM, NULL, NULL);
	g_dbus_attach_object_manager(dbus_conn);

	btsrc_client = g_dbus_client_new(dbus_conn, "org.bluez", "/org/bluez");
	if (NULL == btsrc_client) {
		pr_info("%s: btsrc_client init fail\n", __func__);
		dbus_connection_unref(dbus_conn);
		return false;
	}

	if (g_rkbt_content->profile & PROFILE_BLE)
		gatt_init(g_rkbt_content);

	g_dbus_client_set_connect_watch(btsrc_client, connect_handler, NULL);
	g_dbus_client_set_disconnect_watch(btsrc_client, disconnect_handler, NULL);
	g_dbus_client_set_signal_watch(btsrc_client, message_handler, NULL);
	g_dbus_client_set_proxy_handlers(btsrc_client, proxy_added, proxy_removed,
						  property_changed, NULL);

	BT_OPENED = 1;
	pr_info("%s: server start...\n", __func__);

	return false;
}

int bt_open(RkBtContent *bt_content)
{
	int confirm_cnt = 100;
	pr_display("%s id: %d\n", __func__, rk_gettid());

	g_idle_add(bluetooth_open, bt_content);

	BT_OPENED = 0;
	while (confirm_cnt-- && !BT_OPENED)
		usleep(50 * 1000);

	if (BT_OPENED)
		return 0;

	return -1;
}

static gboolean _bluetooth_close(void *user_data)
{
	g_dbus_client_unref(btsrc_client);
	btsrc_client = NULL;

	gatt_cleanup();

	dbus_connection_unref(dbus_conn);
	dbus_conn = NULL;

	g_list_free_full(ctrl_list, proxy_leak);
	pthread_mutex_destroy(&mutex);

	pr_info("%s: server exit!\n", __func__);

	return false;
}

void bt_close(void)
{
	g_idle_add(_bluetooth_close, NULL);
}

static int disconnect_by_proxy(GDBusProxy *proxy)
{
	if (!proxy) {
		pr_info("%s: Invalid proxy\n", __func__);
		bt_state_send(NULL, RK_BT_STATE_DISCONN_FAILED);
		return false;
	}

	if (g_dbus_proxy_method_call(proxy, "Disconnect", NULL, disconn_reply,
							proxy, NULL) == FALSE) {
		pr_info("Failed to disconnect\n");
		return false;
	}

	pr_info("%s: Attempting to disconnect from %s\n", __func__, proxy_address(proxy));
	return false;
}

gboolean _bt_remove_by_address(gpointer data)
{
	GDBusProxy *proxy;
	char *t_address = data;

	if (t_address == NULL) {
		pr_err("%s: Invalid address\n", __func__);
		return false;
	}

	if (check_default_ctrl() == FALSE)
		return false;

	if (strcmp(t_address, "*") == 0) {
		GList *list;

		for (list = default_ctrl->devices; list; list = g_list_next(list)) {
			proxy = (GDBusProxy *)list->data;
			remove_device(proxy);
		}
		return 0;
	} else if ((strlen(t_address) != 17)) {
		pr_err("%s: %s address error!\n", __func__, t_address);
		return false;
	}

	proxy = find_proxy_by_address(default_ctrl->devices, t_address);
	if (!proxy) {
		pr_info("Device %s not available\n", t_address);
		return false;
	}

	return remove_device(proxy);
}

gboolean disconnect_current_devices(gpointer data)
{
	if (!default_dev) {
		pr_info("%s: No connected device\n", __func__);
		return false;
	}

	return disconnect_by_proxy(default_dev);
}

int get_dev_platform(char *address)
{
	int vendor = -1, platform = DEV_PLATFORM_UNKNOWN;
	char *str;
	const char *valstr;
	GDBusProxy *proxy;
	DBusMessageIter iter;

	if (!address) {
		pr_info("%s: Invalid address\n", __func__);
		return DEV_PLATFORM_UNKNOWN;
	}

	proxy = find_device_by_address(address);
	if (!proxy) {
		pr_info("%s: Invalid proxy\n", __func__);
		return DEV_PLATFORM_UNKNOWN;
	}

	if (g_dbus_proxy_get_property(proxy, "Modalias", &iter) == FALSE) {
		pr_info("%s: WARING: can't get Modalias!\n", __func__);
		return DEV_PLATFORM_UNKNOWN;
	}

	dbus_message_iter_get_basic(&iter, &valstr);
	pr_info("%s: Modalias valstr = %s\n", __func__, valstr);

	str = strstr(valstr, "v");
	if (str) {
		if(!strncasecmp(str + 1, "004c", 4))
			vendor = IOS_VENDOR_SOURCE_BT;
		else if(!strncasecmp(str + 1, "05ac", 4))
			vendor = IOS_VENDOR_SOURCE_USB;
	}

	if (vendor == IOS_VENDOR_SOURCE_BT || vendor == IOS_VENDOR_SOURCE_USB)
		platform = DEV_PLATFORM_IOS;

	pr_info("%s: %s is %s\n", __func__, address,
		platform == DEV_PLATFORM_UNKNOWN ? "Unknown Platform" : "Apple IOS");

	return platform;
}

int get_current_dev_platform()
{
	if (!default_dev) {
		pr_info("%s: No connected device\n", __func__);
		return DEV_PLATFORM_UNKNOWN;
	}

	return get_dev_platform((char *)proxy_address(default_dev));
}

static void connect_by_address_reply(DBusMessage *message, void *user_data)
{
	DBusError error;
	GDBusProxy *proxy = user_data;
	char buff[128];

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		pr_info("%s: Failed to connect: %s:%s\n", __func__, error.name, error.message);
		sprintf(buff, "Failed to connect: %s", error.message);
		dbus_error_free(&error);

		bt_state_change(proxy, buff, RK_BT_STATE_CONNECT_FAILED);

		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_state_send(NULL, RK_BT_STATE_COMMAND_RESP_OK);
	pr_info("%s: Connection successful\n", __func__);
	set_default_device(proxy, NULL);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

gboolean connect_by_address(gpointer data)
{
	GDBusProxy *proxy;
	char *addr = data;
	pr_display("%s id: %d\n", __func__, rk_gettid());

	if (!addr || (strlen(addr) != 17)) {
		bt_state_send(NULL, RK_BT_STATE_CONNECT_FAILED_INVAILD_ADDR);
		return false;
	}

	proxy = find_device_by_address(addr);
	if (!proxy) {
		bt_state_send(NULL, RK_BT_STATE_CONNECT_FAILED_NO_FOUND_DEVICE);
		return false;
	}

	DBusMessageIter iter;
	if (g_dbus_proxy_get_property(proxy, "Connected", &iter)) {
		dbus_bool_t connected;

		dbus_message_iter_get_basic(&iter, &connected);

		if (connected) {
			bt_state_send(NULL, RK_BT_STATE_CONNECTED_ALREADY);
			return false;
		}
	}

	if (g_dbus_proxy_method_call(proxy, "Connect", NULL,
								 connect_by_address_reply, proxy, NULL) == FALSE) {
		pr_info("%s: Failed to call org.bluez.Device1.Connect\n", __func__);
		//bt_state_send(NULL, RK_BT_STATE_CONNECT_FAILED);
		return false;
	}

	pr_info("%s: Attempting to connect to %s\n", __func__, addr);
	return false;
}

gboolean _bt_disconnect_by_address(gpointer data)
{
	GDBusProxy *proxy;
	char *addr = data;

	if (!addr || (strlen(addr) != 17)) {
		pr_err("%s: Invalid address\n", __func__);
		bt_state_send(NULL, RK_BT_STATE_DISCONN_FAILED);
		return false;
	}

	proxy = find_device_by_address(addr);
	if (!proxy) {
		bt_state_send(NULL, RK_BT_STATE_DISCONN_FAILED);
		pr_info("%s: Invalid proxy\n", __func__);
		return false;
	}

	DBusMessageIter iter;
	if (g_dbus_proxy_get_property(proxy, "Connected", &iter)) {
		dbus_bool_t connected;

		dbus_message_iter_get_basic(&iter, &connected);

		if (!connected) {
			bt_state_send(NULL, RK_BT_STATE_DISCONN_ALREADY);
			return false;
		}
	}

	return disconnect_by_proxy(proxy);
}

gboolean pair_by_addr(gpointer data)
{
	GDBusProxy *proxy;
	char *addr = data;

	if (!addr || (strlen(addr) != 17)) {
		pr_err("%s: Invalid address\n", __func__);
		return false;
	}

	proxy = find_device_by_address(addr);
	if (!proxy) {
		pr_info("%s: Invalid proxy\n", __func__);
		return false;
	}

	return cmd_pair(proxy);
}

gboolean unpair_by_addr(gpointer data)
{
	GDBusProxy *proxy;
	char *addr = data;
	pr_info("%s\n", __func__);

	if (!addr || (strlen(addr) != 17)) {
		bt_state_send(NULL, RK_BT_STATE_DEL_DEV_FAILED);
		pr_err("%s: Invalid address: [%p:%s]\n", __func__, data, addr);
		return false;
	}

	proxy = find_device_by_address(addr);
	if (!proxy) {
		bt_state_send(NULL, RK_BT_STATE_DEL_DEV_FAILED);
		pr_info("%s: Invalid proxy\n", __func__);
		return false;
	}

	/* There is no direct unpair method, removing device will clear pairing information */
	return remove_device(proxy);
}

static void reomve_unpaired_device(void)
{
	//GDBusProxy *proxy;
	DBusMessageIter iter;
	GList *list;

	if (check_default_ctrl() == FALSE)
		return;

	for (list = default_ctrl->devices; list;
					list = g_list_next(list)) {
		dbus_bool_t paired = FALSE;
		dbus_bool_t connected = FALSE;

		GDBusProxy *proxy = (GDBusProxy *)list->data;

		if (g_dbus_proxy_get_property(proxy, "Paired", &iter))
			dbus_message_iter_get_basic(&iter, &paired);

		if (g_dbus_proxy_get_property(proxy, "Connected", &iter))
			dbus_message_iter_get_basic(&iter, &connected);

		if (paired || connected) {
			const char *address;
			if (g_dbus_proxy_get_property(proxy, "Address", &iter)) {
				dbus_message_iter_get_basic(&iter, &address);
				pr_info("%s: address(%s) is paired(%d) or connected(%d)\n", __func__, address, paired, connected);
			}
			continue;
		}

		remove_device(proxy);
	}

	return;
}

gboolean bt_start_discovery(gpointer data)
{
	RK_BT_SCAN_TYPE *scan_type = data;
	pr_display("%s id: %d\n", __func__, rk_gettid());

	pr_info("=== scan on %d===\n", *scan_type);

	reomve_unpaired_device();

	cmd_scan("on", *scan_type);

	return false;
}

gboolean bt_cancel_discovery(gpointer data)
{
	pr_info("%s thread tid = %lu\n", __func__, pthread_self());

	cmd_scan("off", 0);

	return false;
}

/*
 * / # hcitool con
 * Connections:
 *      > ACL 64:A2:F9:68:1E:7E handle 1 state 1 lm SLAVE AUTH ENCRYPT
 *      > LE 60:9C:59:31:7F:B9 handle 16 state 1 lm SLAVE
 */
bool bt_is_connected()
{
	bool ret = false;
	char buf[1024];

	memset(buf, 0, 1024);
	exec_command("hcitool con", buf, 1024);
	usleep(300000);

	if (strstr(buf, "ACL") || strstr(buf, "LE"))
		ret = true;

	return ret;
}
