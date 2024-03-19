#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/signalfd.h>

#include "gdbus/gdbus.h"
#include "avrcpctrl.h"
#include "a2dp_source/a2dp_masterctrl.h"
#include "slog.h"

/* String display constants */
#define COLORED_NEW	"NEW"
#define COLORED_CHG	"CHG"
#define COLORED_DEL	"DEL"

#define BLUEZ_MEDIA_INTERFACE "org.bluez.Media1"
#define BLUEZ_MEDIA_PLAYER_INTERFACE "org.bluez.MediaPlayer1"
#define BLUEZ_MEDIA_FOLDER_INTERFACE "org.bluez.MediaFolder1"
#define BLUEZ_MEDIA_ITEM_INTERFACE "org.bluez.MediaItem1"
#define BLUEZ_MEDIA_TRANSPORT_INTERFACE "org.bluez.MediaTransport1"
#define BLUEZ_MEDIA_ENDPOINT_INTERFACE "org.bluez.MediaEndpoint1"

extern DBusConnection *dbus_conn;
static GDBusProxy *default_player;
static GDBusProxy *default_transport;
//static GList *medias = NULL;
static GList *players = NULL;
static GList *folders = NULL;
static GList *items = NULL;
static GList *endpoints = NULL;
static GList *transports = NULL;
uint16_t transport_volume;

extern void print_iter(const char *label, const char *name, DBusMessageIter *iter);

typedef struct {
	RK_BT_AVRCP_TRACK_CHANGE_CB avrcp_track_cb;
	RK_BT_AVRCP_PLAY_POSITION_CB avrcp_position_cb;
} avrcp_callback_t;

static avrcp_callback_t g_avrcp_cb = {
	NULL, NULL,
};

static char track_key[256];
static unsigned int current_song_len = 0;

static RkBtMedia transport_media;

void folder_removed(GDBusProxy *proxy)
{
	pr_display("folder_removed %s\n", g_dbus_proxy_get_path(proxy));
	folders = g_list_remove(folders, proxy);
}

char *proxy_description(GDBusProxy *proxy, const char *title,
						const char *description)
{
	const char *path;

	path = g_dbus_proxy_get_path(proxy);

	return g_strdup_printf("%s %s ", title, path);
}

void player_added(GDBusProxy *proxy)
{
	pr_display("Add player: %s\n", g_dbus_proxy_get_path(proxy));

	players = g_list_append(players, proxy);
	default_player = proxy;

	//if (default_player == NULL) {
	//	default_player = proxy;
	//}
}

void print_item(GDBusProxy *proxy, const char *description)
{
	const char *path, *name;
	DBusMessageIter iter;

	path = g_dbus_proxy_get_path(proxy);

	if (g_dbus_proxy_get_property(proxy, "Name", &iter))
		dbus_message_iter_get_basic(&iter, &name);
	else
		name = "<unknown>";

	pr_info("path %s, name %s\n", path, name);
}

void item_added(GDBusProxy *proxy)
{
	items = g_list_append(items, proxy);

	print_item(proxy, COLORED_NEW);
}

void folder_added(GDBusProxy *proxy)
{
	pr_display("folder_added %s\n", g_dbus_proxy_get_path(proxy));
	folders = g_list_append(folders, proxy);
}

static void volume_callback(const DBusError *error, void *user_data)
{
	if (dbus_error_is_set(error)) {
		pr_info("Failed to set Volume: %s\n", error->name);
		return;
	}

	pr_info("Changing Volume succeeded\n");

	return;
}

int transport_set_volume(int volume)
{
	if (default_transport == NULL)
		return -1;

	if (!g_dbus_proxy_set_property_basic(default_transport, "Volume", DBUS_TYPE_UINT16,
						&volume, volume_callback,
						NULL, NULL)) {
		pr_info("Failed release transport\n");
		return -1;
	}

	return 0;
}

static void transport_added(GDBusProxy *proxy)
{
	char *path, *uuid;
	DBusMessageIter iter, array;
	uint8_t codec  = 0xff;
	uint8_t *value;
	int value_len = 0;

	transports = g_list_append(transports, proxy);
	default_transport = proxy;

	if (g_dbus_proxy_get_property(proxy, "UUID", &iter))
		dbus_message_iter_get_basic(&iter, &uuid);

	if (g_dbus_proxy_get_property(proxy, "Device", &iter))
		dbus_message_iter_get_basic(&iter, &path);

	//INFO
	pr_display("transport_added %s UUID: %s PATH: %s\n", g_dbus_proxy_get_path(proxy), uuid, path);

	if (g_dbus_proxy_get_property(proxy, "Codec", &iter))
		dbus_message_iter_get_basic(&iter, &codec);

	if (g_dbus_proxy_get_property(proxy, "Configuration", &iter)) {
		dbus_message_iter_recurse(&iter, &array);
		dbus_message_iter_get_fixed_array(&array, &value, &value_len);
	}

	//only support SBC
	if (value_len != 4)
		return;

	//Codec
	transport_media.codec = codec;
	//Configuration
	memcpy(&transport_media.sbc, value, value_len);
	//UUID
	memcpy(&transport_media.remote_uuids, uuid, strlen(uuid));

	pr_info("codec: %s, freq: %s(0x%x), chn: 0x%x\n",
				transport_media.codec == 0 ? "SBC" : "UNKNOW",
				transport_media.sbc.frequency == 1 ? "48K" : "44.1K",
				transport_media.sbc.frequency, transport_media.sbc.channel_mode);

	if (!strcasecmp(uuid, BT_UUID_A2DP_SOURCE))
		bt_media_state_change(path, "Media", RK_BT_STATE_SRC_ADD, &transport_media);

	if (!strcasecmp(uuid, BT_UUID_A2DP_SINK))
		bt_media_state_change(path, "Media", RK_BT_STATE_SINK_ADD, &transport_media);
}

static void endpoint_added(GDBusProxy *proxy)
{
	pr_display("endpoint_added %s\n", g_dbus_proxy_get_path(proxy));
	endpoints = g_list_append(endpoints, proxy);
}

void a2dp_sink_proxy_added(GDBusProxy *proxy, void *user_data)
{
	const char *interface;

	interface = g_dbus_proxy_get_interface(proxy);
	pr_info("interface:%s \n", interface);

	if (!strcmp(interface, BLUEZ_MEDIA_PLAYER_INTERFACE))
		player_added(proxy);
	else if (!strcmp(interface, BLUEZ_MEDIA_FOLDER_INTERFACE))
		folder_added(proxy);
	else if (!strcmp(interface, BLUEZ_MEDIA_ITEM_INTERFACE))
		item_added(proxy);
	else if (!strcmp(interface, BLUEZ_MEDIA_ENDPOINT_INTERFACE))
		endpoint_added(proxy);
	else if (!strcmp(interface, BLUEZ_MEDIA_TRANSPORT_INTERFACE))
		transport_added(proxy);
}

 void player_removed(GDBusProxy *proxy)
{
	//GList *list;

	pr_display("Remove player: %s\n", g_dbus_proxy_get_path(proxy));

	if (default_player == proxy) {
		default_player = NULL;
	}

	//players = g_list_remove(players, proxy);

	//list = g_list_last(players);
	//if (list !=  NULL) {
	//	default_player = (GDBusProxy *)list->data;
	//	pr_display("Set new default player: %s\n", g_dbus_proxy_get_path(default_player));
	//}
}

void item_removed(GDBusProxy *proxy)
{
	items = g_list_remove(items, proxy);

	print_item(proxy, COLORED_DEL);
}

static void endpoint_removed(GDBusProxy *proxy)
{
	endpoints = g_list_remove(endpoints, proxy);
	pr_display("endpoint_removed %s\n", g_dbus_proxy_get_path(proxy));
}

static void transport_removed(GDBusProxy *proxy)
{
	char *path, *uuid;
	DBusMessageIter iter;
	pr_display("transport_removed %s\n", g_dbus_proxy_get_path(proxy));

	if (g_dbus_proxy_get_property(proxy, "Device", &iter))
		dbus_message_iter_get_basic(&iter, &path);

	if (g_dbus_proxy_get_property(proxy, "UUID", &iter))
		dbus_message_iter_get_basic(&iter, &uuid);

	//INFO
	pr_display("	UUID: [%s] PATH: [%s]\n", uuid, path);

	if (!strcasecmp(uuid, BT_UUID_A2DP_SOURCE))
		bt_media_state_change(path, "Media", RK_BT_STATE_SRC_DEL, &transport_media);

	if (!strcasecmp(uuid, BT_UUID_A2DP_SINK))
		bt_media_state_change(path, "Media", RK_BT_STATE_SINK_DEL, &transport_media);

	transports = g_list_remove(transports, proxy);
}

 void a2dp_sink_proxy_removed(GDBusProxy *proxy, void *user_data)
{
	const char *interface;

	interface = g_dbus_proxy_get_interface(proxy);
	pr_info("interface:%s \n", interface);

	if (!strcmp(interface, BLUEZ_MEDIA_PLAYER_INTERFACE))
		player_removed(proxy);
	if (!strcmp(interface, BLUEZ_MEDIA_FOLDER_INTERFACE))
		folder_removed(proxy);
	if (!strcmp(interface, BLUEZ_MEDIA_ITEM_INTERFACE))
		item_removed(proxy);
	if (!strcmp(interface, BLUEZ_MEDIA_ENDPOINT_INTERFACE))
		endpoint_removed(proxy);
	if (!strcmp(interface, BLUEZ_MEDIA_TRANSPORT_INTERFACE))
		transport_removed(proxy);
}

static void save_track_info(BtTrackInfo *track, const char *valstr, unsigned int valu32)
{
	if (!strcmp(track_key, "Title") && valstr != NULL)
		memcpy(track->title, valstr, strlen(valstr));
	else if(!strcmp(track_key, "Album") && valstr != NULL)
		memcpy(track->album, valstr, strlen(valstr));
	else if(!strcmp(track_key, "Artist") && valstr != NULL)
		memcpy(track->artist, valstr, strlen(valstr));
	else if(!strcmp(track_key, "Genre") && valstr != NULL)
		memcpy(track->genre, valstr, strlen(valstr));
	else if(!strcmp(track_key, "TrackNumber"))
		sprintf(track->track_num, "%d", valu32);
	else if(!strcmp(track_key, "NumberOfTracks"))
		sprintf(track->num_tracks, "%d", valu32);
	else if(!strcmp(track_key, "Duration")) {
		sprintf(track->playing_time, "%d", valu32);
		current_song_len = valu32;
	}

	memset(track_key, 0, 256);
}

static void avrcp_get_track_info(BtTrackInfo *track, const char *name,
					 DBusMessageIter *iter)
{
	dbus_uint32_t valu32;
	const char *valstr;
	DBusMessageIter subiter;
	char *entry;

	switch (dbus_message_iter_get_arg_type(iter)) {
	case DBUS_TYPE_STRING:
		dbus_message_iter_get_basic(iter, &valstr);
		if(!strncmp(name, "Track Key", 9)) {
			memcpy(track_key, valstr, strlen(valstr));
		}
		else if(!strncmp(name, "Track Value", 11)) {
			save_track_info(track, valstr, 0);
		}
		break;
	case DBUS_TYPE_UINT32:
		dbus_message_iter_get_basic(iter, &valu32);
		if(!strncmp(name, "Track Value", 11)) {
			save_track_info(track, NULL, (unsigned int)valu32);
		}
		break;
	case DBUS_TYPE_VARIANT:
		dbus_message_iter_recurse(iter, &subiter);
		avrcp_get_track_info(track, name, &subiter);
		break;
	case DBUS_TYPE_ARRAY:
		dbus_message_iter_recurse(iter, &subiter);
		while (dbus_message_iter_get_arg_type(&subiter) !=
							DBUS_TYPE_INVALID) {
			avrcp_get_track_info(track, name, &subiter);
			dbus_message_iter_next(&subiter);
		}
		break;
	case DBUS_TYPE_DICT_ENTRY:
		dbus_message_iter_recurse(iter, &subiter);
		entry = g_strconcat(name, " Key", NULL);
		avrcp_get_track_info(track, entry, &subiter);
		g_free(entry);

		entry = g_strconcat(name, " Value", NULL);
		dbus_message_iter_next(&subiter);
		avrcp_get_track_info(track, entry, &subiter);
		g_free(entry);
		break;
	}
}

static bool avrcp_track_info_get(const char *name, DBusMessageIter *iter, BtTrackInfo *track)
{
	if (track == NULL) {
		return FALSE;
	}

	if (strncmp(name, "Track", 5))
		return FALSE;

	memset(track_key, 0, 256);
	memset(track, 0, sizeof(BtTrackInfo));
	avrcp_get_track_info(track, name, iter);

	if(track->title[0]!=0 ||track->artist[0]!=0 )
		return TRUE;

	return FALSE;
}

void player_property_changed(GDBusProxy *proxy, const char *name,
					 DBusMessageIter *iter)
{
	char *str, *string;
	char *path = (char *)g_dbus_proxy_get_path(proxy);
	char dev_path[37 + 1];
	BtTrackInfo track;

	strncpy(dev_path, path, 37);
	dev_path[37] = 0;

	str = proxy_description(proxy, "Player", COLORED_CHG);
	pr_info("player_property_changed: str: %s, name: %s\n", str, name);

	if (!strncmp(name, "Position", 8)) {
		dbus_uint32_t valu32;
		dbus_message_iter_get_basic(iter, &valu32);
		if(current_song_len != 0 && (valu32 < current_song_len)) {
			struct _song_lens{
				unsigned int position;
				unsigned int total;
			} song_lens = {valu32, current_song_len};
			bt_media_state_change(dev_path, "position", RK_BT_STATE_SINK_POSITION, &song_lens);
		}
	}

	if (!strncmp(name, "Status", 8)) {
		dbus_message_iter_get_basic(iter, &string);
		if (strstr(string, "playing"))
			bt_media_state_change(dev_path, "player", RK_BT_STATE_SINK_PLAY, NULL);
		else if (strstr(string, "paused"))
			bt_media_state_change(dev_path, "player", RK_BT_STATE_SINK_PAUSE, NULL);
		else if (strstr(string, "stopped"))
			bt_media_state_change(dev_path, "player", RK_BT_STATE_SINK_STOP, NULL);
	}

	if (avrcp_track_info_get(name, iter, &track)) {
		bt_media_state_change(dev_path, "track", RK_BT_STATE_SINK_TRACK, &track);
	}
	//avrcp_position_send(name, iter);

	print_iter(str, name, iter);
	g_free(str);
}

void folder_property_changed(GDBusProxy *proxy, const char *name,
					 DBusMessageIter *iter)
{
	char *str;

	str = proxy_description(proxy, "Folder", COLORED_CHG);
	print_iter(str, name, iter);
	g_free(str);
}

void item_property_changed(GDBusProxy *proxy, const char *name,
					DBusMessageIter *iter)
{
	char *str;

	str = proxy_description(proxy, "Item", COLORED_CHG);
	print_iter(str, name, iter);
	g_free(str);
}

void transport_property_changed(GDBusProxy *proxy, const char *name,
					DBusMessageIter *iter)
{
	char *str;
	const char *valstr;
	dbus_uint16_t valu16;

	// /org/bluez/hci0/dev_F8_7D_76_F2_12_F3/player0
	char *path = (char *)g_dbus_proxy_get_path(proxy);
	char dev_path[37 + 1];
	strncpy(dev_path, path, 37);
	dev_path[37] = 0;

	str = proxy_description(proxy, "MediaTransport1", COLORED_CHG);

	if (!strncmp(name, "State", 5)) {
		dbus_message_iter_get_basic(iter, &valstr);

		if (strstr(valstr, "active")) {
			transport_media.state = RK_BT_STATE_TRANSPORT_ACTIVE;
			bt_media_state_change(dev_path, "Media", RK_BT_STATE_TRANSPORT_ACTIVE, &transport_media);
		} else if (strstr(valstr, "idle")) {
			transport_media.state = RK_BT_STATE_TRANSPORT_IDLE;
			bt_media_state_change(dev_path, "Media", RK_BT_STATE_TRANSPORT_IDLE, &transport_media);
		}
	}

	if (!strncmp(name, "Volume", 6)) {
		dbus_message_iter_get_basic(iter, &valu16);
		transport_media.volume = valu16;
		transport_media.state = RK_BT_STATE_TRANSPORT_VOLUME;
		bt_media_state_change(dev_path, "Media", RK_BT_STATE_TRANSPORT_VOLUME, &transport_media);
	}

	print_iter(str, name, iter);
	g_free(str);
}

static void endpoint_property_changed(GDBusProxy *proxy, const char *name,
						DBusMessageIter *iter)
{
	char *str;

	str = proxy_description(proxy, "Endpoint", COLORED_CHG);
	print_iter(str, name, iter);
	g_free(str);
}

static void property_changed(GDBusProxy *proxy, const char *name,
					 DBusMessageIter *iter, void *user_data)
{
	const char *interface;

	interface = g_dbus_proxy_get_interface(proxy);
	pr_info("BT A2DP: property_changed %s\n", interface);

	if (!strcmp(interface, BLUEZ_MEDIA_PLAYER_INTERFACE))
		player_property_changed(proxy, name, iter);
	else if (!strcmp(interface, BLUEZ_MEDIA_FOLDER_INTERFACE))
		folder_property_changed(proxy, name, iter);
	else if (!strcmp(interface, BLUEZ_MEDIA_ITEM_INTERFACE))
		item_property_changed(proxy, name, iter);
	else if (!strcmp(interface, BLUEZ_MEDIA_ENDPOINT_INTERFACE))
		endpoint_property_changed(proxy, name, iter);
	else if (!strcmp(interface, BLUEZ_MEDIA_TRANSPORT_INTERFACE))
		transport_property_changed(proxy, name, iter);
}

void a2dp_sink_property_changed(GDBusProxy *proxy, const char *name,
					 DBusMessageIter *iter, void *user_data)
{
	property_changed(proxy, name, iter, user_data);
}

bool check_default_player(void)
{
	if (!default_player) {
		pr_info("No default player available\n");
		return FALSE;
	}

	return TRUE;
}

void play_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		pr_info("Failed to play\n");
		dbus_error_free(&error);
		return;
	}

	pr_info("Play successful\n");
}

int play_avrcp(void)
{
	if (!check_default_player())
		return -1;

	if (g_dbus_proxy_method_call(default_player, "Play", NULL, play_reply,
				  NULL, NULL) == FALSE) {
		pr_info("Failed to play\n");
		return -1;
	}

	pr_info("Attempting to play\n");
	return 0;
}

void pause_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		pr_info("Failed to pause: %s\n", __func__);
		dbus_error_free(&error);
		return;
	}

	pr_info("Pause successful\n");
}

int pause_avrcp(void)
{
	if (!check_default_player())
		return -1;

	if (g_dbus_proxy_method_call(default_player, "Pause", NULL,
					pause_reply, NULL, NULL) == FALSE) {
		pr_info("Failed to pause\n");
		return -1;
	}
	pr_info("Attempting to pause\n");
	return 0;
}

void volumedown_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		pr_info("Failed to volume down\n");
		dbus_error_free(&error);
		return;
	}

	pr_info("volumedown successful\n");
}

void volumedown_avrcp(void)
{
	if (!check_default_player())
				return;
	if (g_dbus_proxy_method_call(default_player, "VolumeDown", NULL, volumedown_reply,
							NULL, NULL) == FALSE) {
		pr_info("Failed to volumeup\n");
		return;
	}
	pr_info("Attempting to volumeup\n");
}

void volumeup_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		pr_info("Failed to volumeup\n");
		dbus_error_free(&error);
		return;
	}

	pr_info("volumeup successful\n");
}


void volumeup_avrcp()
{
	if (!check_default_player())
				return;

	if (g_dbus_proxy_method_call(default_player, "VolumeUp", NULL, volumeup_reply,
							NULL, NULL) == FALSE) {
		pr_info("Failed to volumeup\n");
		return;
	}
	pr_info("Attempting to volumeup\n");
}

void stop_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		dbus_error_free(&error);
		return;
	}

	pr_info("Stop successful\n");
}

int stop_avrcp()
{
	if (!check_default_player())
			return -1;

	if (g_dbus_proxy_method_call(default_player, "Stop", NULL, stop_reply,
							NULL, NULL) == FALSE) {
		pr_info("Failed to stop\n");
		return -1;
	}

	pr_info("Attempting to stop\n");

	return 0;
}

void next_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		pr_info("Failed to jump to next\n");
		dbus_error_free(&error);
		return;
	}

	pr_info("Next successful\n");
}

int next_avrcp(void)
{
	if (!check_default_player())
		return -1;

	if (g_dbus_proxy_method_call(default_player, "Next", NULL, next_reply,
							NULL, NULL) == FALSE) {
		pr_info("Failed to jump to next\n");
		return -1;
	}

	pr_info("Attempting to jump to next\n");
	return 0;
}

void previous_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		pr_info("Failed to jump to previous\n");
		dbus_error_free(&error);
		return;
	}

	pr_info("Previous successful\n");
}

int previous_avrcp(void)
{

	if (!check_default_player())
		return -1;
	if (g_dbus_proxy_method_call(default_player, "Previous", NULL,
					previous_reply, NULL, NULL) == FALSE) {
		pr_info("Failed to jump to previous\n");
		return -1;
	}
	pr_info("Attempting to jump to previous\n");

	return 0;
}

void fast_forward_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		pr_info("Failed to fast forward\n");
		dbus_error_free(&error);
		return;
	}

	pr_info("FastForward successful\n");
}

void fast_forward_avrcp(void)
{
	if (!check_default_player())
			return;
	if (g_dbus_proxy_method_call(default_player, "FastForward", NULL,
				fast_forward_reply, NULL, NULL) == FALSE) {
		pr_info("Failed to jump to previous\n");
		return;
	}
	pr_info("Fast forward playback\n");
}

void rewind_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		pr_info("Failed to rewind\n");
		dbus_error_free(&error);
		return;
	}

	pr_info("Rewind successful\n");
}

void rewind_avrcp(void)
{
	if (!check_default_player())
			return;
	if (g_dbus_proxy_method_call(default_player, "Rewind", NULL,
					rewind_reply, NULL, NULL) == FALSE) {
		pr_info("Failed to rewind\n");
		return;
	}
	pr_info("Rewind playback\n");
}

int getstatus_avrcp(void)
{
	GDBusProxy *proxy;
	DBusMessageIter iter;
	const char *valstr;

	if (check_default_player() == FALSE)
		return AVRCP_PLAY_STATUS_ERROR; //default player no find

	proxy = default_player;
	if (g_dbus_proxy_get_property(proxy, "Status", &iter) == FALSE)
			return AVRCP_PLAY_STATUS_ERROR; //unkonw status

	dbus_message_iter_get_basic(&iter, &valstr);
	//pr_info("----getstatus_avrcp,rtl wifi,return %s--\n",valstr);

	if (!strcasecmp(valstr, "stopped"))
		return AVRCP_PLAY_STATUS_STOPPED;
	else if (!strcasecmp(valstr, "playing"))
		return AVRCP_PLAY_STATUS_PLAYING;
	else if (!strcasecmp(valstr, "paused"))
		return AVRCP_PLAY_STATUS_PAUSED;
	else if (!strcasecmp(valstr, "forward-seek"))
		return AVRCP_PLAY_STATUS_FWD_SEEK;
	else if (!strcasecmp(valstr, "reverse-seek"))
		return AVRCP_PLAY_STATUS_REV_SEEK;
	else if (!strcasecmp(valstr, "error"))
		return AVRCP_PLAY_STATUS_ERROR;

	return AVRCP_PLAY_STATUS_ERROR;
}

void a2dp_sink_register_track_cb(RK_BT_AVRCP_TRACK_CHANGE_CB cb)
{
	g_avrcp_cb.avrcp_track_cb = cb;
}

void a2dp_sink_register_position_cb(RK_BT_AVRCP_PLAY_POSITION_CB cb)
{
	g_avrcp_cb.avrcp_position_cb = cb;
}

void a2dp_sink_clear_cb()
{
	g_avrcp_cb.avrcp_track_cb = NULL;
	g_avrcp_cb.avrcp_position_cb = NULL;
}

int a2dp_sink_status(RK_BT_STATE *pState)
{
	int avrcp_status;

	if (!pState)
		return -1;

	avrcp_status = getstatus_avrcp();
	switch (avrcp_status) {
	case AVRCP_PLAY_STATUS_STOPPED:
		*pState = RK_BT_STATE_SINK_STOP;
		break;
	case AVRCP_PLAY_STATUS_REV_SEEK:
	case AVRCP_PLAY_STATUS_FWD_SEEK:
	case AVRCP_PLAY_STATUS_PLAYING:
		*pState = RK_BT_STATE_SINK_PLAY;
		break;
	case AVRCP_PLAY_STATUS_PAUSED:
		*pState = RK_BT_STATE_SINK_PAUSE;
		break;
	default:
		//if (g_btsrc_connect_status == RK_BT_SINK_STATE_CONNECT)
		//	*pState = RK_BT_SINK_STATE_CONNECT;
		//else
		//	*pState = RK_BT_SINK_STATE_IDLE;
		break;
	}

	return 0;
}
