#ifndef __BLUEZ_CTRL_P__
#define __BLUEZ_CTRL_P__

#include "avrcpctrl.h"
#include "gatt_config.h"
#include "a2dp_source/a2dp_masterctrl.h"
#include "a2dp_source/shell.h"
#include "RkBtSource.h"
#include "RkBtSink.h"

#define HOSTNAME_MAX_LEN	250	/* 255 - 3 (FQDN) - 2 (DNS enc) */

bool bt_is_open();
bool ble_is_open();
bool ble_client_is_open();
bool bt_source_is_open(void);
bool bt_sink_is_open(void);
bool bt_hfp_is_open(void);
int bt_interface(enum BtControl type, void *data);
void bt_close_ble(bool disconnect);
int bt_close_sink(bool disconnect);
int bt_close_source(bool disconnect);
int bt_control_cmd_send(enum BtControl bt_ctrl_cmd);
int rk_bt_control(enum BtControl cmd, void *data, int len);

//#define msleep(x) usleep(x * 1000)

//new
int bt_avrcp_cmd(enum BtControl bt_ctrl_cmd);
int _bt_close_server(void);
int _bt_open_server(void);

#endif /* __BLUEZ_CTRL_P__ */
