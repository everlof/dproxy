//
//  proxy_channel.c
//  dproxy
//
//  Created by David Everlöf on 2018-01-20.
//  Copyright © 2018 David Everlöf. All rights reserved.
//

#include "proxy_channel.h"
#include "log.h"

struct proxy_channel_t* proxy_channel_create(CFSocketNativeHandle client_native_handle) {
    struct proxy_channel_t *pchan = (struct proxy_channel_t *) calloc(1, sizeof(struct proxy_channel_t));

    pchan->client = client_create(client_native_handle);
    pchan->client->proxy = pchan;

    pchan->server = server_create();
    pchan->server->proxy = pchan;

    log_trace("%p pchan: alloc, client=%p, server=%p\n", pchan, pchan->client, pchan->server);
    return pchan;
}

void proxy_channel_free(struct proxy_channel_t** pchan) {
    log_debug("%p pchan: free\n", *pchan);
    free(*pchan);
    (*pchan) = NULL;
}

void proxy_signal_server_eof(struct server_conn_t* server) {
    log_trace("%p server signal eof\n", server);
}

void proxy_signal_client_eof(struct client_conn_t* client) {
    log_trace("%p client signal eof\n", client);
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

    CFHTTPMessageSetHeaderFieldValue(server->incoming_message, CFSTR("Server"), CFSTR("dproxy"));

    CFDataRef data = CFHTTPMessageCopySerializedMessage(server->incoming_message);
    CFIndex length = CFDataGetLength(data);
    UInt8 buff[length];
    CFDataGetBytes(data, CFRangeMake(0, length), buff);

    CFWriteStreamWrite(P_CHAN(server)->client->writeStream, buff, length);
    CFRelease(data);
}

void proxy_signal_client_req_recv(struct client_conn_t *client) {
    log_trace("%p client rect HTTP complete message\n", client);

    assert(client->incoming_message != NULL);
    struct proxy_channel_t *p_chan = P_CHAN(client);

    CFURLRef server_url = CFHTTPMessageCopyRequestURL(client->incoming_message);
    CFStringRef server_hostname = CFURLCopyHostName(server_url);
    CFStringRef scheme = CFURLCopyScheme(server_url);

    CFHostRef host = CFHostCreateWithName(kCFAllocatorDefault, server_hostname);
    SInt32 port_nbr = CFURLGetPortNumber(server_url);
    
    p_chan->server->host = host;
    p_chan->server->port_nbr = port_nbr;

    if (p_chan->server->port_nbr == -1) {
        p_chan->server->port_nbr = CFStringCompare(scheme, CFSTR("http"), kCFCompareCaseInsensitive) == kCFCompareEqualTo ? 80 : 433;
    }

    server_start_resolve(p_chan->server);

    CFRelease(scheme);
    CFRelease(server_hostname);
    CFRelease(server_url);
}
