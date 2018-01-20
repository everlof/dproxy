//
//  server_connection.h
//  dproxy
//
//  Created by David Everlöf on 2018-01-20.
//  Copyright © 2018 David Everlöf. All rights reserved.
//

#ifndef server_connection_h
#define server_connection_h

#include <CFNetwork/CFNetwork.h>
#include <netinet/in.h>

struct server_conn_t {
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
    CFHostClientContext dns_context;

    CFHTTPMessageRef incoming_message;
    CFHTTPMessageRef outgoing_message;

    SInt32 port_nbr;
    CFHostRef host;
};

void server_free(struct server_conn_t** server);
struct server_conn_t* server_create(void);

void setup_streams(struct server_conn_t *server, struct sockaddr * addr);
void server_start_resolve(struct server_conn_t *conn);

#endif /* server_connection_h */
