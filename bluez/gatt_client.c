#include <stdio.h>
#include <errno.h>
#include <glib.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "a2dp_source/a2dp_masterctrl.h"
#include "a2dp_source/gatt.h"
#include "utility.h"
#include "slog.h"
#include "gatt_client.h"

void gatt_client_recv_data_send(GDBusProxy *proxy, DBusMessageIter *iter)
{
	DBusMessageIter array, uuid_iter;
	const char *uuid;
	uint8_t *value;
	int len;

	if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_ARRAY) {
		pr_info("%s: Unable to get value\n", __func__);
		return;
	}

	dbus_message_iter_recurse(iter, &array);
	dbus_message_iter_get_fixed_array(&array, &value, &len);

	if (len < 0) {
		pr_info("%s: Unable to parse value\n", __func__);
		return;
	}

	if (g_dbus_proxy_get_property(proxy, "UUID", &uuid_iter) == FALSE)
		return;

	dbus_message_iter_get_basic(&uuid_iter, &uuid);

	//todo sync??
	if (g_rkbt_content->ble_content.cb_ble_recv_fun)
		g_rkbt_content->ble_content.cb_ble_recv_fun(uuid, (char *)value, &len, RK_BLE_GATT_CLIENT_READ_BY_LOCAL);
}

int gatt_client_get_service_info(char *address, RK_BLE_CLIENT_SERVICE_INFO *info)
{
	GDBusProxy *proxy;

	if (!address || !info) {
		pr_err("%s: Invalid parameters\n", __func__);
		return -1;
	}

	proxy = find_device_by_address(address);
	if (!proxy) {
		pr_info("%s: can't find device(%s)\n", __func__, address);
		return -1;
	}

	memset(info, 0, sizeof(RK_BLE_CLIENT_SERVICE_INFO));
	gatt_get_list_attributes(g_dbus_proxy_get_path(proxy), info);
	return 0;
}

static int gatt_client_select_attribute(const char *uuid)
{
	GDBusProxy *proxy;

	proxy = gatt_select_attribute(NULL, uuid);
	if (proxy) {
		set_default_attribute(proxy);
		return 0;
	}

	return -1;
}

gboolean gatt_client_read(gpointer data)
{
	int ret;
	const char *uuid = data;

	if (!uuid) {
		pr_err("%s: Invalid uuid format\n", __func__);
		g_rkbt_content->ble_content.cb_ble_recv_fun(uuid,
					"Invalid uuid",
					(int32_t *)strlen("Invalid uuid format"),
					RK_BLE_GATT_CMD_CLIENT_READ_ERR);
		return false;
	}

	if (gatt_client_select_attribute(uuid) < 0) {
		g_rkbt_content->ble_content.cb_ble_recv_fun(uuid,
					"Invalid uuid",
					(int32_t *)strlen("Invalid uuid"),
					RK_BLE_GATT_CMD_CLIENT_READ_ERR);
		return false;
	}

	if (gatt_read_attribute(default_attr, 0)) {
		g_rkbt_content->ble_content.cb_ble_recv_fun(uuid,
					"UNKNOW ERR",
					(int32_t *)strlen("UNKNOW ERR"),
					RK_BLE_GATT_CMD_CLIENT_READ_ERR);
		return false;
	}

	return false;
}

extern char rk_gatt_data[1 + 512];
gboolean gatt_client_write(gpointer uuid)
//int gatt_client_write(const char *uuid)
{
	char data[512];
	int data_len = rk_gatt_data[0];

	memcpy(data, rk_gatt_data + 1, data_len);

	if (gatt_client_select_attribute(uuid) < 0) {
		pr_err("%s: select attribute failed\n", __func__);
		g_rkbt_content->ble_content.cb_ble_recv_fun(uuid,
											"Invalid uuid",
											(int32_t *)strlen("Invalid uuid"),
											RK_BLE_GATT_CMD_CLIENT_WRITE_ERR);
		return false;
	}

	if (gatt_write_attribute(default_attr, data, data_len, 0)) {
		g_rkbt_content->ble_content.cb_ble_recv_fun(uuid,
											"UNKNOW ERR",
											(int32_t *)strlen("UNKNOW ERR"),
											RK_BLE_GATT_CMD_CLIENT_WRITE_ERR);
		return false;
	}

	return false;
}

bool gatt_client_is_notifying(const char *uuid)
{
	if (!uuid) {
		pr_err("%s: failed\n", __func__);
		g_rkbt_content->ble_content.cb_ble_recv_fun(uuid,
					"Invalid uuid",
					(int32_t *)strlen("Invalid uuid"),
					RK_BLE_GATT_CMD_CLIENT_NOTIFYD_ERR);
		return false;
	}

	if (gatt_client_select_attribute(uuid) < 0) {
		pr_err("%s: select attribute failed\n", __func__);
		g_rkbt_content->ble_content.cb_ble_recv_fun(uuid,
					"Invalid uuid",
					(int32_t *)strlen("Invalid uuid"),
					RK_BLE_GATT_CMD_CLIENT_NOTIFYD_ERR);
		return false;
	}

	return gatt_get_notifying(default_attr);
}

int gatt_client_notify(const char *uuid, bool enable)
{
	if (!uuid) {
		pr_err("%s: gatt_client_notify failed\n", __func__);
		g_rkbt_content->ble_content.cb_ble_recv_fun(uuid,
					"Invalid uuid",
					(int32_t *)strlen("Invalid uuid"),
					RK_BLE_GATT_CMD_CLIENT_NOTIFYD_ERR);
		return false;
	}

	if (gatt_client_select_attribute(uuid) < 0) {
		pr_err("%s: select attribute failed\n", __func__);
		g_rkbt_content->ble_content.cb_ble_recv_fun(uuid,
					"Invalid uuid",
					(int32_t *)strlen("Invalid uuid"),
					RK_BLE_GATT_CMD_CLIENT_NOTIFYD_ERR);
		return false;
	}

	if (gatt_notify_attribute(default_attr, enable ? true : false) < 0) {
		pr_err("%s: select attribute failed\n", __func__);
		g_rkbt_content->ble_content.cb_ble_recv_fun(uuid,
					"Invalid uuid",
					(int32_t *)strlen("Invalid uuid"),
					RK_BLE_GATT_CMD_CLIENT_NOTIFYD_ERR);
		return false;
	}

	return false;
}
