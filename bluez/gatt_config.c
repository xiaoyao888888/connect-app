/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2014  Instituto Nokia de Tecnologia - INdT
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdint.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <pthread.h>

#include <glib.h>
#include <dbus/dbus.h>

#include <RkBle.h>
#include "error.h"
#include "gdbus/gdbus.h"
#include "gatt_config.h"
#include "slog.h"
#include "a2dp_source/util.h"
#include "a2dp_source/io.h"
#include "a2dp_source/a2dp_masterctrl.h"
#include "gatt_client.h"
#include "utility.h"

#define GATT_MGR_IFACE				"org.bluez.GattManager1"
#define GATT_SERVICE_IFACE			"org.bluez.GattService1"
#define GATT_CHR_IFACE				"org.bluez.GattCharacteristic1"
#define GATT_DESCRIPTOR_IFACE		"org.bluez.GattDescriptor1"

//static char reconnect_path[66];
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
static volatile bool g_dis_adv_close_ble = false;
#define min(x, y) ((x) < (y) ? (x) : (y))

#define AD_FLAGS						0x1
#define AD_COMPLETE_128_SERVICE_UUID	0x7
#define AD_COMPLETE_LOCAL_NAME			0x9


#define GATT_MAX_CHR 10
#define MAX_UUID_LEN 38

typedef struct {
	const char *uuid_flag_t[10];
	char uuid_flag[10][32];
} ble_uuid_flag_t;

typedef struct {
	char server_uuid[MAX_UUID_LEN];
	char char_uuid[GATT_MAX_CHR][MAX_UUID_LEN];
	ble_uuid_flag_t prop_uuid_flag[GATT_MAX_CHR];
	uint8_t char_cnt;
} ble_gatt_t;

static int characteristic_id = 1;
static int service_id = 1;

static GDBusProxy *ble_proxy = NULL;
extern DBusConnection *dbus_conn;

struct characteristic {
	uint16_t handle;
	char *service;
	char *uuid;
	char **flags;
	char *path;
	uint8_t *value;
	int vlen;
	uint16_t mtu;
	bool notifying;
	const char **props;
	struct io *write_io;
	struct io *notify_io;
};

struct rk_gatt_service {
	struct characteristic *gchr[MAX_GATT_CHARACTERISTIC];
	struct descriptor *gdesc[MAX_GATT_CHARACTERISTIC];
	uint8_t chr_cnt;
};

struct rk_gatt {
	struct rk_gatt_service gatt_service[MAX_GATT_SERVICE];
	char *gservice_path[MAX_GATT_SERVICE];
	uint8_t srv_cnt;
};
struct rk_gatt gatt_service_instance;

struct descriptor {
	struct characteristic *chr;
	char *uuid;
	char *path;
	uint8_t *value;
	int vlen;
	char **props;
};

/*
 * Supported properties are defined at doc/gatt-api.txt. See "Flags"
 * property of the GattCharacteristic1.
 */
//static const char *ias_alert_level_props[] = { "read", "write", NULL };
//static const char *chr_props[] = { "read", "write", "notify", "indicate", "write-without-response", NULL };
//static const char *chr_props[] = { "read", "write", "indicate", "write-without-response", "authorize", "encrypt-read", NULL };
static char *desc_props[] = { "read", "write", NULL };

static void chr_write(struct characteristic *chr, const uint8_t *value, int len);
static void chr_iface_destroy(gpointer user_data);

static gboolean desc_get_uuid(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	struct descriptor *desc = user_data;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &desc->uuid);

	return TRUE;
}

static gboolean desc_get_characteristic(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	struct descriptor *desc = user_data;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH,
						&desc->chr->path);

	return TRUE;
}

static bool desc_read(struct descriptor *desc, DBusMessageIter *iter)
{
	DBusMessageIter array;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
					DBUS_TYPE_BYTE_AS_STRING, &array);

	if (desc->vlen && desc->value)
		dbus_message_iter_append_fixed_array(&array, DBUS_TYPE_BYTE,
						&desc->value, desc->vlen);

	dbus_message_iter_close_container(iter, &array);

	return true;
}

static gboolean desc_get_value(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	struct descriptor *desc = user_data;

	pr_info("Descriptor(%s): Get(\"Value\")\n", desc->uuid);

	return desc_read(desc, iter);
}

static void desc_write(struct descriptor *desc, const uint8_t *value, int len)
{
	g_free(desc->value);
	desc->value = g_memdup2(value, len);
	desc->vlen = len;

	g_dbus_emit_property_changed(dbus_conn, desc->path,
					GATT_DESCRIPTOR_IFACE, "Value");
}

static int parse_value(DBusMessageIter *iter, const uint8_t **value, int *len)
{
	DBusMessageIter array;

	if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_ARRAY)
		return -EINVAL;

	dbus_message_iter_recurse(iter, &array);
	dbus_message_iter_get_fixed_array(&array, value, len);

	return 0;
}

static void desc_set_value(const GDBusPropertyTable *property,
				DBusMessageIter *iter,
				GDBusPendingPropertySet id, void *user_data)
{
	struct descriptor *desc = user_data;
	const uint8_t *value;
	int len;

	pr_info("Descriptor(%s): Set(\"Value\", ...)\n", desc->uuid);

	if (parse_value(iter, &value, &len)) {
		pr_info("Invalid value for Set('Value'...)\n");
		g_dbus_pending_property_error(id,
					ERROR_INTERFACE ".InvalidArguments",
					"Invalid arguments in method call");
		return;
	}

	desc_write(desc, value, len);

	g_dbus_pending_property_success(id);
}

static gboolean desc_get_props(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *data)
{
	struct descriptor *desc = data;
	DBusMessageIter array;
	int i;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
					DBUS_TYPE_STRING_AS_STRING, &array);

	for (i = 0; desc->props[i]; i++)
		dbus_message_iter_append_basic(&array,
					DBUS_TYPE_STRING, &desc->props[i]);

	dbus_message_iter_close_container(iter, &array);

	return TRUE;
}

static const GDBusPropertyTable desc_properties[] = {
	{ "UUID",		"s", desc_get_uuid },
	{ "Characteristic",	"o", desc_get_characteristic },
	{ "Value",		"ay", desc_get_value, desc_set_value, NULL },
	{ "Flags",		"as", desc_get_props, NULL, NULL },
	{ }
};

static gboolean chr_get_uuid(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	struct characteristic *chr = user_data;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &chr->uuid);

	return TRUE;
}

static gboolean chr_get_service(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	struct characteristic *chr = user_data;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH,
							&chr->service);

	return TRUE;
}

static bool chr_read(struct characteristic *chr, DBusMessageIter *iter)
{
	DBusMessageIter array;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
					DBUS_TYPE_BYTE_AS_STRING, &array);

	dbus_message_iter_append_fixed_array(&array, DBUS_TYPE_BYTE,
						&chr->value, chr->vlen);

	dbus_message_iter_close_container(iter, &array);

	return true;
}

static gboolean chr_get_value(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	struct characteristic *chr = user_data;

	pr_info("Characteristic(%s): Get(\"Value\")\n", chr->uuid);

	return chr_read(chr, iter);
}

static gboolean chr_get_props(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *data)
{
	struct characteristic *chr = data;
	DBusMessageIter array;
	int i;
	pr_info("%s: enter\n", __func__);

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
					DBUS_TYPE_STRING_AS_STRING, &array);

	pr_info("%s: %p:%p\n", __func__, chr->props, chr->props[0]);
	for (i = 0; chr->props[i]; i++) {
		pr_info("%s: %s\n", __func__, chr->props[i]);
		dbus_message_iter_append_basic(&array,
					DBUS_TYPE_STRING, &chr->props[i]);
	}

	dbus_message_iter_close_container(iter, &array);

	pr_info("%s: exit\n", __func__);
	return TRUE;
}

static void chr_write(struct characteristic *chr, const uint8_t *value, int len)
{
	g_free(chr->value);
	chr->value = g_memdup2(value, len);
	chr->vlen = len;

	//g_dbus_emit_property_changed(dbus_conn, chr->path, GATT_CHR_IFACE,
	//							"Value");
	g_dbus_emit_property_changed_full(dbus_conn, chr->path, GATT_CHR_IFACE,
								"Value", G_DBUS_PROPERTY_CHANGED_FLAG_FLUSH);
}

static void chr_set_value(const GDBusPropertyTable *property,
				DBusMessageIter *iter,
				GDBusPendingPropertySet id, void *user_data)
{
	struct characteristic *chr = user_data;
	const uint8_t *value;
	int len;

	pr_info("Characteristic(%s): Set('Value', ...)\n", chr->uuid);

	if (!parse_value(iter, &value, &len)) {
		pr_info("Invalid value for Set('Value'...)\n");
		g_dbus_pending_property_error(id,
					ERROR_INTERFACE ".InvalidArguments",
					"Invalid arguments in method call");
		return;
	}

	chr_write(chr, value, len);

	g_dbus_pending_property_success(id);
}

static gboolean chr_get_notify_acquired(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *data)
{
	struct characteristic *chrc = data;
	dbus_bool_t value;

	value = chrc->notify_io ? TRUE : FALSE;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN, &value);

	return TRUE;
}

static gboolean chr_notify_acquired_exists(const GDBusPropertyTable *property,
								void *data)
{
	struct characteristic *chrc = data;
	int i;

	for (i = 0; chrc->flags[i]; i++) {
		if (!strcmp("disable-notify", chrc->flags[i]))
			return TRUE;
	}

	return FALSE;
}

static void chr_set_mtu(const GDBusPropertyTable *property,
			DBusMessageIter *value, GDBusPendingPropertySet id,
			void *data)
{
	struct characteristic *chr = data;

	if (dbus_message_iter_get_arg_type(value) != DBUS_TYPE_UINT16) {
		g_dbus_pending_property_error(id, "org.bluez.InvalidArguments",
					"Invalid arguments in method call");
		return;
	}

	dbus_message_iter_get_basic(value, &chr->mtu);

	pr_display("Characteristic(%s): MTU: %d\n", chr->uuid, chr->mtu);
	if (g_rkbt_content->ble_content.cb_ble_recv_fun)
		g_rkbt_content->ble_content.cb_ble_recv_fun(chr->uuid, (char *)(&chr->mtu), NULL, RK_BLE_GATT_MTU);

	g_dbus_pending_property_success(id);
}

static gboolean chr_get_mtu(const GDBusPropertyTable *property,
				       DBusMessageIter *iter, void *data)
{
	struct characteristic *chr = data;
	uint16_t mtu;
	//pr_display("%s\n", __func__);

	mtu = chr->mtu;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_UINT16, &mtu);

	return TRUE;
}

static gboolean chr_mtu_exists(const GDBusPropertyTable *property,
								void *data)
{
	//pr_display("%s\n", __func__);
	return TRUE;
}
								
static gboolean chrc_get_handle(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *data)
{
	struct characteristic *chrc = data;
	//pr_display("%s\n", __func__);

	dbus_message_iter_append_basic(iter, DBUS_TYPE_UINT16, &chrc->handle);

	return TRUE;
}

static void chrc_set_handle(const GDBusPropertyTable *property,
			DBusMessageIter *value, GDBusPendingPropertySet id,
			void *data)
{
	struct characteristic *chrc = data;

	//pr_display("%s\n", __func__);

	if (dbus_message_iter_get_arg_type(value) != DBUS_TYPE_UINT16) {
		g_dbus_pending_property_error(id, "org.bluez.InvalidArguments",
					"Invalid arguments in method call");
		return;
	}

	dbus_message_iter_get_basic(value, &chrc->handle);

	//print_chrc(chrc, COLORED_CHG);

	g_dbus_pending_property_success(id);
}

static const GDBusPropertyTable chr_properties[] = {
	{ "Handle", "q", chrc_get_handle, chrc_set_handle, NULL },
	{ "UUID",	"s", chr_get_uuid },
	{ "Service",	"o", chr_get_service },
	{ "Value",	"ay", chr_get_value, chr_set_value, NULL },
	{ "Flags",	"as", chr_get_props, NULL, NULL },
	//{ "NotifyAcquired", "b", chr_get_notify_acquired, NULL,
	//			chr_notify_acquired_exists },
	{ "MTU", "q", chr_get_mtu, chr_set_mtu, chr_mtu_exists },
	{ }
};

static gboolean service_get_primary(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	dbus_bool_t primary = TRUE;

	pr_info("Get Primary: %s\n", primary ? "True" : "False");

	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN, &primary);

	return TRUE;
}

static gboolean service_get_uuid(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	const char *uuid = user_data;

	pr_info("Get UUID: %s\n", uuid);

	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &uuid);

	return TRUE;
}

static const GDBusPropertyTable service_properties[] = {
	{ "Primary", "b", service_get_primary },
	{ "UUID", "s", service_get_uuid },
	{ }
};

static void chr_iface_destroy(gpointer user_data)
{
	struct characteristic *chr = user_data;

	pr_info("== %s ==\n", __func__);
	g_free(chr->uuid);
	g_free(chr->service);
	g_free(chr->value);
	g_free(chr->path);
	g_free(chr);
}

static void desc_iface_destroy(gpointer user_data)
{
	struct descriptor *desc = user_data;

	pr_info("== %s ==\n", __func__);
	g_free(desc->uuid);
	g_free(desc->value);
	g_free(desc->path);
	g_free(desc);
}

static int parse_options(DBusMessageIter *iter, uint16_t *offset, uint16_t *mtu,
						char **device, char **link,
						bool *prep_authorize)
{
	DBusMessageIter dict;

	if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_ARRAY)
		return -EINVAL;

	dbus_message_iter_recurse(iter, &dict);

	while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
		const char *key;
		DBusMessageIter value, entry;
		int var;

		dbus_message_iter_recurse(&dict, &entry);
		dbus_message_iter_get_basic(&entry, &key);

		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &value);

		var = dbus_message_iter_get_arg_type(&value);
		if (strcasecmp(key, "offset") == 0) {
			if (var != DBUS_TYPE_UINT16)
				return -EINVAL;
			if (offset)
				dbus_message_iter_get_basic(&value, offset);
		} else if (strcasecmp(key, "MTU") == 0) {
			uint16_t tmp;

			if (var != DBUS_TYPE_UINT16)
				return -EINVAL;

			dbus_message_iter_get_basic(&value, &tmp);

			if (tmp == 517)
				tmp = 512;

			pr_display("mtu: %d:%d\n", *mtu, tmp);
			if (*mtu != tmp) {
				*mtu = tmp;
				if (g_rkbt_content->ble_content.cb_ble_recv_fun)
					g_rkbt_content->ble_content.cb_ble_recv_fun(NULL, (char *)mtu, NULL, RK_BLE_GATT_MTU);
				//dbus_message_iter_get_basic(&value, mtu);
			}
		} else if (strcasecmp(key, "device") == 0) {
			if (var != DBUS_TYPE_OBJECT_PATH)
				return -EINVAL;
			if (device)
				dbus_message_iter_get_basic(&value, device);
		} else if (strcasecmp(key, "link") == 0) {
			if (var != DBUS_TYPE_STRING)
				return -EINVAL;
			if (link)
				dbus_message_iter_get_basic(&value, link);
		} else if (strcasecmp(key, "prepare-authorize") == 0) {
			if (var != DBUS_TYPE_BOOLEAN)
				return -EINVAL;
			if (prep_authorize) {
				int tmp;

				dbus_message_iter_get_basic(&value, &tmp);
				*prep_authorize = !!tmp;
			}
		}

		dbus_message_iter_next(&dict);
	}

	return 0;
}

static void execute(const char cmdline[], char recv_buff[], int len)
{
	//pr_info("[GATT_CONFIG] execute: %s\n", cmdline);

	FILE *stream = NULL;
	char *tmp_buff = recv_buff;

	memset(recv_buff, 0, len);

	if ((stream = popen(cmdline, "r")) != NULL) {
		while (fgets(tmp_buff, len, stream)) {
			//pr_info("tmp_buf[%d]: %s\n", strlen(tmp_buff), tmp_buff);
			tmp_buff += strlen(tmp_buff);
			len -= strlen(tmp_buff);
			if (len <= 1)
				break;
		}
		//pr_info("[GATT_CONFIG] execute_r: %s \n", recv_buff);
		pclose(stream);
	}
}

static DBusMessage *chr_read_value(DBusConnection *conn, DBusMessage *msg,
							void *user_data)
{
	struct characteristic *chr = user_data;
	DBusMessage *reply;
	DBusMessageIter iter;
	char value[513];
	int len;
	//uint16_t offset = 0;
	char *device, *link;

	pr_info("=== chr_read_value enter ===\n");

	dbus_message_iter_init(msg, &iter);

	if (parse_options(&iter, NULL, &chr->mtu, &device, &link, NULL))
		return g_dbus_create_error(msg, DBUS_ERROR_INVALID_ARGS,
							"Invalid arguments");

	reply = dbus_message_new_method_return(msg);
	if (!reply)
		return g_dbus_create_error(msg, DBUS_ERROR_NO_MEMORY,
							"No Memory");

	dbus_message_iter_init_append(reply, &iter);

	memset(value, 0, 513);
	if (g_rkbt_content->ble_content.cb_ble_recv_fun)
		g_rkbt_content->ble_content.cb_ble_recv_fun(chr->uuid, value, &len, RK_BLE_GATT_SERVER_READ_BY_REMOTE);

	value[len] = 0;
	pr_info("=== chr_read_value value: %s[%d] ===\n", value, len);

	if (chr->value)
		g_free(chr->value);
	chr->value = g_memdup2(value, len);
	chr->vlen = len;
	chr_read(chr, &iter);

	pr_info("=== chr_read_value exit ===\n");
	return reply;
}

static DBusMessage *chr_write_value(DBusConnection *conn, DBusMessage *msg,
							void *user_data)
{
	pr_info("=== chr_write_value enter ===\n");
	struct characteristic *chr = user_data;
	DBusMessageIter iter;
	const uint8_t *value;
	int len;
	char *device = NULL, *link= NULL;
	char debug[513];

	dbus_message_iter_init(msg, &iter);

	if (parse_value(&iter, &value, &len))
		return g_dbus_create_error(msg, DBUS_ERROR_INVALID_ARGS,
							"Invalid arguments");

	if (parse_options(&iter, NULL, &chr->mtu, &device, &link, NULL))
		return g_dbus_create_error(msg,
					"org.bluez.Error.InvalidArguments",
					NULL);

	chr_write(chr, value, len);
	if (len == 0 || chr->value == NULL) {
		pr_info("chr_write_value is null\n");
		return dbus_message_new_method_return(msg);
	}

	memcpy(debug, chr->value, len);
	debug[len] = 0;
	pr_info("=== chr_write_value value: %s[%d] ===\n", debug, len);

	if (g_rkbt_content->ble_content.cb_ble_recv_fun)
		g_rkbt_content->ble_content.cb_ble_recv_fun(chr->uuid, (char *)chr->value, &len, RK_BLE_GATT_SERVER_WRITE_BY_REMOTE);
	else
		pr_info("cb_ble_recv_fun is null !!! \n");

	pr_info("=== chr_write_value exit ===\n");

	return dbus_message_new_method_return(msg);
}

static bool sock_hup(struct io *io, void *user_data)
{
	struct characteristic *chrc = user_data;

	pr_info("Attribute %s %s sock closed\n", chrc->path, "Notify");

	io_destroy(chrc->notify_io);
	chrc->notify_io = NULL;

	return false;
}

static struct io *sock_io_new(int fd, void *user_data)
{
	struct io *io;

	io = io_new(fd);

	io_set_close_on_destroy(io, true);

	io_set_disconnect_handler(io, sock_hup, user_data, NULL);

	return io;
}

static DBusMessage *create_sock(struct characteristic *chrc, DBusMessage *msg, char *device)
{
	int fds[2];
	struct io *io;
	//bool dir;
	DBusMessage *reply;

	if (socketpair(AF_LOCAL, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC,
								0, fds) < 0)
		return g_dbus_create_error(msg, "org.bluez.Error.Failed", "%s",
							strerror(errno));

	io = sock_io_new(fds[0], chrc);
	if (!io) {
		close(fds[0]);
		close(fds[1]);
		return g_dbus_create_error(msg, "org.bluez.Error.Failed", "%s",
							strerror(errno));
	}

	reply = g_dbus_create_reply(msg, DBUS_TYPE_UNIX_FD, &fds[1],
					DBUS_TYPE_UINT16, &chrc->mtu,
					DBUS_TYPE_INVALID);

	close(fds[1]);

	chrc->notify_io = io;

	pr_info("Attribute %s %s sock acquired\n",
					chrc->path, "Notify");

	return reply;
}

static DBusMessage *chrc_acquire_notify(DBusConnection *conn, DBusMessage *msg,
							void *user_data)
{
	struct characteristic *chrc = user_data;
	DBusMessageIter iter;
	DBusMessage *reply;
	char *device = NULL, *link = NULL;

	dbus_message_iter_init(msg, &iter);

	if (chrc->notify_io)
		return g_dbus_create_error(msg,
					"org.bluez.Error.NotPermitted",
					NULL);

	if (parse_options(&iter, NULL, &chrc->mtu, &device, &link, NULL))
		return g_dbus_create_error(msg,
					"org.bluez.Error.InvalidArguments",
					NULL);

	pr_info("AcquireNotify: %s link %s\n", device,
									link);

	reply = create_sock(chrc, msg, device);

	if (chrc->notify_io)
		g_dbus_emit_property_changed(conn, chrc->path, GATT_CHR_IFACE,
							"NotifyAcquired");

	return reply;
}

static DBusMessage *chrc_confirm(DBusConnection *conn, DBusMessage *msg,
							void *user_data)
{
	struct characteristic *chrc = user_data;

	pr_info("Attribute %s:%s indication confirm received\n", chrc->path, chrc->uuid);

	if (g_rkbt_content->ble_content.cb_ble_recv_fun)
		g_rkbt_content->ble_content.cb_ble_recv_fun(chrc->uuid, NULL, NULL, RK_BLE_GATT_SERVER_INDICATE_RESP_BY_REMOTE);

	return dbus_message_new_method_return(msg);
}

static DBusMessage *chrc_start_notify(DBusConnection *conn, DBusMessage *msg,
							void *user_data)
{
	struct characteristic *chrc = user_data;

	if (chrc->notifying)
		return g_dbus_create_reply(msg, DBUS_TYPE_INVALID);

	chrc->notifying = true;
	pr_info("Attribute %s (%s) notifications enabled\n", chrc->path, bt_uuidstr_to_str(chrc->uuid));
	g_dbus_emit_property_changed(conn, chrc->path, GATT_CHR_IFACE,
							"Notifying");

	if (g_rkbt_content->ble_content.cb_ble_recv_fun)
		g_rkbt_content->ble_content.cb_ble_recv_fun(chrc->uuid, NULL, NULL, RK_BLE_GATT_SERVER_ENABLE_NOTIFY_BY_REMOTE);

	return g_dbus_create_reply(msg, DBUS_TYPE_INVALID);
}

static DBusMessage *chrc_stop_notify(DBusConnection *conn, DBusMessage *msg,
							void *user_data)
{
	struct characteristic *chrc = user_data;

	if (!chrc->notifying)
		return g_dbus_create_reply(msg, DBUS_TYPE_INVALID);

	chrc->notifying = false;
	pr_info("Attribute %s (%s) notifications disabled\n", chrc->path, bt_uuidstr_to_str(chrc->uuid));
	g_dbus_emit_property_changed(conn, chrc->path, GATT_CHR_IFACE,
							"Notifying");

	if (g_rkbt_content->ble_content.cb_ble_recv_fun)
		g_rkbt_content->ble_content.cb_ble_recv_fun(chrc->uuid, NULL, NULL, RK_BLE_GATT_SERVER_DISABLE_NOTIFY_BY_REMOTE);

	return g_dbus_create_reply(msg, DBUS_TYPE_INVALID);
}

static const GDBusMethodTable chr_methods[] = {
	{ GDBUS_ASYNC_METHOD("ReadValue", GDBUS_ARGS({ "options", "a{sv}" }),
					GDBUS_ARGS({ "value", "ay" }),
					chr_read_value) },
	{ GDBUS_ASYNC_METHOD("WriteValue", GDBUS_ARGS({ "value", "ay" },
						{ "options", "a{sv}" }),
					NULL, chr_write_value) },
	//{ GDBUS_METHOD("AcquireNotify", GDBUS_ARGS({ "options", "a{sv}" }),
	//				NULL, chrc_acquire_notify) },
	{ GDBUS_METHOD("Confirm", NULL, NULL, chrc_confirm) },
	{ GDBUS_ASYNC_METHOD("StartNotify", NULL, NULL, chrc_start_notify) },
	{ GDBUS_METHOD("StopNotify", NULL, NULL, chrc_stop_notify) },
	{ }
};

static DBusMessage *desc_read_value(DBusConnection *conn, DBusMessage *msg,
							void *user_data)
{
	struct descriptor *desc = user_data;
	DBusMessage *reply;
	DBusMessageIter iter;
	char *device;

	if (!dbus_message_iter_init(msg, &iter))
		return g_dbus_create_error(msg, DBUS_ERROR_INVALID_ARGS,
							"Invalid arguments");

	if (parse_options(&iter, NULL, NULL, &device, NULL, NULL))
		return g_dbus_create_error(msg, DBUS_ERROR_INVALID_ARGS,
							"Invalid arguments");

	reply = dbus_message_new_method_return(msg);
	if (!reply)
		return g_dbus_create_error(msg, DBUS_ERROR_NO_MEMORY,
							"No Memory");

	dbus_message_iter_init_append(reply, &iter);

	desc_read(desc, &iter);

	return reply;
}

static DBusMessage *desc_write_value(DBusConnection *conn, DBusMessage *msg,
							void *user_data)
{
	struct descriptor *desc = user_data;
	DBusMessageIter iter;
	char *device;
	const uint8_t *value;
	int len;

	if (!dbus_message_iter_init(msg, &iter))
		return g_dbus_create_error(msg, DBUS_ERROR_INVALID_ARGS,
							"Invalid arguments");

	if (parse_value(&iter, &value, &len))
		return g_dbus_create_error(msg, DBUS_ERROR_INVALID_ARGS,
							"Invalid arguments");

	if (parse_options(&iter, NULL, NULL, &device, NULL, NULL))
		return g_dbus_create_error(msg, DBUS_ERROR_INVALID_ARGS,
							"Invalid arguments");

	desc_write(desc, value, len);

	return dbus_message_new_method_return(msg);
}

static const GDBusMethodTable desc_methods[] = {
	{ GDBUS_ASYNC_METHOD("ReadValue", GDBUS_ARGS({ "options", "a{sv}" }),
					GDBUS_ARGS({ "value", "ay" }),
					desc_read_value) },
	{ GDBUS_ASYNC_METHOD("WriteValue", GDBUS_ARGS({ "value", "ay" },
						{ "options", "a{sv}" }),
					NULL, desc_write_value) },
	{ }
};

static int unregister_ble(void)
{
	int i, j;


	for (i = 0; i < gatt_service_instance.srv_cnt; i++) {
		for (j = 0; j < gatt_service_instance.gatt_service[i].chr_cnt; j++) {
			pr_info("%s: chr_uuid[%d]: %s, gdesc[%d]->path: %s\n", __func__,
					i, gatt_service_instance.gatt_service[i].gchr[j]->uuid,
					i, gatt_service_instance.gatt_service[i].gchr[j]->path);
			g_dbus_unregister_interface(dbus_conn, gatt_service_instance.gatt_service[i].gchr[j]->path, GATT_CHR_IFACE);
		}
		pr_info("%s: gservice_path: %s\n", __func__, gatt_service_instance.gservice_path[i]);
		g_dbus_unregister_interface(dbus_conn, gatt_service_instance.gservice_path[i], GATT_SERVICE_IFACE);
	}

	return TRUE;
}

struct characteristic *register_characteristic(const char *chr_uuid,
						uint8_t *value, int vlen,
						char **props,
						char *desc_uuid,
						char **desc_props,
						char *service_path)
{
	struct characteristic *chr;
	struct descriptor *desc;

	chr = g_new0(struct characteristic, 1);
	chr->uuid = g_strdup(chr_uuid);
	chr->value = g_memdup2(value, vlen);
	chr->vlen = vlen;
	chr->props = (const char **)props;
	chr->service = g_strdup(service_path);
	chr->path = g_strdup_printf("%s/characteristic%d", service_path, characteristic_id++);

	pr_info("register_characteristic chr->uuid: %s, chr->path: %s\n", chr->uuid, chr->path);
	if (!g_dbus_register_interface(dbus_conn, chr->path, GATT_CHR_IFACE,
					chr_methods, NULL, chr_properties,
					chr, chr_iface_destroy)) {
		pr_info("Couldn't register characteristic interface\n");
		chr_iface_destroy(chr);
		return NULL;
	}
	pr_info("%s\n", __func__);

	if (!desc_uuid)
		return chr;

	desc = g_new0(struct descriptor, 1);
	desc->uuid = g_strdup(desc_uuid);
	desc->chr = chr;
	desc->props = desc_props;
	desc->path = g_strdup_printf("%s/descriptor%d", chr->path, characteristic_id++);

	if (!g_dbus_register_interface(dbus_conn, desc->path,
					GATT_DESCRIPTOR_IFACE,
					desc_methods, NULL, desc_properties,
					desc, desc_iface_destroy)) {
		pr_info("Couldn't register descriptor interface\n");
		g_dbus_unregister_interface(dbus_conn, chr->path,
							GATT_CHR_IFACE);

		desc_iface_destroy(desc);
		return NULL;
	}

	return chr;
}

static char *register_service(const char *uuid)
{
	//static int id = 1;
	char *path;

	path = g_strdup_printf("/service%d", service_id++);
	if (!g_dbus_register_interface(dbus_conn, path, GATT_SERVICE_IFACE,
				NULL, NULL, service_properties,
				g_strdup(uuid), g_free)) {
		pr_info("Couldn't register service interface\n");
		g_free(path);
		return NULL;
	}

	return path;
}

static void gatt_create_services(RkBtContent *bt_content)
{
	char *service_path;
	uint8_t level = ' ';
	int i, index;
	int srv_cnt = bt_content->ble_content.srv_cnt;
	RkBleGattService *gs;

	for (index = 0; index < srv_cnt; index++) {
		gs = &(bt_content->ble_content.gatt_instance[index]);

		pr_info("server_uuid: %s\n", gs->server_uuid.uuid);

		service_path = register_service(gs->server_uuid.uuid);
		if (!service_path)
			return;

		gatt_service_instance.gservice_path[index] = service_path;
		gatt_service_instance.srv_cnt++;

		int char_cnt = gs->chr_cnt;
		for (i = 0; i < char_cnt; i++) {
			pr_info("char_uuid[%d]: %s %s\n", i, gs->chr_uuid[i].uuid, gs->chr_uuid[i].chr_props[0]);

			struct characteristic *chr = register_characteristic(
								gs->chr_uuid[i].uuid,
								&level, sizeof(level),
								gs->chr_uuid[i].chr_props,
								NULL,
								desc_props,
								service_path);

			/* Add Alert Level Characteristic to Immediate Alert Service */
			if (!chr) {
				pr_info("Couldn't register characteristic.\n");
				g_dbus_unregister_interface(dbus_conn, service_path,
									GATT_SERVICE_IFACE);
				g_free(service_path);
				return;
			}
			gatt_service_instance.gatt_service[index].gchr[i] = chr;
			gatt_service_instance.gatt_service[index].chr_cnt++;
		}

		pr_info("Registered service: %s\n", service_path);
	}

}

gboolean gatt_write_data(gpointer data)//(char *uuid, void *data, int len)
{
	int i, j;
	struct characteristic *chr = NULL;
	RkBleConfig *ble_cfg =  data;

	//check some cond
	//if (!ble_dev) {
	//	pr_info("gatt_write_data: ble not connect!\n");
	//	return 0;
	//}

	pr_info("gatt_write uuid: [%s], len: [%d], data_ptr: %p\n",
			 ble_cfg->uuid, ble_cfg->len, ble_cfg->data);

	for (i = 0; i < gatt_service_instance.srv_cnt; i++) {
		for (j = 0; j < gatt_service_instance.gatt_service[i].chr_cnt; j++) {
			chr = gatt_service_instance.gatt_service[i].gchr[j];
			pr_info("gatt_write[%d] uuid: %s\n", i, chr->uuid);
			if (strncmp(chr->uuid, ble_cfg->uuid,
					   strlen("dfd4416e-1810-47f7-8248-eb8be3dc47f9")) == 0)
				goto found;
			chr = NULL;
		}
	}

	if (chr == NULL) {
		pr_info("gatt_write ??invaild uuid: %s.\n", ble_cfg->uuid);
		return -1;
	}

found:
#if 0
	for (int i = 0; i < 10000000; i++) {
		for (int j = 0; j < 255; j++) {
			memset((const uint8_t *)ble_cfg->data, j, 244);
			usleep(5 * 1000);
			chr_write(chr, (const uint8_t *)ble_cfg->data, 244);
		}
	}
#else
	chr_write(chr, (const uint8_t *)ble_cfg->data, 244);
#endif

	return 0;
}

extern void iBle_advertise(int enable);
extern int exec_command_system(const char *cmd);
gboolean ble_enable_adv(gpointer data)
{
	pr_display("%s id: %d\n", __func__, rk_gettid());

	if (!(g_rkbt_content->profile & PROFILE_BLE))
		return false;

	exec_command_system("echo 160 > /sys/kernel/debug/bluetooth/hci0/adv_min_interval");
	usleep(300 * 1000);
	exec_command_system("echo 160 > /sys/kernel/debug/bluetooth/hci0/adv_max_interval");
	usleep(300 * 1000);

	iBle_advertise(1);

	return false;
}

gboolean ble_disable_adv(gpointer data)
{
	pr_display("%s id: %d\n", __func__, rk_gettid());

	if (!(g_rkbt_content->profile & PROFILE_BLE))
		return false;

	iBle_advertise(0);

	return false;
}

static void register_app_reply(DBusMessage *reply, void *user_data)
{
	DBusError derr;

	dbus_error_init(&derr);
	dbus_set_error_from_message(&derr, reply);

	if (dbus_error_is_set(&derr))
		pr_info("RegisterApplication: %s\n", derr.message);
	else
		pr_info("RegisterApplication: OK\n");

	dbus_error_free(&derr);
}

static void register_app_setup(DBusMessageIter *iter, void *user_data)
{
	const char *path = "/";
	DBusMessageIter dict;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH, &path);

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "{sv}", &dict);

	/* TODO: Add options dictionary */

	dbus_message_iter_close_container(iter, &dict);
}

void register_app(GDBusProxy *proxy)
{
	if (!(g_rkbt_content->profile & PROFILE_BLE))
		return;

	ble_proxy = proxy;

	if (!g_dbus_proxy_method_call(proxy, "RegisterApplication",
					register_app_setup, register_app_reply,
					NULL, NULL)) {
		pr_info("Unable to call RegisterApplication\n");
		return;
	}
}

static void unregister_app_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		pr_info("Failed to unregister application: %s\n",
				error.name);
		dbus_error_free(&error);
		return;
	}

	pr_info("%s: Application unregistered\n", __func__);
}

static void unregister_app_setup(DBusMessageIter *iter, void *user_data)
{
	const char *path = "/";

	dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH, &path);
}

void unregister_app(GDBusProxy *proxy)
{
	if (g_dbus_proxy_method_call(proxy, "UnregisterApplication",
						unregister_app_setup,
						unregister_app_reply, NULL,
						NULL) == FALSE) {
		pr_info("Failed unregister profile\n");
		return;
	}
}

void gatt_cleanup(void)
{
	unregister_ble();

	for (int i = 0; i < gatt_service_instance.srv_cnt; i++)
		if (gatt_service_instance.gservice_path[i]) {
			g_free(gatt_service_instance.gservice_path[i]);
			gatt_service_instance.gservice_path[i] = NULL;
		}

	memset(&gatt_service_instance, 0, sizeof(struct rk_gatt));
}

extern void iBle_init_adv(DBusConnection *dbus_conn, RkBtContent *bt_content);
int gatt_init(RkBtContent *bt_content)
{
	characteristic_id = 1;
	service_id = 1;

	iBle_init_adv(dbus_conn, bt_content);

	/* CREATE GATT SERVICE */
	gatt_create_services(bt_content);

	return 0;
}

#define CMD_PARA		 "hcitool -i hci0 cmd 0x08 0x0006 A0 00 A0 00 00 01 00 00 00 00 00 00 00 07 00"
int ble_set_adv_interval(unsigned short adv_int_min, unsigned short adv_int_max)
{
	char ret_buff[128];
	char cmd_para[128];
	char adv_min_low, adv_min_high;
	char adv_max_low, adv_max_high;

	if (adv_int_min < 32) {
		pr_err("%s: the minimum is 32(20ms), adv_int_min = %d", __func__, adv_int_min);
		adv_int_min = 32;
	}

	if (adv_int_max < adv_int_min)
		adv_int_max = adv_int_min;

	adv_min_low = adv_int_min & 0xFF;
	adv_min_high = (adv_int_min & 0xFF00) >> 8;
	adv_max_low = adv_int_max & 0xFF;
	adv_max_high = (adv_int_max & 0xFF00) >> 8;

	memset(cmd_para, 0, 128);
	memcpy(cmd_para, CMD_PARA, strlen(CMD_PARA));
	if (sprintf(cmd_para, "%s %02hhx %02hhx %02hhx %02hhx %s",
			"hcitool -i hci0 cmd 0x08 0x0006",
			adv_min_low, adv_min_high, adv_max_low, adv_max_high,
			"00 01 00 00 00 00 00 00 00 07 00") < 0) {
		pr_err("%s: set ble adv interval failed\n", __func__);
		return -1;
	}
	pr_info("CMD_PARA: %zu, %s\n", strlen(cmd_para), cmd_para);

	execute(cmd_para, ret_buff, 128);
	pr_info("CMD_PARA buff: %s", ret_buff);

	return 1;
}
