#ifndef AVRCPCTRL_H
#define AVRCPCTRL_H

#include "RkBtSink.h"

//play status
#define AVRCP_PLAY_STATUS_STOPPED	0x00 // 停止
#define AVRCP_PLAY_STATUS_PLAYING	0x01 //正在播放
#define AVRCP_PLAY_STATUS_PAUSED	0x02 //暂停播放
#define AVRCP_PLAY_STATUS_FWD_SEEK	0x03 //快进
#define AVRCP_PLAY_STATUS_REV_SEEK	0x04 //重播
#define AVRCP_PLAY_STATUS_ERROR		0xFF //错误状态

/**
* 播放
*/
int play_avrcp();
 /**
* 暂停播放
*/
int pause_avrcp();
/**
* 停止播放
*/
int stop_avrcp();
/**
* 下一首
*/
int next_avrcp();
/**
* 上一首
*/
int previous_avrcp();
/**
* 获取当前蓝牙音频状态
*/
int getstatus_avrcp();
void volumeup_avrcp();
void volumedown_avrcp();
bool check_default_player(void);
void a2dp_sink_register_track_cb(RK_BT_AVRCP_TRACK_CHANGE_CB cb);
void a2dp_sink_register_position_cb(RK_BT_AVRCP_PLAY_POSITION_CB cb);
int a2dp_sink_status(RK_BT_STATE *pState);
bool get_poschange_avrcp(void);
int transport_set_volume(int volume);

#endif
