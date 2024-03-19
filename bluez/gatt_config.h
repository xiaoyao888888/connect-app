#ifndef __BT_GATT_CONFIG_H__
#define __BT_GATT_CONFIG_H__

#include <RkBtBase.h>
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	char uuid[38];
	char data[BT_ATT_MAX_VALUE_LEN];
	int len;
} RkBleConfig;

int gatt_init(RkBtContent *bt_content);
gboolean ble_enable_adv(gpointer data);
gboolean ble_disable_adv(gpointer data);
gboolean gatt_write_data(gpointer data);
int gatt_setup(void);
void gatt_cleanup(void);
void gatt_set_stopping(bool stopping);
int ble_set_address(char *address);
int ble_set_adv_interval(unsigned short adv_int_min, unsigned short adv_int_max);
int gatt_set_on_adv(void);

#ifdef __cplusplus
}
#endif

#endif /* __BT_GATT_CONFIG_H__ */

