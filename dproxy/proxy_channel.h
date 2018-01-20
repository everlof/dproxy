//
//  proxy_channel.h
//  dproxy
//
//  Created by David Everlöf on 2018-01-20.
//  Copyright © 2018 David Everlöf. All rights reserved.
//

#ifndef proxy_channel_h
#define proxy_channel_h

#include "client_connection.h"
#include "server_connection.h"

struct proxy_channel_t {
    struct client_conn_t *client;
    struct server_conn_t *server;
};

#define P_CHAN(X) ((struct proxy_channel_t*) ((X)->proxy))

struct proxy_channel_t* proxy_channel_create(CFSocketNativeHandle);
void proxy_channel_free(struct proxy_channel_t**);

void proxy_signal_server_req_recv(struct server_conn_t *);
void proxy_signal_client_req_recv(struct client_conn_t *);

void proxy_signal_server_eof(struct server_conn_t*);
void proxy_signal_client_eof(struct client_conn_t*);

void proxy_signal_server_error(struct server_conn_t*, CFStreamError error);
void proxy_signal_client_error(struct client_conn_t*, CFStreamError error);

#endif /* proxy_channel_h */
