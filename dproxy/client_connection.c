//
//  client_connection.c
//  dproxy
//
//  Created by David Everlöf on 2018-01-20.
//  Copyright © 2018 David Everlöf. All rights reserved.
//

#include <arpa/inet.h>
#include "client_connection.h"
#include "log.h"
#include "proxy_channel.h"
#include "utils.h"

static void client_write_stream_callback(CFWriteStreamRef stream, CFStreamEventType type, void *info);
static void client_read_stream_callback(CFReadStreamRef stream, CFStreamEventType type, void *info);

struct client_conn_t* client_create(CFSocketNativeHandle fd) {
    struct client_conn_t *client = (struct client_conn_t *) calloc(1, sizeof(struct client_conn_t));
    log_trace("%p client alloc, fd=%d\n", client, (int) fd);
    client->fd = fd;

    // TODO - LOOK INTO THIS
    memset(client->remote_addr_str, 0, sizeof(client->remote_addr_str));
    uint8_t name[SOCK_MAXADDRLEN];
    socklen_t namelen = sizeof(name);
    if (0 == getpeername(client->fd, (struct sockaddr *)name, &namelen)) {
        switch(((struct sockaddr *)name)->sa_family) {
            case AF_INET: {
                struct sockaddr_in *addr_in = (struct sockaddr_in *)name;
                inet_ntop(AF_INET, &(addr_in->sin_addr), client->remote_addr_str, INET_ADDRSTRLEN);
                break;
            }
            case AF_INET6: {
                struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)name;
                inet_ntop(AF_INET6, &(addr_in6->sin6_addr), client->remote_addr_str, INET6_ADDRSTRLEN);
                break;
            }
            default:
                break;
        }

        log_trace("%p conn: client ip connected => %s:%d\n", client, client->remote_addr_str, ((struct sockaddr_in *)name)->sin_port);
    }

    CFStreamCreatePairWithSocket(kCFAllocatorDefault, client->fd, &(client->readStream), &(client->writeStream));

    if (client->readStream && client->writeStream) {
        CFReadStreamSetProperty(client->readStream, kCFStreamPropertyShouldCloseNativeSocket, kCFBooleanTrue);
        CFWriteStreamSetProperty(client->writeStream, kCFStreamPropertyShouldCloseNativeSocket, kCFBooleanTrue);
    }

    client->stream_context.version = 0;
    client->stream_context.info = (void *)(client);
    client->stream_context.retain = nil;
    client->stream_context.release = nil;
    client->stream_context.copyDescription = nil;

    CFWriteStreamSetClient(client->writeStream,
                           kCFStreamEventOpenCompleted     |
                           kCFStreamEventErrorOccurred     |
                           kCFStreamEventEndEncountered    |
                           kCFStreamEventCanAcceptBytes,
                           &client_write_stream_callback,
                           &client->stream_context);

    CFWriteStreamScheduleWithRunLoop(client->writeStream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    CFWriteStreamOpen(client->writeStream);

    CFReadStreamSetClient(client->readStream,
                          kCFStreamEventOpenCompleted      |
                          kCFStreamEventErrorOccurred      |
                          kCFStreamEventEndEncountered     |
                          kCFStreamEventHasBytesAvailable,
                          &client_read_stream_callback,
                          &client->stream_context);

    CFReadStreamScheduleWithRunLoop(client->readStream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    CFReadStreamOpen(client->readStream);

    return client;
}

void client_free(struct client_conn_t **client) {
    CFReadStreamSetClient((*client)->readStream, kCFStreamEventNone, NULL, NULL);
    CFReadStreamClose((*client)->readStream);

    CFWriteStreamSetClient((*client)->writeStream, kCFStreamEventNone, NULL, NULL);
    CFWriteStreamClose((*client)->writeStream);

    log_trace("%p client free\n", client);
    free((*client));
    (*client) = NULL;
}

static void client_read_stream_callback(CFReadStreamRef stream, CFStreamEventType type, void *info) {
    struct client_conn_t *client = (struct client_conn_t *) info;
    log_trace("%p client: read-event: %s on fd=%d\n", client, CFStreamEventTypeString(type), client->fd);

    switch (type) {
        case kCFStreamEventHasBytesAvailable:
            if (client->incoming_message == nil) {
                client->incoming_message = CFHTTPMessageCreateEmpty(kCFAllocatorDefault, true);
            }

            memset(client->read_buf, 0, sizeof(client->read_buf));
            CFIndex nbr_read = CFReadStreamRead(stream, client->read_buf, sizeof(client->read_buf));

            if (!CFHTTPMessageAppendBytes(client->incoming_message, client->read_buf, nbr_read)) {
                log_error("couldn't append bytes...\n");
            }

            if (CFHTTPMessageIsHeaderComplete(client->incoming_message)) {
                proxy_signal_client_req_recv(client);
            }

            break;
        case kCFStreamEventErrorOccurred:
            log_debug("kCFStreamEventErrorOccurred\n");
            proxy_signal_client_error(client, CFReadStreamGetError(stream));
            break;
        case kCFStreamEventEndEncountered:
            proxy_signal_client_eof(client);
            break;
        case kCFStreamEventOpenCompleted:
            break;
    }
}

static void client_write_stream_callback(CFWriteStreamRef stream, CFStreamEventType type, void *info) {
    struct client_conn_t *client = (struct client_conn_t *) info;
    log_trace("%p client: write-event: %s on fd=%d\n", client, CFStreamEventTypeString(type), client->fd);

    switch (type) {
        case kCFStreamEventCanAcceptBytes:
            client->can_accept_bytes = true;
            break;
        case kCFStreamEventErrorOccurred:
            log_debug("kCFStreamEventErrorOccurred\n");
            proxy_signal_client_error(client, CFWriteStreamGetError(stream));
            break;
        case kCFStreamEventEndEncountered:
            proxy_signal_client_eof(client);
            break;
        case kCFStreamEventOpenCompleted:
            break;
    }
}
