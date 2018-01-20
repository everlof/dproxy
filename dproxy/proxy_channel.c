//
//  proxy_channel.c
//  dproxy
//
//  Created by David Everlöf on 2018-01-20.
//  Copyright © 2018 David Everlöf. All rights reserved.
//

#include "proxy_channel.h"
#include "log.h"
#include "conn.h"

struct proxy_channel_t* proxy_channel_create(CFSocketNativeHandle client_native_handle) {
    struct proxy_channel_t *pchan = (struct proxy_channel_t *) calloc(1, sizeof(struct proxy_channel_t));
    log_trace("%p pchan: alloc, client=%p, server=%p\n", pchan, &(pchan->client), &(pchan->server));

    pchan->client.is_client = true;
    pchan->server.is_client = false;
    conn_init(&(pchan->client), client_native_handle);

    return pchan;
}

void proxy_channel_free(struct proxy_channel_t** pchan) {
    log_debug("%p pchan: free\n", *pchan);
    free(*pchan);
    (*pchan) = NULL;
}

void proxy_channel_signal_end(struct conn_t* conn) {
    log_trace("%p conn: signaled eof => %p\n", conn);
}

void proxy_channel_signal_request_receievd(struct conn_t* conn) {
    // For the client we should start resolve the server
    if (conn->is_client) {
        assert(conn->incoming_message != NULL);
        struct proxy_channel_t *pchan = PCHAN(conn);

        CFURLRef server_url = CFHTTPMessageCopyRequestURL(conn->incoming_message);
        CFStringRef server_hostname = CFURLCopyHostName(server_url);
        CFStringRef scheme = CFURLCopyScheme(server_url);
        CFHostRef host = CFHostCreateWithName(kCFAllocatorDefault, server_hostname);

        // Pass of `host` for the connection to resolve it.
        pchan->server.port_nbr = CFURLGetPortNumber(server_url);
        if (pchan->server.port_nbr == -1) {
            pchan->server.port_nbr = CFStringCompare(scheme, CFSTR("http"), kCFCompareCaseInsensitive) == kCFCompareEqualTo ? 80 : 433;
        }

        conn_start_resolve(&pchan->server, host);

        CFRelease(scheme);
        CFRelease(server_hostname);
        CFRelease(server_url);
        // NOTE! host is released in `conn_domain_resolution_completed`
    }

    // For the server, we should tell the client that we have a full response received
    if (!conn->is_client) {
        CFHTTPMessageSetHeaderFieldValue(conn->incoming_message, CFSTR("Server"), CFSTR("dproxy"));

        CFDataRef data = CFHTTPMessageCopySerializedMessage(conn->incoming_message);
        CFIndex length = CFDataGetLength(data);
        UInt8 buff[length];
        CFDataGetBytes(data, CFRangeMake(0, length), buff);

        CFWriteStreamWrite(PCHAN(conn)->client.writeStream, buff, length);
        CFRelease(data);
    }
}
