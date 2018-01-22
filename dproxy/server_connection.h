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

struct proxy_channel_t;

struct server_conn_t {
    struct proxy_channel_t *proxy;
    
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
    CFDataRef        outgoing_message_data;
    CFIndex          outgoing_message_data_i;

    SInt32 port_nbr;
    CFHostRef host;
    CFStringRef server_hostname;
};

void server_free(struct server_conn_t** server);
struct server_conn_t* server_create(void);

void server_process_request(struct server_conn_t *server, CFHTTPMessageRef message);

void server_write_message(struct server_conn_t *server, CFHTTPMessageRef message);
void server_write_if_possible(struct server_conn_t *server);

#endif /* server_connection_h */
