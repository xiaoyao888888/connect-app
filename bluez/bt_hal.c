#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <string.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>

#include <Rk_wifi.h>
#include <RkBtBase.h>
#include <RkBle.h>
#include <RkBtSource.h>
#include <RkBleClient.h>
//#include <RkBtObex.h>
//#include <RkBtPan.h>

#include "avrcpctrl.h"
#include "bluez_ctrl.h"
#include "a2dp_source/a2dp_masterctrl.h"
#include "a2dp_source/shell.h"
#include "spp_server/spp_server.h"
#include "gatt_client.h"
#include "gatt_config.h"
#include "utility.h"
#include "slog.h"

#if 0
void rk_printf_system_time(char *tag)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);

	printf("---%s: time: %lld ms\n", tag, tv.tv_sec * 1000 + tv.tv_usec/1000 + tv.tv_usec%1000);
}
#endif

RK_BT_VENDOR_CALLBACK bt_vendor_cb;
RK_BT_AUDIO_SERVER_CALLBACK bt_audio_server_cb;

static char con_addr[18];
static char dis_addr[18];
static char pair_addr[18];
static char unpair_addr[18];
static char del_addr[18];

//ble uuid "00002a26-0000-1000-8000-00805f9b34fb"
static char gatt_client_uuid[36];

//First byte: length + data
char rk_gatt_data[1 + 512];

bool bt_is_open(void)
{
	if (g_rkbt_content == NULL)
		return false;

	if (g_rkbt_content->init == false)
		return false;

	if (get_ps_pid("bluetoothd"))
		return true;

	pr_display("bt has been opened but bluetoothd server exit.\n");

	return false;
}

__attribute__((visibility("default")))
bool rk_bt_is_open(void)
{
	return bt_is_open();
}

/* ble api */
//gboolean int bt_start_discovery(gpointer data)
__attribute__((visibility("default")))
int rk_ble_adv_start(void)
{
	pr_display("%s id: %d\n", __func__, rk_gettid());

	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return -1;
	}

	g_idle_add(ble_enable_adv, NULL);

	return 0;
}

__attribute__((visibility("default")))
int rk_ble_adv_stop(void)
{
	//ble_disable_adv();
	g_idle_add(ble_disable_adv, NULL);

	return 0;
}

RkBleConfig ble_cfg;
__attribute__((visibility("default")))
int rk_ble_send_notify(const char *uuid, char *data, int len)
{
	int ret = 0;

	if (data == NULL)
		return -1;

	//pr_display("%s data:%s, len: %d\n", __func__, data, len);
	ble_cfg.len = len > BT_ATT_MAX_VALUE_LEN ? BT_ATT_MAX_VALUE_LEN : len;
	memcpy(ble_cfg.data, data, ble_cfg.len);
	strcpy(ble_cfg.uuid, uuid);
	//pr_display("%s data:%s, len: %d\n", __func__, ble_cfg.data, ble_cfg.len);

	//ret = gatt_write_data(ble_cfg.uuid, ble_cfg.data, ble_cfg.len);
	g_idle_add(gatt_write_data, &ble_cfg);

	return ret;
}

__attribute__((visibility("default")))
int rk_ble_set_adv_interval(unsigned short adv_int_min, unsigned short adv_int_max)
{
	return ble_set_adv_interval(adv_int_min, adv_int_max);
}

/* bluetooth LE GATT Client api */
__attribute__((visibility("default")))
int rk_ble_client_get_service_info(char *address, RK_BLE_CLIENT_SERVICE_INFO *info)
{
	return gatt_client_get_service_info(address, info);
}

__attribute__((visibility("default")))
int rk_ble_client_read(const char *uuid)
{
	pr_info("%s: %s\n", __func__, uuid);

	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return -1;
	}

	if (uuid == NULL)
		return -1;

	strncpy(gatt_client_uuid, uuid, 36);
	g_idle_add(gatt_client_read, gatt_client_uuid);

	return 0;
}

__attribute__((visibility("default")))
int rk_ble_client_write(const char *uuid, char *data, int data_len)
{
	pr_info("%s: %s\n", __func__, uuid);

	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return -1;
	}

	if (uuid == NULL)
		return -1;

	strncpy(gatt_client_uuid, uuid, 36);
	rk_gatt_data[0] = data_len;
	memcpy(rk_gatt_data + 1, data, data_len);
	g_idle_add(gatt_client_write, gatt_client_uuid);

	return 0;
	//return gatt_client_write(uuid, data, data_len, 0);
}

__attribute__((visibility("default")))
bool rk_ble_client_is_notifying(const char *uuid)
{
	return gatt_client_is_notifying(uuid);
}

__attribute__((visibility("default")))
int rk_ble_client_notify(const char *uuid, bool enable)
{
	return gatt_client_notify(uuid, enable);
}

static int bt_hal_source_close(bool disconnect)
{
	return 0;
}

__attribute__((visibility("default")))
int rk_bt_disconnect_by_addr(char *address)
{
	pr_info("%s\n", __func__);

	if (address == NULL)
		return -1;

	if (!address || (strlen(address) != 17)) {
		g_idle_add(disconnect_current_devices, NULL);
		return 0;
	}

	strncpy(dis_addr, address, 18);
	g_idle_add(_bt_disconnect_by_address, dis_addr);

	return 0;
}

__attribute__((visibility("default")))
int rk_bt_remove(char *address)
{
	if (address == NULL)
		return -1;

	strncpy(del_addr, address, 18);
	g_idle_add(_bt_remove_by_address, del_addr);
	//return _bt_remove_by_address(address);

	return 0;
}

__attribute__((visibility("default")))
void rk_bt_adapter_info(char *data)
{

}

int rk_bt_sink_register_track_callback(RK_BT_AVRCP_TRACK_CHANGE_CB cb)
{
	a2dp_sink_register_track_cb(cb);
	return 0;
}

int rk_bt_sink_register_position_callback(RK_BT_AVRCP_PLAY_POSITION_CB cb)
{
	a2dp_sink_register_position_cb(cb);
	return 0;
}

int rk_bt_sink_get_state(RK_BT_STATE *pState)
{
	return a2dp_sink_status(pState);
}

__attribute__((visibility("default")))
int rk_bt_sink_media_control(char *cmd)
{
	char *str = (char *)cmd;

	if (str == NULL)
		return -1;

	pr_display("bt_avrcp_cmd: [%s]\n", str);

	if (!strcmp("play", str))
		play_avrcp();
	else if (!strcmp("pause", str))
		pause_avrcp();
	else if (!strcmp("stop", str))
		stop_avrcp();
	else if (!strcmp("previous", str))
		previous_avrcp();
	else if (!strcmp("next", str))
		next_avrcp();
	else
		pr_display("Not Support [%s] cmd\n", str);

	return 0;
}

static int _get_bluealsa_plugin_volume_ctrl_info(char *name, int *value)
{
	char buff[1024] = {0};
	char ctrl_name[128] = {0};
	int ctrl_value = 0;
	char *start = NULL;
	char *end = NULL;

	if (!name && !value)
		return -1;

	if (name) {
		exec_command("amixer -D bluealsa scontents", buff, sizeof(buff));
		start = strstr(buff, "Simple mixer control ");
		end = strstr(buff, "A2DP'");
		if (!start || (!strstr(start, "A2DP")))
			return -1;

		start += strlen("Simple mixer control '");
		end += strlen("A2DP");
		if ((end - start) < strlen(" - A2DP"))
			return -1;

		memcpy(ctrl_name, start, end-start);
		memcpy(name, ctrl_name, strlen(ctrl_name));
	}

	if (value) {
		start = strstr(buff, "Front Left: Capture ");
		if (!start)
			return -1;

		start += strlen("Front Left: Capture ");
		if ((*start < '0') || (*start > '9'))
			return -1;

		/* Max volume value:127, the length of volume value string must be <= 3 */
		ctrl_value += (*start - '0');
		start++;
		if ((*start >= '0') && (*start <= '9'))
			ctrl_value = 10 * ctrl_value + (*start - '0');
		start++;
		if ((*start >= '0') && (*start <= '9'))
			ctrl_value = 10 * ctrl_value + (*start - '0');

		*value = ctrl_value;
	}

	return 0;
}

static int _set_bluealsa_plugin_volume_ctrl_info(char *name, int value)
{
	char buff[1024] = {0};
	char cmd[256] = {0};
	char ctrl_name[128] = {0};
	int new_volume = 0;

	if (!name)
		return -1;

	sprintf(cmd, "amixer -D bluealsa sset \"%s\" %d", name, value);
	exec_command(cmd, buff, sizeof(buff));

	if (_get_bluealsa_plugin_volume_ctrl_info(ctrl_name, &new_volume) == -1)
		return -1;
	if (new_volume != value)
		return -1;

	return 0;
}

int rk_bt_sink_volume_up(void)
{
	char ctrl_name[128] = {0};
	int current_volume = 0;
	int ret = 0;

	ret = _get_bluealsa_plugin_volume_ctrl_info(ctrl_name, &current_volume);
	if (ret)
		return ret;

	ret = _set_bluealsa_plugin_volume_ctrl_info(ctrl_name, current_volume + 8);
	return ret;
}

int rk_bt_sink_volume_down(void)
{
	char ctrl_name[128] = {0};
	int current_volume = 0;
	int ret = 0;

	ret = _get_bluealsa_plugin_volume_ctrl_info(ctrl_name, &current_volume);
	if (ret)
		return ret;

	if (current_volume < 8)
		current_volume = 0;
	else
		current_volume -= 8;

	ret = _set_bluealsa_plugin_volume_ctrl_info(ctrl_name, current_volume);
	return ret;
}

__attribute__((visibility("default")))
int rk_bt_sink_set_volume(int volume)
{
	//char ctrl_name[128] = {0};
	int new_volume = 0;
	int ret = 0;

	if (volume < 0)
		new_volume = 0;
	else if (volume > 127)
		new_volume = 127;
	else
		new_volume = volume;

	transport_set_volume(new_volume);

	//ret = _get_bluealsa_plugin_volume_ctrl_info(ctrl_name, NULL);
	//if (ret)
	//	return ret;

	//ret = _set_bluealsa_plugin_volume_ctrl_info(ctrl_name, new_volume);
	return ret;
}

/*****************************************************************
 *            Rockchip bluetooth spp api                         *
 *****************************************************************/
__attribute__((visibility("default")))
int rk_bt_spp_open(char *data)
{
	int ret = 0;

	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		//return -1;
	}

	ret = bt_spp_server_open();
	return ret;
}

__attribute__((visibility("default")))
int rk_bt_spp_register_status_cb(RK_BT_SPP_STATUS_CALLBACK cb)
{
	bt_spp_register_status_callback(cb);
	return 0;
}

__attribute__((visibility("default")))
int rk_bt_spp_register_recv_cb(RK_BT_SPP_RECV_CALLBACK cb)
{
	bt_spp_register_recv_callback(cb);
	return 0;
}

__attribute__((visibility("default")))
int rk_bt_spp_close(void)
{
	bt_spp_server_close();
	return 0;
}

__attribute__((visibility("default")))
int rk_bt_spp_get_state(RK_BT_SPP_STATE *pState)
{
	if (pState)
		*pState = bt_spp_get_status();

	return 0;
}

__attribute__((visibility("default")))
int rk_bt_spp_write(char *data, int len)
{
	bt_spp_write(data, len);
	return 0;
}

__attribute__((visibility("default")))
int rk_bt_spp_listen()
{
	pr_info("bluez don't support %s\n", __func__);

	return 0;
}

__attribute__((visibility("default")))
int rk_bt_spp_connect(char *address)
{
	int ret = 0;
	pr_info("enter %s\n", __func__);

	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		//return -1;
	}

	ret = bt_spp_client_open(address);
	return ret;

	return 0;
}

__attribute__((visibility("default")))
int rk_bt_spp_disconnect(char *address)
{
	pr_info("bluez don't support %s\n", __func__);

	return 0;
}

__attribute__((visibility("default")))
char *rk_bt_version(void)
{
	return "V2.0.3 DEBUG";
}

//====================================================//
static GMainLoop *bt_main_loop = NULL;
static pthread_t main_loop_thread = 0;

static void *main_loop_init_thread(void *data)
{
	pr_info("%s: bt mainloop run with default context\n", __func__);
	pr_display("%s id: %d\n", __func__, rk_gettid());

	bt_main_loop = g_main_loop_new(NULL, FALSE);

	g_main_loop_run(bt_main_loop);

	g_main_loop_unref(bt_main_loop);
	bt_main_loop = NULL;

	pr_info("%s: bt mainloop exit\n", __func__);
	return NULL;
}

static int main_loop_init(void)
{
	if (main_loop_thread)
		return 0;

	if (pthread_create(&main_loop_thread, NULL, main_loop_init_thread, NULL)) {
		pr_err("%s: Create bt mainloop thread failed\n", __func__);
		return -1;
	}
	pthread_setname_np(main_loop_thread, "main_loop_thread");

	return 0;
}

static int main_loop_deinit(void)
{
	if (bt_main_loop) {
		pr_info("%s bt mainloop quit\n", __func__);
		g_main_loop_quit(bt_main_loop);
	}

	if (main_loop_thread) {
		if (pthread_join(main_loop_thread, NULL)) {
			pr_err("%s: bt mainloop exit failed!\n", __func__);
			return -1;
		} else {
			pr_info("%s: bt mainloop thread exit ok\n", __func__);
		}
		main_loop_thread = 0;
	}

	return 0;
}

__attribute__((visibility("default")))
void rk_bt_set_profile(uint8_t profile)
{
	if (profile == PROFILE_A2DP_SINK_HF)
		g_rkbt_content->profile &= ~PROFILE_A2DP_SOURCE_AG;

	if (profile == PROFILE_A2DP_SOURCE_AG)
		g_rkbt_content->profile &= ~PROFILE_A2DP_SINK_HF;
	
	g_rkbt_content->profile |= profile;
	if (bt_audio_server_cb)
		bt_audio_server_cb();
}

static pthread_t rk_bt_init_thread = 0;
void *_rk_bt_init(void *p)
{
	RkBtContent *p_bt_content = p;
	pr_display("%s id: %d\n", __func__, rk_gettid());

	int day, year;
	char month[4];
	const char *dateString = __DATE__;
	if (sscanf(dateString, "%s %d %d", month, &day, &year) == 3) {
		pr_display("librkwifibt.so version:[%d-%s-%d:%s]\n", year, month, day, __TIME__);
	}

	setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/var/run/dbus/system_bus_socket", 1);

	//refer
	g_rkbt_content = p_bt_content;

	if (bt_is_open())
		goto end;

	main_loop_init();

	if (bt_vendor_cb && !bt_vendor_cb(true))
		goto fail;

	if (bt_open(p_bt_content) < 0)
		goto fail;

	//if (bt_audio_server_cb && !bt_audio_server_cb())
	//	goto fail;

end:
	pr_debug("_rk_bt_init exit\n");
	g_rkbt_content->init = true;
	return NULL;

fail:
	pr_debug("_rk_bt_init fail\n");
	g_rkbt_content->init = false;
	return NULL;
}

__attribute__((visibility("default")))
int rk_bt_init(RkBtContent *p_bt_content)
{
	if (bt_is_open()) {
		pr_info("%s:  has been enable!!!\n", __func__);
		return 0;
	}

	if (rk_bt_init_thread)
		return 0;

	if (pthread_create(&rk_bt_init_thread, NULL, _rk_bt_init, (void *)p_bt_content)) {
		pr_err("%s: Create rk_bt_init_thread thread failed\n", __func__);
		return -1;
	}

	pthread_setname_np(rk_bt_init_thread, "rk_bt_init_thread");
	pthread_detach(rk_bt_init_thread);

	return 0;
}

/**
 */
static void *_rk_bt_deinit(void *p)
{
	rk_bt_init_thread = 0;

	if (!bt_is_open()) {
		pr_info("%s: bluetooth has been closed!\n", __func__);
		return NULL;
	}

	pr_info("enter %s, tid(%lu)\n", __func__, pthread_self());
	//bt_state_send(RK_BT_STATE_TURNING_OFF);

	//bt_hal_sink_close(false);
	//bt_hal_source_close(false);
	rk_bt_spp_close();
	bt_close();
	main_loop_deinit();

	if (bt_vendor_cb && !bt_vendor_cb(false))
		pr_info("bt_vendor_cb false err\n");
	//kill_task("obexd");
	//kill_task("bluealsa");
	//kill_task("bluealsa-aplay");
	//msleep(300);
	//exec_command_system("hciconfig hci0 down");
	//kill_task("bluetoothd");
	//kill_task("rtk_hciattach");
	//kill_task("brcm_patchram_plus1");

	g_rkbt_content->init = false;
	bt_state_send(NULL, RK_BT_STATE_INIT_OFF);
	bt_deregister_state_callback();
	pr_info("exit %s\n", __func__);

	return NULL;
}

static pthread_t rk_bt_deinit_thread = 0;
__attribute__((visibility("default")))
int rk_bt_deinit()
{
	if (pthread_create(&rk_bt_deinit_thread, NULL, _rk_bt_deinit, NULL)) {
		pr_err("Create rk_bt_init_thread thread failed\n");
		return -1;
	}

	pthread_setname_np(rk_bt_deinit_thread, "rk_bt_deinit_thread");
	pthread_detach(rk_bt_deinit_thread);

	return 0;
}

__attribute__((visibility("default")))
void rk_bt_register_state_callback(RK_BT_STATE_CALLBACK cb)
{
	bt_register_state_callback(cb);
}

__attribute__((visibility("default")))
void rk_bt_register_vendor_callback(RK_BT_VENDOR_CALLBACK cb)
{
	bt_vendor_cb = cb;
}

__attribute__((visibility("default")))
void rk_bt_register_audio_server_callback(RK_BT_AUDIO_SERVER_CALLBACK cb)
{
	bt_audio_server_cb = cb;
}

/*
 * / # hcitool con
 * Connections:
 *      > ACL 64:A2:F9:68:1E:7E handle 1 state 1 lm SLAVE AUTH ENCRYPT
 *      > LE 60:9C:59:31:7F:B9 handle 16 state 1 lm SLAVE
 */
__attribute__((visibility("default")))
int rk_bt_is_connected(void)
{
	return bt_is_connected();
}

__attribute__((visibility("default")))
int rk_bt_set_class(int value)
{
	char cmd[100] = {0};

	pr_info("#%s value:0x%x\n", __func__, value);
	sprintf(cmd, "hciconfig hci0 class 0x%x", value);
	exec_command_system(cmd);
	msleep(100);

	return 0;
}

int rk_bt_set_sleep_mode()
{
	pr_info("bluez don't support %s\n", __func__);
	return 0;
}

__attribute__((visibility("default")))
void rk_bt_set_discoverable(bool enable)
{
	cmd_discoverable(enable);
}

__attribute__((visibility("default")))
void rk_bt_set_pairable(bool enable)
{
	cmd_pairable(enable);
}

__attribute__((visibility("default")))
void rk_bt_set_power(bool enable)
{
	cmd_power(enable);
}

__attribute__((visibility("default")))
int rk_bt_start_discovery(RK_BT_SCAN_TYPE scan_type)
{
	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return -1;
	}

	if (g_rkbt_content->scanning == true){
		pr_display("%s: already scanning!!!\n", __func__);
		return 0;
	}

	g_idle_add(bt_start_discovery, &scan_type);

	return 0;
}

__attribute__((visibility("default")))
int rk_bt_cancel_discovery(void)
{
	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return -1;
	}

	if (g_rkbt_content->scanning == false) {
		pr_display("%s: already no discovery!!!\n", __func__);
		return 0;
	}

	g_idle_add(bt_cancel_discovery, NULL);

	return 0;
}

__attribute__((visibility("default")))
int rk_bt_connect_by_addr(char *addr)
{
	pr_display("%s: [%s]\n", __func__, addr);

	if (addr == NULL)
		return -1;

	if (g_rkbt_content->scanning)
		bt_cancel_discovery(NULL);

	//TODO: WAIT scanning to false
	int wait_cnt = 100;
	while (wait_cnt--) {
		usleep(100 * 1000);
		pr_info("%s: g_rkbt_content->scanning %d\n", __func__, g_rkbt_content->scanning);
		if (!g_rkbt_content->scanning)
			break;
	}

	if (g_rkbt_content->scanning) {
		bt_state_send(NULL, RK_BT_STATE_CONNECT_FAILED_SCANNING);
		return 0;
	}

	strncpy(con_addr, addr, 18);
	g_idle_add(connect_by_address, con_addr);

	return 0;
}

__attribute__((visibility("default")))
int rk_bt_pair_by_addr(char *addr)
{
	pr_info("%s: %s\n", __func__, addr);

	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return -1;
	}

	if (addr == NULL)
		return -1;

	strncpy(pair_addr, addr, 18);
	g_idle_add(pair_by_addr, pair_addr);

	return 0;
}

__attribute__((visibility("default")))
int rk_bt_unpair_by_addr(char *addr)
{
	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return -1;
	}

	if (addr == NULL)
		return -1;

	strncpy(unpair_addr, addr, 18);
	pr_info("%s: %p:%s\n", __func__, unpair_addr, addr);
	g_idle_add(unpair_by_addr, unpair_addr);

	return 0;
}

__attribute__((visibility("default")))
int rk_bt_set_visibility(const int visiable, const int connectable)
{
	if (visiable && connectable) {
		exec_command_system("hciconfig hci0 piscan");
		return 0;
	}

	exec_command_system("hciconfig hci0 noscan");
	usleep(20000);//20ms
	if (visiable)
		exec_command_system("hciconfig hci0 iscan");
	if (connectable)
		exec_command_system("hciconfig hci0 pscan");

	return 0;
}

RK_BT_DEV_PLATFORM_TYPE rk_bt_get_dev_platform(char *addr)
{
	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return -1;
	}

	return (RK_BT_DEV_PLATFORM_TYPE)get_dev_platform(addr);
}
