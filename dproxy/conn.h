#ifndef conn_h
#define conn_h

#include <stdio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CFNetwork/CFNetwork.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

struct conn_t {
    bool is_client;

    CFSocketNativeHandle fd;

    CFReadStreamRef readStream;
    UInt8 read_buf[BUFSIZ];

    CFWriteStreamRef writeStream;

    // Misc stream related
    bool can_accept_bytes;

    // Remote server
    struct sockaddr_in remote_addr;
    char remote_addr_str[INET6_ADDRSTRLEN];

    CFStreamClientContext stream_context;

    CFHostClientContext dns_context;

    // Current message
    SInt32 port_nbr;
    CFHTTPMessageRef incoming_message;
    CFHTTPMessageRef outgoing_message;
};

void conn_deinit(struct conn_t*);
void conn_init(struct conn_t*, CFSocketNativeHandle);

void conn_start_resolve(struct conn_t *, CFHostRef);

#endif /* conn_h */
