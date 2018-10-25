#ifndef _WS_H_
#define _WS_H_

#include <pthread.h>

typedef struct
{
	void *buf;
	void *end;
	size_t stored;
	size_t total;

	/* When sending to libwebsocket, the buffer must be padded */
	int padded;
} packet_t;

typedef struct
{
	int (*recv_fn)(packet_t *, void *);
	void *recv_user;

	packet_t partial;

	struct lws *wsi;
	struct lws_context *ctx;

	int interrupted;
	int connected;

	pthread_mutex_t *send_lock;
	pthread_t worker;
} ws_t;

ws_t *ws_init();
int ws_start(ws_t *ws);
int ws_send_buf(ws_t *ws, char *buf, size_t len);
void ws_register_recv_cb(ws_t *ws, int (*fn)(packet_t *, void *), void *user);
int ws_send_pkt(ws_t *ws, packet_t *pkt);

#endif
