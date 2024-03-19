#ifndef __BT_LOG__
#define __BT_LOG__
#include <syslog.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_TAG "BLUEZ_LOG"
#define SYSLOG_DEBUG

#ifdef SYSLOG_DEBUG
#define pr_debug(fmt, ...)		syslog(LOG_DEBUG, fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...)		syslog(LOG_INFO, fmt, ##__VA_ARGS__)
#define pr_warning(fmt, ...)	syslog(LOG_WARNING, fmt, ##__VA_ARGS__)
#define pr_err(fmt, ...)		syslog(LOG_ERR, fmt, ##__VA_ARGS__)
#define pr_display(fmt, ...)	{syslog(LOG_INFO, fmt, ##__VA_ARGS__); printf(fmt, ##__VA_ARGS__);}
#else
extern void pr_no();
#define pr_debug	pr_no
#define pr_info pr_no
#define pr_warning pr_no
#define pr_err pr_no
#define pr_display pr_no
#endif

#ifdef __cplusplus
}
#endif

#endif
