#ifndef __BT_GATT_CLITEN_H__
#define __BT_GATT_CLITEN_H__

#include "gdbus/gdbus.h"

#include <RkBtBase.h>
#include <RkBleClient.h>

#ifdef __cplusplus
extern "C" {
#endif

extern struct adapter *default_ctrl;
//extern GDBusProxy *ble_dev;
extern GDBusProxy *default_attr;

void gatt_client_state_send(RK_BLE_CLIENT_STATE state);
void gatt_client_recv_data_send(GDBusProxy *proxy, DBusMessageIter *iter);
RK_BLE_CLIENT_STATE gatt_client_get_state();
void gatt_client_open();
void gatt_client_close();
int gatt_client_get_service_info(char *address, RK_BLE_CLIENT_SERVICE_INFO *info);
gboolean gatt_client_read(gpointer data);
gboolean gatt_client_write(gpointer arg);
bool gatt_client_is_notifying(const char *uuid);
int gatt_client_notify(const char *uuid, bool enable);
int gatt_client_get_eir_data(char *address, char *eir_data, int eir_len);

#ifdef __cplusplus
}
#endif

#endif /* __BT_GATT_CLITEN_H__ */
