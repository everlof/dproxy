//
//  proxy_channel.c
//  dproxy
//
//  Created by David Everlöf on 2018-01-20.
//  Copyright © 2018 David Everlöf. All rights reserved.
//

#include "proxy_channel.h"
#include "log.h"
#include "utils.h"

struct proxy_channel_t* proxy_channel_create(CFSocketNativeHandle client_native_handle) {
    struct proxy_channel_t *p_chan = (struct proxy_channel_t *) calloc(1, sizeof(struct proxy_channel_t));

    p_chan->client = client_create(client_native_handle);
    p_chan->client->proxy = p_chan;

    log_trace("%p pchan: alloc, client=%p, server=%p\n", p_chan, p_chan->client, p_chan->server);
    return p_chan;
}

void proxy_channel_free(struct proxy_channel_t** pchan) {
    log_debug("%p pchan: free\n", *pchan);
    free(*pchan);
    (*pchan) = NULL;
}

char* proxy_state_str(enum proxy_state_t state) {
    switch (state) {
        case AVAILABLE:
            return "AVAILABLE";
        case CLIENT_READING:
            return "CLIENT_READING";
        case SERVER_RESOLVING_HOSTNAME:
            return "SERVER_RESOLVING_HOSTNAME";
        case SERVER_SENDING:
            return "SERVER_SENDING";
        case SERVER_READING:
            return "SERVER_READING";
        case CLIENT_SENDING:
            return "CLIENT_SENDING";
        case COMPLETED:
            return "COMPLETED";
    }
}

void proxy_state(struct proxy_channel_t* pchan, enum proxy_state_t state) {
    log_info("%p pchan: state %s => %s\n", pchan, proxy_state_str(pchan->state), proxy_state_str(state));
    pchan->state = state;
}

void proxy_signal_server_eof(struct server_conn_t* server) {
    log_trace("%p server signal eof\n", server);
}

void proxy_signal_client_eof(struct client_conn_t* client) {
    log_trace("%p client signal eof\n", client);
}

void proxy_signal_server_resp_send(struct server_conn_t *server) {
    log_trace("%p server sent\n", server);
}

void proxy_signal_client_resp_send(struct client_conn_t *client) {
    log_trace("%p client sent\n", client);
}

void proxy_signal_server_error(struct server_conn_t* server, CFStreamError error) {
    log_trace("%p server error %p\n", server, error);
}

void proxy_signal_client_error(struct client_conn_t* client, CFStreamError error) {
    log_trace("%p client error %p\n", client, error);
    /*
     CFStreamError error = CFWriteStreamGetError(stream);
     if (error.domain == kCFStreamErrorDomainPOSIX) {
        log_warn("Error occured: %s\n", strerror(error.error));
     } else {
        log_error("UNHANDLED ERROR: domain: %d\n", error.domain);
     }
     */
}

void proxy_signal_server_req_recv(struct server_conn_t *server) {
    log_trace("%p server recv HTTP complete message\n", server);
    CFHTTPMessageSetHeaderFieldValue(server->incoming_message, CFSTR("X-Server"), CFSTR("dproxy"));
    client_write_message(server->proxy->client, server->incoming_message);
}

void proxy_signal_client_req_recv(struct client_conn_t *client) {
    assert(client->incoming_message != NULL);
    struct proxy_channel_t *p_chan = client->proxy;

    log_trace("%p client recv HTTP complete message\n", client);

    if (!p_chan->server) {
        p_chan->server = server_create();
        p_chan->server->proxy = p_chan;
    }

    server_process_request(p_chan->server, client->incoming_message);
}
