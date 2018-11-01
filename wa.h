#ifndef _WA_H_
#define _WA_H_

#include <openssl/evp.h>
#include <pthread.h>
#include <uthash.h>
#include "ws.h"
#include "msg.h"
#include "dispatcher.h"
#include "crypto.h"

#define MAX_QUEUE 10

#define WA_STATE_LOGGING	0
#define WA_STATE_LOGGED_IN	1

typedef struct
{
	/* Session */
	char *client_id;
	char *client_token;
	char *server_token;
	char *browser_token;
	char *secret;
	char *ref;
	EVP_PKEY *keypair;
	EVP_PKEY *peer_key;
	char *pubkey;

	/* Internals */
	int run;
	int state;
	dispatcher_t *d;
	ws_t *ws;
	crypto_t *c;
} wa_t;

wa_t *wa_init();
int wa_login(wa_t *w);
void wa_free(wa_t *w);
void wa_loop(wa_t *w);

msg_t *
wa_request(wa_t *wa, msg_t *msg);

#endif
