/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2016  Intel Corporation. All rights reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "../gdbus/gdbus.h"
#include "util.h"
#include "shell.h"
#include "advertising.h"
#include "slog.h"
#include "RkBtBase.h"
#include "RkBtBase.h"
#include "a2dp_masterctrl.h"

#define AD_PATH "/org/bluez/advertising"
#define AD_IFACE "org.bluez.LEAdvertisement1"

struct ad_data {
	uint8_t data[25];
	uint8_t len;
};

struct service_data {
	char *uuid;
	struct ad_data data;
};

struct manufacturer_data {
	uint16_t id;
	struct ad_data data;
};

struct data {
	uint8_t type;
	struct ad_data data;
};

static struct ad {
	bool registered;
	char *type;
	char *local_name;
	uint16_t local_appearance;
	uint16_t duration;
	uint16_t timeout;
	uint16_t discoverable_to;
	char **uuids;
	size_t uuids_len;
	struct service_data service;
	struct manufacturer_data manufacturer;
	struct data data;
	bool discoverable;
	bool tx_power;
	bool name;
	bool appearance;
} ad = {
	.local_appearance = UINT16_MAX,
};

static void ad_release(DBusConnection *conn)
{
	ad.registered = false;

	g_dbus_unregister_interface(conn, AD_PATH, AD_IFACE);
}

static DBusMessage *release_advertising(DBusConnection *conn,
					DBusMessage *msg, void *user_data)
{
	pr_info("Advertising released\n");

	ad_release(conn);

	return dbus_message_new_method_return(msg);
}

static const GDBusMethodTable ad_methods[] = {
	{ GDBUS_METHOD("Release", NULL, NULL, release_advertising) },
	{ }
};

static void register_setup(DBusMessageIter *iter, void *user_data)
{
	DBusMessageIter dict;
	const char *path = AD_PATH;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH, &path);
	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
				DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
				DBUS_TYPE_STRING_AS_STRING
				DBUS_TYPE_VARIANT_AS_STRING
				DBUS_DICT_ENTRY_END_CHAR_AS_STRING, &dict);
	dbus_message_iter_close_container(iter, &dict);
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

		pr_info("UUID: %s(%s)\n", str, uuid);
	} else
		pr_info("UUID: (%s)\n", uuid ? uuid : "");
}

static void print_ad_uuids(void)
{
	char **uuid;

	for (uuid = ad.uuids; uuid && *uuid; uuid++)
		print_uuid(*uuid);
}

static void print_ad(void)
{
	print_ad_uuids();

	if (ad.service.uuid) {
		print_uuid(ad.service.uuid);
		bt_shell_hexdump(ad.service.data.data, ad.service.data.len);
	}

	if (ad.manufacturer.data.len) {
		pr_info("Manufacturer: %u\n", ad.manufacturer.id);
		bt_shell_hexdump(ad.manufacturer.data.data,
						ad.manufacturer.data.len);
	}

	if (ad.data.data.len) {
		pr_info("Data Type: 0x%02x\n", ad.data.type);
		bt_shell_hexdump(ad.data.data.data, ad.data.data.len);
	}

	pr_info("Tx Power: %s\n", ad.tx_power ? "on" : "off");

	if (ad.local_name)
		pr_info("LocalName: %s\n", ad.local_name);
	else
		pr_info("Name: %s\n", ad.name ? "on" : "off");

	if (ad.local_appearance != UINT16_MAX)
		pr_info("Appearance: %s (0x%04x)\n",
					bt_appear_to_str(ad.local_appearance),
					ad.local_appearance);
	else
		pr_info("Apperance: %s\n",
					ad.appearance ? "on" : "off");

	pr_info("Discoverable: %s\n", ad.discoverable ? "on": "off");

	if (ad.duration)
		pr_info("Duration: %u sec\n", ad.duration);

	if (ad.timeout)
		pr_info("Timeout: %u sec\n", ad.timeout);
}

static void register_reply(DBusMessage *message, void *user_data)
{
	DBusConnection *conn = user_data;
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == FALSE) {
		ad.registered = true;
		pr_info("Advertising object registered\n");
		print_ad();
		bt_state_send(NULL, RK_BT_STATE_ADAPTER_BLE_ADV_START);
		g_rkbt_content->ble_content.ble_advertised = true;
		/* Leave advertise running even on noninteractive mode */
	} else {
		pr_info("Failed to register advertisement: %s\n", error.name);
		dbus_error_free(&error);

		if (g_dbus_unregister_interface(conn, AD_PATH,
						AD_IFACE) == FALSE)
			pr_info("Failed to unregister advertising object\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}
}

static gboolean get_type(const GDBusPropertyTable *property,
				DBusMessageIter *iter, void *user_data)
{
	const char *type = "peripheral";

	if (ad.type && strlen(ad.type) > 0)
		type = ad.type;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &type);

	return TRUE;
}

static gboolean uuids_exists(const GDBusPropertyTable *property, void *data)
{
	return ad.uuids_len != 0;
}

static gboolean get_uuids(const GDBusPropertyTable *property,
				DBusMessageIter *iter, void *user_data)
{
	DBusMessageIter array;
	size_t i;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "as", &array);

	for (i = 0; i < ad.uuids_len; i++)
		dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING,
							&ad.uuids[i]);

	dbus_message_iter_close_container(iter, &array);

	return TRUE;
}

static gboolean service_data_exists(const GDBusPropertyTable *property,
								void *data)
{
	return ad.service.uuid != NULL;
}

static gboolean get_service_data(const GDBusPropertyTable *property,
				DBusMessageIter *iter, void *user_data)
{
	DBusMessageIter dict;
	struct ad_data *data = &ad.service.data;
	uint8_t *val = data->data;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "{sv}", &dict);

	g_dbus_dict_append_array(&dict, ad.service.uuid, DBUS_TYPE_BYTE, &val,
								data->len);

	dbus_message_iter_close_container(iter, &dict);

	return TRUE;
}

static gboolean manufacturer_data_exists(const GDBusPropertyTable *property,
								void *data)
{
	return ad.manufacturer.id != 0;
}

static gboolean get_manufacturer_data(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	DBusMessageIter dict;
	struct ad_data *data = &ad.manufacturer.data;
	uint8_t *val = data->data;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "{qv}", &dict);

	g_dbus_dict_append_basic_array(&dict, DBUS_TYPE_UINT16,
					&ad.manufacturer.id,
					DBUS_TYPE_BYTE, &val, data->len);

	dbus_message_iter_close_container(iter, &dict);

	return TRUE;
}

static gboolean includes_exists(const GDBusPropertyTable *property, void *data)
{
	return ad.tx_power || ad.name || ad.appearance;
}

static gboolean get_includes(const GDBusPropertyTable *property,
				DBusMessageIter *iter, void *user_data)
{
	DBusMessageIter array;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "as", &array);

	if (ad.tx_power) {
		const char *str = "tx-power";

		dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &str);
	}

	if (ad.name) {
		const char *str = "local-name";

		dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &str);
	}

	if (ad.appearance) {
		const char *str = "appearance";

		dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &str);
	}

	dbus_message_iter_close_container(iter, &array);


	return TRUE;
}

static gboolean local_name_exits(const GDBusPropertyTable *property, void *data)
{
	return ad.local_name ? TRUE : FALSE;
}

static gboolean get_local_name(const GDBusPropertyTable *property,
				DBusMessageIter *iter, void *user_data)
{
	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &ad.local_name);

	return TRUE;
}

static gboolean appearance_exits(const GDBusPropertyTable *property, void *data)
{
	return ad.local_appearance != UINT16_MAX ? TRUE : FALSE;
}

static gboolean get_appearance(const GDBusPropertyTable *property,
				DBusMessageIter *iter, void *user_data)
{
	dbus_message_iter_append_basic(iter, DBUS_TYPE_UINT16,
							&ad.local_appearance);

	return TRUE;
}

static gboolean duration_exits(const GDBusPropertyTable *property, void *data)
{
	return ad.duration;
}

static gboolean get_duration(const GDBusPropertyTable *property,
				DBusMessageIter *iter, void *user_data)
{
	dbus_message_iter_append_basic(iter, DBUS_TYPE_UINT16, &ad.duration);

	return TRUE;
}

static gboolean timeout_exits(const GDBusPropertyTable *property, void *data)
{
	return ad.timeout;
}

static gboolean get_timeout(const GDBusPropertyTable *property,
				DBusMessageIter *iter, void *user_data)
{
	dbus_message_iter_append_basic(iter, DBUS_TYPE_UINT16, &ad.timeout);

	return TRUE;
}

static gboolean data_exists(const GDBusPropertyTable *property, void *data)
{
	return ad.data.type != 0;
}

static gboolean get_data(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	DBusMessageIter dict;
	struct ad_data *data = &ad.data.data;
	uint8_t *val = data->data;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "{yv}", &dict);

	g_dbus_dict_append_basic_array(&dict, DBUS_TYPE_BYTE, &ad.data.type,
					DBUS_TYPE_BYTE, &val, data->len);

	dbus_message_iter_close_container(iter, &dict);

	return TRUE;
}

static gboolean discoverable_exists(const GDBusPropertyTable *property,
							void *data)
{
	return ad.discoverable;
}

static gboolean get_discoverable(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN,
							&ad.discoverable);

	return TRUE;
}

static gboolean discoverable_timeout_exits(const GDBusPropertyTable *property,
							void *data)
{
	return ad.discoverable_to;
}

static gboolean get_discoverable_timeout(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	dbus_message_iter_append_basic(iter, DBUS_TYPE_UINT16,
							&ad.discoverable_to);

	return TRUE;
}

static const GDBusPropertyTable ad_props[] = {
	{ "Type", "s", get_type },
	{ "ServiceUUIDs", "as", get_uuids, NULL, uuids_exists },
	{ "ServiceData", "a{sv}", get_service_data, NULL, service_data_exists },
	{ "ManufacturerData", "a{qv}", get_manufacturer_data, NULL,
						manufacturer_data_exists },
	{ "Data", "a{yv}", get_data, NULL, data_exists },
	{ "Discoverable", "b", get_discoverable, NULL, discoverable_exists },
	{ "DiscoverableTimeout", "q", get_discoverable_timeout, NULL,
						discoverable_timeout_exits },
	{ "Includes", "as", get_includes, NULL, includes_exists },
	{ "LocalName", "s", get_local_name, NULL, local_name_exits },
	{ "Appearance", "q", get_appearance, NULL, appearance_exits },
	{ "Duration", "q", get_duration, NULL, duration_exits },
	{ "Timeout", "q", get_timeout, NULL, timeout_exits },
	{ }
};

void ad_register(DBusConnection *conn, GDBusProxy *manager, const char *type)
{
	if (ad.registered) {
		pr_info("Advertisement is already registered\n");
		return;
	}

	g_free(ad.type);
	ad.type = g_strdup(type);

	if (g_dbus_register_interface(conn, AD_PATH, AD_IFACE, ad_methods,
					NULL, ad_props, NULL, NULL) == FALSE) {
		pr_info("Failed to register advertising object\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	if (g_dbus_proxy_method_call(manager, "RegisterAdvertisement",
					register_setup, register_reply,
					conn, NULL) == FALSE) {
		pr_info("Failed to register advertising\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}
}

static void unregister_setup(DBusMessageIter *iter, void *user_data)
{
	const char *path = AD_PATH;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH, &path);
}

static void unregister_reply(DBusMessage *message, void *user_data)
{
	DBusConnection *conn = user_data;
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == FALSE) {
		ad.registered = false;
		pr_info("Advertising object unregistered\n");
		if (g_dbus_unregister_interface(conn, AD_PATH,
							AD_IFACE) == FALSE)
			pr_info("Failed to unregister advertising"
					" object\n");
		bt_state_send(NULL, RK_BT_STATE_ADAPTER_BLE_ADV_STOP);
		g_rkbt_content->ble_content.ble_advertised = FALSE;
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);
	} else {
		pr_info("Failed to unregister advertisement: %s\n",
								error.name);
		bt_state_send(NULL, RK_BT_STATE_ADAPTER_BLE_ADV_ERR);
		dbus_error_free(&error);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}
}

void ad_unregister(DBusConnection *conn, GDBusProxy *manager)
{
	if (!manager)
		ad_release(conn);

	if (!ad.registered)
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);

	g_free(ad.type);
	ad.type = NULL;

	if (g_dbus_proxy_method_call(manager, "UnregisterAdvertisement",
					unregister_setup, unregister_reply,
					conn, NULL) == FALSE) {
		pr_info("Failed to unregister advertisement method\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}
}

static void ad_clear_uuids(void)
{
	//g_strfreev(ad.uuids);
	ad.uuids = NULL;
	ad.uuids_len = 0;
}

void ad_advertise_uuids(DBusConnection *conn, int argc, char *argv[])
{
	if (argc < 2 || !strlen(argv[1])) {
		print_ad_uuids();
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);
	}

	ad_clear_uuids();

	ad.uuids = g_strdupv(&argv[1]);
	if (!ad.uuids) {
		pr_info("Failed to parse input\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	ad.uuids_len = g_strv_length(ad.uuids);

	g_dbus_emit_property_changed(conn, AD_PATH, AD_IFACE, "ServiceUUIDs");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

void ad_disable_uuids(DBusConnection *conn)
{
	if (!ad.uuids)
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);

	ad_clear_uuids();
	g_dbus_emit_property_changed(conn, AD_PATH, AD_IFACE, "ServiceUUIDs");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void ad_clear_service(void)
{
	g_free(ad.service.uuid);
	memset(&ad.service, 0, sizeof(ad.service));

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static bool ad_add_data(struct ad_data *data, int argc, char *argv[])
{
	unsigned int i;

	memset(data, 0, sizeof(*data));

	for (i = 0; i < (unsigned int) argc; i++) {
		long int val;
		char *endptr = NULL;

		if (i >= G_N_ELEMENTS(data->data)) {
			pr_info("Too much data\n");
			return false;
		}

		val = strtol(argv[i], &endptr, 0);
		if (!endptr || *endptr != '\0' || val > UINT8_MAX) {
			pr_info("Invalid value at index %d\n", i);
			return false;
		}

		data->data[data->len] = val;
		data->len++;
	}

	return true;
}

void ad_advertise_service(DBusConnection *conn, int argc, char *argv[])
{
	struct ad_data data;

	if (argc < 2 || !strlen(argv[1])) {
		if (ad.service.uuid) {
			print_uuid(ad.service.uuid);
			bt_shell_hexdump(ad.service.data.data,
						ad.service.data.len);
		}
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);
	}

	if (!ad_add_data(&data, argc - 2, argv + 2))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	ad_clear_service();

	ad.service.uuid = g_strdup(argv[1]);
	ad.service.data = data;

	g_dbus_emit_property_changed(conn, AD_PATH, AD_IFACE, "ServiceData");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

void ad_disable_service(DBusConnection *conn)
{
	if (!ad.service.uuid)
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);

	ad_clear_service();
	g_dbus_emit_property_changed(conn, AD_PATH, AD_IFACE, "ServiceData");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void ad_clear_manufacturer(void)
{
	memset(&ad.manufacturer, 0, sizeof(ad.manufacturer));

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

void ad_advertise_manufacturer(DBusConnection *conn, int argc, char *argv[])
{
	char *endptr = NULL;
	long int val;
	struct ad_data data;

	if (argc < 2 || !strlen(argv[1])) {
		if (ad.manufacturer.data.len) {
			pr_info("Manufacturer: %u\n",
						ad.manufacturer.id);
			bt_shell_hexdump(ad.manufacturer.data.data,
						ad.manufacturer.data.len);
		}

		return bt_shell_noninteractive_quit(EXIT_SUCCESS);
	}

	val = strtol(argv[1], &endptr, 0);
	if (!endptr || *endptr != '\0' || val > UINT16_MAX) {
		pr_info("Invalid manufacture id\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	if (!ad_add_data(&data, argc - 2, argv + 2))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	ad_clear_manufacturer();
	ad.manufacturer.id = val;
	ad.manufacturer.data = data;

	g_dbus_emit_property_changed(conn, AD_PATH, AD_IFACE,
							"ManufacturerData");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

void ad_disable_manufacturer(DBusConnection *conn)
{
	if (!ad.manufacturer.id && !ad.manufacturer.data.len)
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);

	ad_clear_manufacturer();
	g_dbus_emit_property_changed(conn, AD_PATH, AD_IFACE,
							"ManufacturerData");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void ad_clear_data(void)
{
	memset(&ad.manufacturer, 0, sizeof(ad.manufacturer));

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

void ad_advertise_data(DBusConnection *conn, int argc, char *argv[])
{
	char *endptr = NULL;
	long int val;
	struct ad_data data;

	if (argc < 2 || !strlen(argv[1])) {
		if (ad.manufacturer.data.len) {
			pr_info("Type: 0x%02x\n", ad.data.type);
			bt_shell_hexdump(ad.data.data.data, ad.data.data.len);
		}

		return bt_shell_noninteractive_quit(EXIT_SUCCESS);
	}

	val = strtol(argv[1], &endptr, 0);
	if (!endptr || *endptr != '\0' || val > UINT8_MAX) {
		pr_info("Invalid type\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	if (!ad_add_data(&data, argc - 2, argv + 2))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	ad_clear_data();
	ad.data.type = val;
	ad.data.data = data;

	g_dbus_emit_property_changed(conn, AD_PATH, AD_IFACE, "Data");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

void ad_disable_data(DBusConnection *conn)
{
	if (!ad.data.type && !ad.data.data.len)
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);

	ad_clear_data();
	g_dbus_emit_property_changed(conn, AD_PATH, AD_IFACE, "Data");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

void ad_advertise_discoverable(DBusConnection *conn, dbus_bool_t *value)
{
	if (!value) {
		pr_info("Discoverable: %s\n",
				ad.discoverable ? "on" : "off");
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);
	}

	if (ad.discoverable == *value)
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);

	ad.discoverable = *value;

	g_dbus_emit_property_changed(conn, AD_PATH, AD_IFACE, "Discoverable");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

void ad_advertise_discoverable_timeout(DBusConnection *conn, long int *value)
{
	if (!value) {
		if (ad.discoverable_to)
			pr_info("Timeout: %u sec\n",
					ad.discoverable_to);
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);
	}

	if (ad.discoverable_to == *value)
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);

	ad.discoverable_to = *value;

	g_dbus_emit_property_changed(conn, AD_PATH, AD_IFACE,
					"DiscoverableTimeout");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

void ad_advertise_tx_power(DBusConnection *conn, dbus_bool_t *value)
{
	if (!value) {
		pr_info("Tx Power: %s\n", ad.tx_power ? "on" : "off");
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);
	}

	if (ad.tx_power == *value)
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);

	ad.tx_power = *value;

	g_dbus_emit_property_changed(conn, AD_PATH, AD_IFACE, "Includes");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

void ad_advertise_name(DBusConnection *conn, bool value)
{
	if (ad.name == value)
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);

	ad.name = value;

	if (!value) {
		free(ad.local_name);
		ad.local_name = NULL;
	}

	g_dbus_emit_property_changed(conn, AD_PATH, AD_IFACE, "Includes");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

void ad_advertise_local_name(DBusConnection *conn, const char *name)
{
	if (!name) {
		if (ad.local_name)
			pr_info("LocalName: %s\n", ad.local_name);
		else
			pr_info("Name: %s\n", ad.name ? "on" : "off");

		return bt_shell_noninteractive_quit(EXIT_SUCCESS);
	}

	if (ad.local_name && !strcmp(name, ad.local_name))
		return;

	g_free(ad.local_name);
	ad.local_name = strdup(name);

	g_dbus_emit_property_changed(conn, AD_PATH, AD_IFACE, "LocalName");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

void ad_advertise_appearance(DBusConnection *conn, bool value)
{
	if (ad.appearance == value)
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);

	ad.appearance = value;

	if (!value)
		ad.local_appearance = UINT16_MAX;

	g_dbus_emit_property_changed(conn, AD_PATH, AD_IFACE, "Includes");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

void ad_advertise_local_appearance(DBusConnection *conn, long int *value)
{
	if (!value) {
		if (ad.local_appearance != UINT16_MAX)
			pr_info("Appearance: %s (0x%04x)\n",
					bt_appear_to_str(ad.local_appearance),
					ad.local_appearance);
		else
			pr_info("Apperance: %s\n",
					ad.appearance ? "on" : "off");

		return bt_shell_noninteractive_quit(EXIT_SUCCESS);
	}

	if (ad.local_appearance == *value)
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);

	ad.local_appearance = *value;

	g_dbus_emit_property_changed(conn, AD_PATH, AD_IFACE, "Appearance");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

void ad_advertise_duration(DBusConnection *conn, long int *value)
{
	if (!value) {
		if (ad.duration)
			pr_info("Duration: %u sec\n", ad.duration);
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);
	}

	if (ad.duration == *value)
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);

	ad.duration = *value;

	g_dbus_emit_property_changed(conn, AD_PATH, AD_IFACE, "Duration");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

void ad_advertise_timeout(DBusConnection *conn, long int *value)
{
	if (!value) {
		if (ad.timeout)
			pr_info("Timeout: %u sec\n", ad.timeout);
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);
	}

	if (ad.timeout == *value)
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);

	ad.timeout = *value;

	g_dbus_emit_property_changed(conn, AD_PATH, AD_IFACE, "Timeout");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

#include "RkBtBase.h"
void iBle_init_adv(DBusConnection *dbus_conn, RkBtContent *bt_content)
{
	pr_info("iBle_init_adv\n");
	static char *guuid[2];

	//service uuid
	//ad_clear_uuids();
	//ad.uuids = g_strdupv(&(bt_content->ble_content.server_uuid.uuid));
	if (bt_content->ble_content.adv_server_uuid.uuid) {
		guuid[0] = bt_content->ble_content.adv_server_uuid.uuid;
		guuid[1] = NULL;
		ad.uuids = guuid;
		pr_info("ad.server.uuids: %s[%s]\n", ad.uuids[0], guuid[0]);
		ad.uuids_len = 1;
		g_dbus_emit_property_changed_full(dbus_conn, AD_PATH, AD_IFACE, "ServiceUUIDs", G_DBUS_PROPERTY_CHANGED_FLAG_FLUSH);
	}

	//local name
	if (bt_content->ble_content.ble_name) {
		if (ad.local_name)
			g_free(ad.local_name);
		ad.local_name = strdup(bt_content->ble_content.ble_name);
		g_dbus_emit_property_changed_full(dbus_conn, AD_PATH, AD_IFACE, "LocalName", G_DBUS_PROPERTY_CHANGED_FLAG_FLUSH);
	}

	//tx power
	if (bt_content->ble_content.tx_power) {
		ad.tx_power = bt_content->ble_content.tx_power;
		g_dbus_emit_property_changed_full(dbus_conn, AD_PATH, AD_IFACE, "Includes", G_DBUS_PROPERTY_CHANGED_FLAG_FLUSH);
	}

	//Appearance 0xFCF6
	if (bt_content->ble_content.Appearance) {
		ad.local_appearance = bt_content->ble_content.Appearance;
		g_dbus_emit_property_changed_full(dbus_conn, AD_PATH, AD_IFACE, "Appearance", G_DBUS_PROPERTY_CHANGED_FLAG_FLUSH);
	}

	//manufacturer
	if (bt_content->ble_content.manufacturer_id) {
		ad_clear_manufacturer();
		ad.manufacturer.id = bt_content->ble_content.manufacturer_id;
		memcpy(ad.manufacturer.data.data, bt_content->ble_content.manufacturer_data, 16);
		ad.manufacturer.data.len = 16;
		g_dbus_emit_property_changed_full(dbus_conn, AD_PATH, AD_IFACE, "ManufacturerData", G_DBUS_PROPERTY_CHANGED_FLAG_FLUSH);
	}

	//Discoverable (BT_AD_FLAG_GENERAL)
	ad.discoverable = true;
	g_dbus_emit_property_changed_full(dbus_conn, AD_PATH, AD_IFACE, "Discoverable", G_DBUS_PROPERTY_CHANGED_FLAG_FLUSH);

	//DiscoverableTimeout (BT_AD_FLAG_LIMITED) 
	/*
	if (bt_content->ble_content.discoverable_to) {
		ad.discoverable_to = 1000;
		g_dbus_emit_property_changed_full(dbus_conn, AD_PATH, AD_IFACE, "DiscoverableTimeout", G_DBUS_PROPERTY_CHANGED_FLAG_FLUSH);
	}
	*/
}
