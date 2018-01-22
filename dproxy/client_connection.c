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

    CFStreamCreatePairWithSocket(kCFAllocatorDefault, client->fd, &(client->read_stream), &(client->write_stream));

    if (client->read_stream && client->write_stream) {
        CFReadStreamSetProperty(client->read_stream, kCFStreamPropertyShouldCloseNativeSocket, kCFBooleanTrue);
        CFWriteStreamSetProperty(client->write_stream, kCFStreamPropertyShouldCloseNativeSocket, kCFBooleanTrue);
    }

    client->stream_context.version = 0;
    client->stream_context.info = (void *)(client);
    client->stream_context.retain = nil;
    client->stream_context.release = nil;
    client->stream_context.copyDescription = nil;

    CFWriteStreamSetClient(client->write_stream,
                           kCFStreamEventOpenCompleted     |
                           kCFStreamEventErrorOccurred     |
                           kCFStreamEventEndEncountered    |
                           kCFStreamEventCanAcceptBytes,
                           &client_write_stream_callback,
                           &client->stream_context);

    CFWriteStreamScheduleWithRunLoop(client->write_stream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    CFWriteStreamOpen(client->write_stream);

    CFReadStreamSetClient(client->read_stream,
                          kCFStreamEventOpenCompleted      |
                          kCFStreamEventErrorOccurred      |
                          kCFStreamEventEndEncountered     |
                          kCFStreamEventHasBytesAvailable,
                          &client_read_stream_callback,
                          &client->stream_context);

    CFReadStreamScheduleWithRunLoop(client->read_stream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    CFReadStreamOpen(client->read_stream);

    return client;
}

void client_free(struct client_conn_t **client) {
    CFReadStreamSetClient((*client)->read_stream, kCFStreamEventNone, NULL, NULL);
    CFReadStreamClose((*client)->read_stream);

    CFWriteStreamSetClient((*client)->write_stream, kCFStreamEventNone, NULL, NULL);
    CFWriteStreamClose((*client)->write_stream);

    log_trace("%p client free\n", client);
    free((*client));
    (*client) = NULL;
}

void client_write_message(struct client_conn_t *client, CFHTTPMessageRef message) {
    log_trace("%p client\n", client);
    client->outgoing_message_data = CFHTTPMessageCopySerializedMessage(message);
    client->outgoing_message_data_i = 0;
    log_debug("%p client constructed buffer of %d bytes for sending\n", client, CFDataGetLength(client->outgoing_message_data));
    client_write_if_possible(client);
}

void client_write_if_possible(struct client_conn_t *client) {
    log_trace("%p client\n", client);

    if (!client->outgoing_message_data) {
        log_trace("%p client: no outgoing message-data\n");
        return;
    }

    if (!client->can_accept_bytes) {
        log_trace("%p client: can't accept byte yet\n");
        return;
    }

    if (client->proxy->state != CLIENT_SENDING) {
        proxy_state(client->proxy, CLIENT_SENDING);
    }

    const UInt8 *buff = client->outgoing_message_data_i + CFDataGetBytePtr(client->outgoing_message_data);
    CFIndex bytes_left = CFDataGetLength(client->outgoing_message_data) - client->outgoing_message_data_i;

    // Write
    CFIndex bytes_written = CFWriteStreamWrite(client->write_stream, buff, bytes_left);

    if (log_get_level() == LOG_TRACE) {
        dump_hex("client write", (void*) buff, (int) bytes_written);
    }

    client->outgoing_message_data_i += bytes_written;

    log_debug("%p client: %d bytes written, %d total bytes written of %d\n",
              client,
              bytes_written,
              client->outgoing_message_data_i,
              CFDataGetLength(client->outgoing_message_data));

    if (CFDataGetLength(client->outgoing_message_data) == client->outgoing_message_data_i) {
        log_debug("%p client: full response sent back to client\n");
        client->outgoing_message_data = NULL;
        client->outgoing_message_data_i = 0;
        proxy_state(client->proxy, COMPLETED);
    }

    client->can_accept_bytes = false;
}

static void client_read_stream_callback(CFReadStreamRef stream, CFStreamEventType type, void *info) {
    struct client_conn_t *client = (struct client_conn_t *) info;
    log_trace("%p client: read-event: %s on fd=%d\n", client, CFStreamEventTypeString(type), client->fd);

    switch (type) {
        case kCFStreamEventHasBytesAvailable:
            if (client->proxy->state != AVAILABLE && client->proxy->state != COMPLETED) {
                log_warn("%p client: haven't fully processed last request...\n");
            }

            if (client->incoming_message == nil) {
                proxy_state(client->proxy, CLIENT_READING);
                client->incoming_message = CFHTTPMessageCreateEmpty(kCFAllocatorDefault, true);
            }

            memset(client->read_buf, 0, sizeof(client->read_buf));
            CFIndex nbr_read = CFReadStreamRead(stream, client->read_buf, sizeof(client->read_buf));

            if (log_get_level() == LOG_TRACE) {
                if (nbr_read > 0) {
                    dump_hex("client read", client->read_buf, (int) nbr_read);
                } else {
                    log_trace("read 0 bytes\n");
                }
            }

            if (nbr_read > 0 && !CFHTTPMessageAppendBytes(client->incoming_message, client->read_buf, nbr_read)) {
                log_error("couldn't append bytes...\n");
            }

            if (nbr_read == 0) {
                // zero bytes - it was closed - will be handled in `kCFStreamEventEndEncountered`
            } else if (!CFReadStreamHasBytesAvailable(stream)) {
                proxy_signal_client_req_recv(client);
            } else {
                log_trace("%p client: more bytes in pipe\n", client);
            }

            break;
        case kCFStreamEventErrorOccurred:
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
            client_write_if_possible(client);
            break;
        case kCFStreamEventErrorOccurred:
            proxy_signal_client_error(client, CFWriteStreamGetError(stream));
            break;
        case kCFStreamEventEndEncountered:
            proxy_signal_client_eof(client);
            break;
        case kCFStreamEventOpenCompleted:
            break;
    }
}
