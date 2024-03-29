/*
 * (C) Copyright 2008-2018 Fuzhou Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <termios.h>
#include <poll.h>
#include <pthread.h>
#include <sys/prctl.h>

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/rfcomm.h>

#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>

#include "utility.h"
#include "slog.h"
#include "spp_server.h"

#define SPP_SERVER_CHANNEL 1

typedef struct {
	int client_fd;
	int server_fd;
	pthread_t tid, cid;
	int server_channel;
	bool server_running;
	RK_BT_SPP_STATE server_state;
	RK_BT_SPP_STATUS_CALLBACK state_cb;
	RK_BT_SPP_RECV_CALLBACK recv_cb;
} spp_handler_t;

static spp_handler_t g_spp_handler = {
	-1, -1, 0, 0, 1, false, RK_BT_SPP_STATE_IDLE, NULL, NULL,
};

static bdaddr_t bdaddr;
static uint8_t rfcomm_get_channel(const char *svr, uint16_t uuid)
{
	sdp_session_t *sdp;
	sdp_list_t *srch, *attrs, *rsp, *protos;
	uuid_t svclass;
	uint16_t attr;
	bdaddr_t dst;
	uint8_t channel = 0;
	int err;

	str2ba(svr, &dst);

	sdp = sdp_connect(&bdaddr, &dst, SDP_RETRY_IF_BUSY);
	if (!sdp)
		return 0;

	sdp_uuid16_create(&svclass, uuid);
	srch = sdp_list_append(NULL, &svclass);

	attr = SDP_ATTR_PROTO_DESC_LIST;
	attrs = sdp_list_append(NULL, &attr);

	err = sdp_service_search_attr_req(sdp, srch,
					SDP_ATTR_REQ_INDIVIDUAL, attrs, &rsp);
	if (err)
		goto done;

	for (; rsp; rsp = rsp->next) {
		sdp_record_t *rec = (sdp_record_t *) rsp->data;

		if (!sdp_get_access_protos(rec, &protos)) {
			channel = sdp_get_proto_port(protos, RFCOMM_UUID);
			if (channel > 0)
				break;
		}
	}

	sdp_list_free(protos, NULL);

done:
	sdp_list_free(srch, NULL);
	sdp_list_free(attrs, NULL);
	sdp_close(sdp);

	return channel;
}


static void spp_state_send(RK_BT_SPP_STATE state)
{
	if (g_spp_handler.state_cb)
		g_spp_handler.state_cb(state);
}

static void close_cilent_fd()
{
	if (g_spp_handler.client_fd >= 0) {
		shutdown(g_spp_handler.client_fd, SHUT_RDWR);
		g_spp_handler.client_fd = -1;
	}
}

static void close_server_fd()
{
	if (g_spp_handler.server_fd >= 0) {
		shutdown(g_spp_handler.server_fd, SHUT_RDWR);
		g_spp_handler.server_fd = -1;
	}
}

static void *spp_server_thread(void *arg)
{
	struct sockaddr_rc loc_addr = {0}, rem_addr = {0};
	char buf[4096];
	char rem_addr_str[20];
	int bytes_read = 0;
	socklen_t opt = sizeof(rem_addr);
	int parame = 1;
	fd_set rfds;
	struct timeval tv;

	g_spp_handler.server_fd = socket(PF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
	if(g_spp_handler.server_fd < 0) {
		pr_err("[BT SPP] create socket error\n");
		goto OUT;
	}

	/* Set address reuse */
	setsockopt(g_spp_handler.server_fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&parame, sizeof(parame));

	loc_addr.rc_family = AF_BLUETOOTH;
	loc_addr.rc_bdaddr = *BDADDR_ANY;
	loc_addr.rc_channel = g_spp_handler.server_channel;
	if(bind(g_spp_handler.server_fd, (struct sockaddr *)&loc_addr, sizeof(loc_addr)) < 0) {
		pr_err("[BT SPP] bind socket error\n");
		goto OUT;
	}

	if(listen(g_spp_handler.server_fd, 1) < 0) {
		pr_err("[BT SPP] listen error\n");
		goto OUT;
	}

	g_spp_handler.server_running = true;
	while (g_spp_handler.server_running) {
		FD_ZERO(&rfds);
		FD_SET(g_spp_handler.server_fd, &rfds);

		tv.tv_sec = 0;
		tv.tv_usec = 100000;/* 100ms */

		if (select(g_spp_handler.server_fd + 1, &rfds, NULL, NULL, &tv) < 0) {
			pr_info("[BT SPP] server socket failed!\n");
			goto OUT;
		}

		if (FD_ISSET(g_spp_handler.server_fd, &rfds) == 0)
			continue;

		g_spp_handler.client_fd = accept(g_spp_handler.server_fd, (struct sockaddr *)&rem_addr, &opt);
		if(g_spp_handler.client_fd < 0) {
			pr_err("[BT SPP] accept error\n");
			goto OUT;
		}

		/* Get remote device addr */
		memset(rem_addr_str, 0, sizeof(rem_addr_str));
		ba2str(&rem_addr.rc_bdaddr, rem_addr_str);
		pr_info("[BT SPP] accepted connection from %s \n", rem_addr_str);

		g_spp_handler.server_state = RK_BT_SPP_STATE_CONNECT;
		spp_state_send(RK_BT_SPP_STATE_CONNECT);

		while(1) {
			memset(buf, 0, sizeof(buf));
			bytes_read = read(g_spp_handler.client_fd, buf, sizeof(buf));
			if (bytes_read <= 0) {
				close_cilent_fd();
				g_spp_handler.server_state = RK_BT_SPP_STATE_DISCONNECT;
				spp_state_send(RK_BT_SPP_STATE_DISCONNECT);
				break;
			}

			if (g_spp_handler.recv_cb)
				g_spp_handler.recv_cb(buf, bytes_read);
		}
	}

OUT:
	close_cilent_fd();
	close_server_fd();
	g_spp_handler.server_state = RK_BT_SPP_STATE_IDLE;

	pr_info("%s: Exit spp server thread!\n", __func__);
	return NULL;
}

int _rk_bt_spp_get_client_fd(void)
{
	return g_spp_handler.client_fd;
}

static void *spp_server_thread_s(void *arg)
{
	struct sockaddr_rc loc_addr = {0}, rem_addr = {0};
	char rem_addr_str[20];
	socklen_t opt = sizeof(rem_addr);
	int parame = 1;
	fd_set rfds;
	struct timeval tv;

	tv.tv_sec = 0;
	tv.tv_usec = 100000;/* 100ms */

	g_spp_handler.server_fd = socket(PF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
	if(g_spp_handler.server_fd < 0) {
		pr_err("[BT SPP] create socket error\n");
		goto OUT;
	}

	/* Set address reuse */
	setsockopt(g_spp_handler.server_fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&parame, sizeof(parame));

	loc_addr.rc_family = AF_BLUETOOTH;
	loc_addr.rc_bdaddr = *BDADDR_ANY;
	loc_addr.rc_channel = g_spp_handler.server_channel;
	if(bind(g_spp_handler.server_fd, (struct sockaddr *)&loc_addr, sizeof(loc_addr)) < 0) {
		pr_err("[BT SPP] bind socket error\n");
		goto OUT;
	}

	if(listen(g_spp_handler.server_fd, 1) < 0) {
		pr_err("[BT SPP] listen error\n");
		goto OUT;
	}

	g_spp_handler.server_running = true;
	while (g_spp_handler.server_running) {
		FD_ZERO(&rfds);
		FD_SET(g_spp_handler.server_fd, &rfds);

		if (select(g_spp_handler.server_fd + 1, &rfds, NULL, NULL, &tv) < 0) {
			pr_info("[BT SPP] server socket failed!\n");
			goto OUT;
		}

		if (FD_ISSET(g_spp_handler.server_fd, &rfds) == 0)
			continue;

		g_spp_handler.client_fd = accept(g_spp_handler.server_fd, (struct sockaddr *)&rem_addr, &opt);
		if (g_spp_handler.client_fd < 0) {
			pr_err("[BT SPP] accept error\n");
			goto OUT;
		}

		/* Get remote device addr */
		memset(rem_addr_str, 0, sizeof(rem_addr_str));
		ba2str(&rem_addr.rc_bdaddr, rem_addr_str);
		pr_info("[BT SPP] accepted connection from %s \n", rem_addr_str);

		g_spp_handler.server_state = RK_BT_SPP_STATE_CONNECT;
		spp_state_send(RK_BT_SPP_STATE_CONNECT);

		return NULL;
	}

OUT:
	close_cilent_fd();
	close_server_fd();
	g_spp_handler.server_state = RK_BT_SPP_STATE_IDLE;

	pr_info("%s: Exit spp server thread!\n", __func__);
	return NULL;
}

char remote_addr[64];
void *spp_client_thread(void *dst_addr)
{
	struct pollfd rfds[2];
	int res = 0;
	struct sockaddr_rc rem_addr = {0};
	int parame = 1;
	//struct timeval tv;
	int ret;
	bdaddr_t dst_t, src_t;
	int channel_dst = rfcomm_get_channel(remote_addr, SERIAL_PORT_SVCLASS_ID);
	int hci_id = hci_devid("hci0");
	int dd = hci_open_dev(hci_id);
	pr_info("enter %s\n", __func__);

	if (hci_read_bd_addr(dd, &src_t, 1000) < 0) {
		fprintf(stderr, "Can't read address for hci%d: %s (%d)\n",
					dd, strerror(errno), errno);
		hci_close_dev(dd);
		exit(1);
	}

	str2ba(remote_addr, &dst_t);

	//tv.tv_sec = 0;
	//tv.tv_usec = 100000;/* 100ms */

	g_spp_handler.client_fd = socket(PF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
	if(g_spp_handler.client_fd < 0) {
		pr_err("[BT SPP] create socket error\n");
		goto OUT;
	}

	/* Set address reuse */
	setsockopt(g_spp_handler.client_fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&parame, sizeof(parame));

	memset(&rem_addr, 0, sizeof(rem_addr));
	rem_addr.rc_family = AF_BLUETOOTH;
	bacpy(&rem_addr.rc_bdaddr, &src_t);
	rem_addr.rc_channel = channel_dst;

	if (bind(g_spp_handler.client_fd, (struct sockaddr *) &rem_addr, sizeof(rem_addr)) < 0) {
		pr_err("rfcomm_bind %s", strerror(errno));
		return NULL;
	}
	pr_info("[BT SPP] bind ok!\n");

	memset(&rem_addr, 0, sizeof(rem_addr));
	rem_addr.rc_family = AF_BLUETOOTH;
	bacpy(&rem_addr.rc_bdaddr, &dst_t);
	rem_addr.rc_channel = channel_dst;
	pr_info("[BT SPP] client connect device: %s!\n", remote_addr);
	ret = connect(g_spp_handler.client_fd, (struct sockaddr *) &rem_addr, sizeof(rem_addr));
	pr_info("[BT SPP] clients connect ret: %d, error: %s!\n", ret, strerror(errno));

	if (ret < 0) {
		spp_state_send(RK_BT_SPP_STATE_CONNECT_FAILED);
		goto OUT;
	}

	g_spp_handler.server_state = RK_BT_SPP_STATE_CONNECT;
	spp_state_send(RK_BT_SPP_STATE_CONNECT);
	pr_info("[BT SPP] spp_state_send RK_BT_SPP_STATE_CONNECT!\n");
	g_spp_handler.cid = 0;

	pr_info("SPP cfd: %d, sfd: %d\n", g_spp_handler.client_fd, g_spp_handler.server_fd);

	memset(rfds, 0, 2 * sizeof(struct pollfd));
	rfds[0].fd = g_spp_handler.client_fd;
	rfds[0].events |= POLLERR;
	do {
		res = poll(rfds, 1, 8000);
		if (res < 0) {
			pr_info("Error poll = %d\n", res);
			goto DIS;
		} else if (res == 0) {
			pr_info("SPP ALIVE ...\n");
		}

		if ((g_spp_handler.client_fd < 0)) {
			pr_info("SPP ABORT !!!\n");
			goto DIS;
		}

		if (rfds[0].revents & POLLERR) {
			pr_info("client_fd dis !!!\n");
			goto DIS;
		}
	} while (res == 0);

DIS:
	spp_state_send(RK_BT_SPP_STATE_DISCONNECT);

OUT:
	close_cilent_fd();
	close_server_fd();
	g_spp_handler.server_state = RK_BT_SPP_STATE_IDLE;
	g_spp_handler.cid = 0;

	pr_info("%s: Exit spp server thread!\n", __func__);
	return NULL;
}

int bt_spp_client_open(char *dst_addr)
{
	memset(remote_addr, 0, 64);
	strcpy(remote_addr, dst_addr);
	pr_info("enter %s\n", __func__);

	if (!g_spp_handler.cid) {
		if (pthread_create(&g_spp_handler.cid, NULL, spp_client_thread, dst_addr)) {
			g_spp_handler.server_state = RK_BT_SPP_STATE_IDLE;
			pr_err("[BT SPP] create spp server thread failed!\n");
			return -1;
		}

		pthread_setname_np(g_spp_handler.cid, "spp_client_thread");
	}

	return 0;
}

#if 0
int bt_spp_server_open_s()
{
	char cmd[128];

	if (!g_spp_handler.tid) {
		memset(cmd, 0, 128);
		sprintf(cmd, "sdptool add --channel=%d SP", g_spp_handler.server_channel);
		exec_command_system(cmd);

		if(pthread_create(&g_spp_handler.tid, NULL, spp_server_thread_s, NULL)) {
			g_spp_handler.server_state = RK_BT_SPP_STATE_IDLE;
			pr_err("[BT SPP] create spp server thread failed!\n");
			return -1;
		}

		pthread_setname_np(g_spp_handler.tid, "spp_server_thread");
	}

	return 0;
}

void bt_spp_server_close_s()
{
	if(!g_spp_handler.server_running)
		return;

	pr_debug("[BT SPP] close start\n");
	g_spp_handler.server_running = false;
	close_cilent_fd();
	usleep(100000); //100ms
	close_server_fd();

	//exec_command_system("sdptool del SP");
	if (g_spp_handler.tid) {
		pthread_join(g_spp_handler.tid, NULL);
		g_spp_handler.tid = 0;
	}

	g_spp_handler.recv_cb = NULL;
	g_spp_handler.state_cb = NULL;
	g_spp_handler.server_state = RK_BT_SPP_STATE_IDLE;
	pr_debug("[BT SPP] close end\n");
}
#endif

int bt_spp_server_open()
{
	char cmd[128];

	if (!g_spp_handler.tid) {
		memset(cmd, 0, 128);
		sprintf(cmd, "sdptool add --channel=%d SP", g_spp_handler.server_channel);
		exec_command_system(cmd);

		if(pthread_create(&g_spp_handler.tid, NULL, spp_server_thread, NULL)) {
			g_spp_handler.server_state = RK_BT_SPP_STATE_IDLE;
			pr_err("[BT SPP] create spp server thread failed!\n");
			return -1;
		}

		pthread_setname_np(g_spp_handler.tid, "spp_server_thread");
	}

	return 0;
}

void bt_spp_register_recv_callback(RK_BT_SPP_RECV_CALLBACK cb)
{
	g_spp_handler.recv_cb = cb;
}

void bt_spp_register_status_callback(RK_BT_SPP_STATUS_CALLBACK cb)
{
	g_spp_handler.state_cb = cb;
}

void bt_spp_server_close()
{
	if(!g_spp_handler.server_running)
		return;

	pr_debug("[BT SPP] close start\n");
	g_spp_handler.server_running = false;
	close_cilent_fd();
	usleep(100000); //100ms
	close_server_fd();

	//exec_command_system("sdptool del SP");
	if (g_spp_handler.tid) {
		pthread_join(g_spp_handler.tid, NULL);
		g_spp_handler.tid = 0;
	}

	g_spp_handler.recv_cb = NULL;
	g_spp_handler.state_cb = NULL;
	g_spp_handler.server_state = RK_BT_SPP_STATE_IDLE;
	pr_debug("[BT SPP] close end\n");
}

int bt_spp_write(char *data, int len)
{
	int ret = 0;

	if (g_spp_handler.client_fd < 0) {
		pr_err("[BT SPP] write failed! ERROR:No connection is ready!\n");
		return -1;
	}

	ret = write(g_spp_handler.client_fd, data, len);
	if (ret <= 0) {
		pr_err("[BT SPP] write failed! ERROR:%s\n", strerror(errno));
		return ret;
	}

	return ret;
}

RK_BT_SPP_STATE bt_spp_get_status()
{
	return g_spp_handler.server_state;
}

int bt_spp_set_channel(int channel)
{
	if ((channel > 0) && (channel < 256))
		g_spp_handler.server_channel = channel;
	else {
		pr_warning("[BT SPP] channel is not valid, use default channel(1)\n");
		g_spp_handler.server_channel = SPP_SERVER_CHANNEL;
	}

	return 0;
}

int bt_spp_get_channel()
{
	return g_spp_handler.server_channel;
}
