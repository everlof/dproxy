//
//  client_connection.h
//  dproxy
//
//  Created by David Everlöf on 2018-01-20.
//  Copyright © 2018 David Everlöf. All rights reserved.
//

#ifndef client_connection_h
#define client_connection_h

#include <CFNetwork/CFNetwork.h>
#include <netinet/in.h>

struct client_conn_t {
    void *proxy;

    CFSocketNativeHandle fd;

    UInt8 read_buf[BUFSIZ];
    CFReadStreamRef readStream;
    CFWriteStreamRef writeStream;

    bool can_accept_bytes;

    // Remote server
    struct sockaddr_in remote_addr;
    char remote_addr_str[INET6_ADDRSTRLEN];

    CFStreamClientContext stream_context;

    CFHTTPMessageRef incoming_message;
    CFHTTPMessageRef outgoing_message;
};

struct client_conn_t* client_create(CFSocketNativeHandle fd);
void client_free(struct client_conn_t **client);

#endif /* client_connection_h */
