//
//  server_connection.c
//  dproxy
//
//  Created by David Everlöf on 2018-01-20.
//  Copyright © 2018 David Everlöf. All rights reserved.
//

#include "server_connection.h"
#include "log.h"
#include "utils.h"
#include "proxy_channel.h"

#include <netinet/in.h>
#include <arpa/inet.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CFNetwork/CFNetwork.h>


static void server_write_stream_callback(CFWriteStreamRef stream, CFStreamEventType type, void *info);
static void server_read_stream_callback(CFReadStreamRef stream, CFStreamEventType type, void *info);

static void server_domain_resolution_completed(CFHostRef host, CFHostInfoType typeInfo, const CFStreamError *error, void *info);

void server_start_resolve(struct server_conn_t *server);

struct server_conn_t* server_create() {
    struct server_conn_t *server = (struct server_conn_t *) calloc(1, sizeof(struct server_conn_t));
    log_trace("%p server alloc\n", server);
    return server;
}

void server_free(struct server_conn_t** server) {
    CFReadStreamSetClient((*server)->readStream, kCFStreamEventNone, NULL, NULL);
    CFReadStreamClose((*server)->readStream);

    CFWriteStreamSetClient((*server)->writeStream, kCFStreamEventNone, NULL, NULL);
    CFWriteStreamClose((*server)->writeStream);

    if ((*server)->host) CFRelease((*server)->host);
    if ((*server)->server_hostname) CFRelease((*server)->server_hostname);

    log_trace("%p server free\n", server);

    free((*server));
    (*server) = NULL;
}

void setup_streams(struct server_conn_t *server, struct sockaddr * addr) {
    log_trace("%p server\n", server);

    char *server_ip_addr = NULL;
    switch(addr->sa_family) {
        case AF_INET: {
            struct sockaddr_in *addr_in = (struct sockaddr_in *)addr;
            server_ip_addr = malloc(INET_ADDRSTRLEN);
            inet_ntop(AF_INET, &(addr_in->sin_addr), server_ip_addr, INET_ADDRSTRLEN);
            break;
        }
        case AF_INET6: {
            struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)addr;
            server_ip_addr = malloc(INET6_ADDRSTRLEN);
            inet_ntop(AF_INET6, &(addr_in6->sin6_addr), server_ip_addr, INET6_ADDRSTRLEN);
            break;
        }
        default:
            break;
    }

    log_info("%p conn: dns resolved '%s' => %s\n", server, CFStringGetCStringPtr(server->server_hostname, kCFStringEncodingUTF8), server_ip_addr);
    free(server_ip_addr);

    CFStreamCreatePairWithSocketToCFHost(kCFAllocatorDefault, server->host, server->port_nbr, &server->readStream, &server->writeStream);

    if (server->readStream && server->writeStream) {
        CFReadStreamSetProperty(server->readStream, kCFStreamPropertyShouldCloseNativeSocket, kCFBooleanTrue);
        CFWriteStreamSetProperty(server->writeStream, kCFStreamPropertyShouldCloseNativeSocket, kCFBooleanTrue);
    }

    server->stream_context.version = 0;
    server->stream_context.info = (void *)(server);
    server->stream_context.retain = nil;
    server->stream_context.release = nil;
    server->stream_context.copyDescription = nil;

    CFStreamEventType events =
        kCFStreamEventOpenCompleted   |
        kCFStreamEventErrorOccurred   |
        kCFStreamEventEndEncountered;

    CFWriteStreamSetClient(server->writeStream,
                           events | kCFStreamEventCanAcceptBytes,
                           &server_write_stream_callback,
                           &server->stream_context);

    CFWriteStreamScheduleWithRunLoop(server->writeStream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    CFWriteStreamOpen(server->writeStream);

    CFReadStreamSetClient(server->readStream,
                          events | kCFStreamEventHasBytesAvailable,
                          &server_read_stream_callback,
                          &server->stream_context);

    CFReadStreamScheduleWithRunLoop(server->readStream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    CFReadStreamOpen(server->readStream);
}

void server_process_request(struct server_conn_t *server, CFHTTPMessageRef message) {
    server->outgoing_message = CFHTTPMessageCreateCopy(kCFAllocatorDefault, message);
    server_write_message(server, server->outgoing_message);

    if (!server->host) {
        CFURLRef server_url = CFHTTPMessageCopyRequestURL(message);
        server->server_hostname = CFURLCopyHostName(server_url);
        CFStringRef scheme = CFURLCopyScheme(server_url);

        CFHostRef host = CFHostCreateWithName(kCFAllocatorDefault, server->server_hostname);
        SInt32 port_nbr = CFURLGetPortNumber(server_url);

        server->host = host;
        server->port_nbr = port_nbr;

        if (server->port_nbr == -1) {
            server->port_nbr = CFStringCompare(scheme, CFSTR("http"), kCFCompareCaseInsensitive) == kCFCompareEqualTo ? 80 : 433;
        }

        server_start_resolve(server);
        proxy_state(server->proxy, SERVER_RESOLVING_HOSTNAME);

        if (scheme) CFRelease(scheme);
        if (server_url) CFRelease(server_url);
    }
}

void server_start_resolve(struct server_conn_t *server) {
    server->dns_context.version = 0;
    server->dns_context.info = (void *)(server);
    server->dns_context.retain = nil;
    server->dns_context.release = nil;
    server->dns_context.copyDescription = nil;

    CFStreamError streamError;
    CFHostSetClient(server->host, server_domain_resolution_completed, &(server->dns_context));
    CFHostScheduleWithRunLoop(server->host, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    Boolean started = CFHostStartInfoResolution(server->host, kCFHostAddresses, &streamError);
    if (!started) log_warn("%s conn: couldn't start server dns resolution\n", server);
}

void server_domain_resolution_completed(CFHostRef host, CFHostInfoType typeInfo, const CFStreamError *error, void *info)
{
    struct server_conn_t *server = (struct server_conn_t *) info;
    CFHostUnscheduleFromRunLoop(host, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

    Boolean hostsResolved;
    CFArrayRef addressesArray = CFHostGetAddressing(host, &hostsResolved);

    if (!hostsResolved) {
        log_warn("Couldn't resolve host...");
    } else if (addressesArray && CFArrayGetCount(addressesArray) > 0) {
        CFDataRef socketData = (CFDataRef) CFArrayGetValueAtIndex(addressesArray, 0);
        CFDataRef socketDataCopy = CFDataCreateCopy(kCFAllocatorDefault, socketData);
        struct sockaddr *addr = (struct sockaddr *) CFDataGetBytePtr(socketDataCopy);
        setup_streams(server, addr);
        CFRelease(socketDataCopy);
    }
}

void server_write_message(struct server_conn_t *server, CFHTTPMessageRef message) {
    log_trace("%p server\n", server);
    server->outgoing_message_data = CFHTTPMessageCopySerializedMessage(message);
    server->outgoing_message_data_i = 0;
    log_debug("%p server constructed buffer of %d bytes for sending\n", server, CFDataGetLength(server->outgoing_message_data));
    server_write_if_possible(server);
}

void server_write_if_possible(struct server_conn_t *server) {
    log_trace("%p server\n", server);

    if (!server->outgoing_message_data) {
        log_trace("%p server: no outgoing message-data\n");
        return;
    }

    if (!server->can_accept_bytes) {
        log_trace("%p server: can't accept byte yet\n");
        return;
    }

    if (server->proxy->state != SERVER_SENDING) {
        proxy_state(server->proxy, SERVER_SENDING);
    }

    const UInt8 *buff = server->outgoing_message_data_i + CFDataGetBytePtr(server->outgoing_message_data);
    CFIndex bytes_left = CFDataGetLength(server->outgoing_message_data) - server->outgoing_message_data_i;

    // Write
    CFIndex bytes_written = CFWriteStreamWrite(server->writeStream, buff, bytes_left);

    if (log_get_level() == LOG_TRACE) {
        dump_hex("server write", (void*) buff, (int) bytes_written);
    }

    server->outgoing_message_data_i += bytes_written;

    log_debug("%p server: %d bytes written, %d total bytes written of %d\n",
              server,
              bytes_written,
              server->outgoing_message_data_i,
              CFDataGetLength(server->outgoing_message_data));

    if (CFDataGetLength(server->outgoing_message_data) == server->outgoing_message_data_i) {
        log_debug("%p server: full response sent to server\n");
        server->outgoing_message_data = NULL;
        server->outgoing_message_data_i = 0;
    }

    server->can_accept_bytes = false;
}

static void server_write_stream_callback(CFWriteStreamRef stream, CFStreamEventType type, void *info) {
    struct server_conn_t *server = (struct server_conn_t *) info;
    log_trace("%p server: write-event: %s on fd=%d\n", server, CFStreamEventTypeString(type), server->fd);

    switch (type) {
        case kCFStreamEventCanAcceptBytes:
            server->can_accept_bytes = true;
            server_write_if_possible(server);
            break;
        case kCFStreamEventErrorOccurred:
            proxy_signal_server_error(server, CFWriteStreamGetError(stream));
            break;
        case kCFStreamEventEndEncountered:
            proxy_signal_server_eof(server);
            break;
        case kCFStreamEventOpenCompleted:
            break;
    }
}

static void server_read_stream_callback(CFReadStreamRef stream, CFStreamEventType type, void *info) {
    struct server_conn_t *server = (struct server_conn_t *) info;
    log_trace("%p server: read-event: %s on fd=%d\n", server, CFStreamEventTypeString(type), server->fd);

    switch (type) {
        case kCFStreamEventHasBytesAvailable:
            if (server->incoming_message == nil) {
                proxy_state(server->proxy, SERVER_READING);
                server->incoming_message = CFHTTPMessageCreateEmpty(kCFAllocatorDefault, false);
            }

            memset(server->read_buf, 0, sizeof(server->read_buf));
            CFIndex nbr_read = CFReadStreamRead(stream, server->read_buf, sizeof(server->read_buf));

            if (log_get_level() == LOG_TRACE) {
                if (nbr_read > 0) {
                    dump_hex("server read", server->read_buf, (int) nbr_read);
                } else {
                    log_trace("read 0 bytes\n");
                }
            }
            
            if (!CFHTTPMessageAppendBytes(server->incoming_message, server->read_buf, nbr_read)) {
                log_error("couldn't append bytes...\n");
            }

            if (!CFReadStreamHasBytesAvailable(stream)) {
                proxy_signal_server_req_recv(server);
            } else {
                log_trace("%p server: more bytes in pipe\n", server);
            }

            break;
        case kCFStreamEventErrorOccurred:
            proxy_signal_server_error(server, CFReadStreamGetError(stream));
            break;
        case kCFStreamEventEndEncountered:
            proxy_signal_server_eof(server);
            break;
        case kCFStreamEventOpenCompleted:
            break;
    }
}
