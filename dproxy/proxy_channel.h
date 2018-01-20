//
//  proxy_channel.h
//  dproxy
//
//  Created by David Everlöf on 2018-01-20.
//  Copyright © 2018 David Everlöf. All rights reserved.
//

#ifndef proxy_channel_h
#define proxy_channel_h

#include "conn.h"

struct proxy_channel_t {
    struct conn_t client;
    struct conn_t server;
};

#define PCHAN(X) (((X)->is_client) ? ((struct proxy_channel_t*) (X)) : ((struct proxy_channel_t*) ((X) - 1)))

struct proxy_channel_t* proxy_channel_create(CFSocketNativeHandle);
void proxy_channel_free(struct proxy_channel_t**);

void proxy_channel_signal_request_receievd(struct conn_t*);
void proxy_channel_signal_end(struct conn_t*);

#endif /* proxy_channel_h */
