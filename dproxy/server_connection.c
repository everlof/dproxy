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

static void conn_domain_resolution_completed(CFHostRef host, CFHostInfoType typeInfo, const CFStreamError *error, void *info);

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

    if ((*server)) CFRelease((*server)->host);

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

    log_info("%p conn: dns for server resolved to => %s\n", server, server_ip_addr);
    free(server_ip_addr);

    server->outgoing_message = CFHTTPMessageCreateCopy(kCFAllocatorDefault, P_CHAN(server)->client->incoming_message);

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

    CFWriteStreamSetClient(server->writeStream,
                           kCFStreamEventOpenCompleted     |
                           kCFStreamEventErrorOccurred     |
                           kCFStreamEventEndEncountered    |
                           kCFStreamEventCanAcceptBytes,
                           &server_write_stream_callback,
                           &server->stream_context);

    CFWriteStreamScheduleWithRunLoop(server->writeStream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    CFWriteStreamOpen(server->writeStream);

    CFReadStreamSetClient(server->readStream,
                          kCFStreamEventOpenCompleted      |
                          kCFStreamEventErrorOccurred      |
                          kCFStreamEventEndEncountered     |
                          kCFStreamEventHasBytesAvailable,
                          &server_read_stream_callback,
                          &server->stream_context);

    CFReadStreamScheduleWithRunLoop(server->readStream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    CFReadStreamOpen(server->readStream);
}

void server_start_resolve(struct server_conn_t *server) {
    server->dns_context.version = 0;
    server->dns_context.info = (void *)(server);
    server->dns_context.retain = nil;
    server->dns_context.release = nil;
    server->dns_context.copyDescription = nil;

    CFStreamError streamError;
    CFHostSetClient(server->host, conn_domain_resolution_completed, &(server->dns_context));
    CFHostScheduleWithRunLoop(server->host, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    Boolean started = CFHostStartInfoResolution(server->host, kCFHostAddresses, &streamError);
    if (!started) log_warn("%s conn: couldn't start server dns resolution\n");
}

void conn_domain_resolution_completed(CFHostRef host, CFHostInfoType typeInfo, const CFStreamError *error, void *info)
{
    struct server_conn_t *server = (struct server_conn_t *) info;
    CFHostUnscheduleFromRunLoop(host, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

    Boolean hostsResolved;
    CFArrayRef addressesArray = CFHostGetAddressing(host, &hostsResolved);

    if (!hostsResolved) {
        log_warn("Couldn't resolve host...");
    } else if (addressesArray && CFArrayGetCount(addressesArray) > 0)
    {
        CFDataRef socketData = (CFDataRef) CFArrayGetValueAtIndex(addressesArray, 0);

        // Here our connection only accepts port 25 - so we must explicitly change this
        // first, we copy the data so we can change it
        CFDataRef socketDataCopy = CFDataCreateCopy(kCFAllocatorDefault, socketData);
        struct sockaddr *addr = (struct sockaddr *) CFDataGetBytePtr(socketDataCopy);

        setup_streams(server, addr);
    }


}

static void server_write_stream_callback(CFWriteStreamRef stream, CFStreamEventType type, void *info) {
    struct server_conn_t *server = (struct server_conn_t *) info;
    log_trace("%p server: write-event: %s on fd=%d\n", server, CFStreamEventTypeString(type), server->fd);

    switch (type) {
        case kCFStreamEventCanAcceptBytes:
            if (server->outgoing_message) {
                CFDataRef data = CFHTTPMessageCopySerializedMessage(server->outgoing_message);
                CFIndex length = CFDataGetLength(data);
                UInt8 buff[length];
                CFDataGetBytes(data, CFRangeMake(0, length), buff);
                CFWriteStreamWrite(server->writeStream, buff, length);
                CFRelease(server->outgoing_message);
                CFRelease(data);
                server->outgoing_message = NULL;
            }
            break;
        case kCFStreamEventErrorOccurred:
            log_debug("kCFStreamEventErrorOccurred\n");
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
                server->incoming_message = CFHTTPMessageCreateEmpty(kCFAllocatorDefault, true);
            }

            memset(server->read_buf, 0, sizeof(server->read_buf));
            CFIndex nbr_read = CFReadStreamRead(stream, server->read_buf, sizeof(server->read_buf));

            if (!CFHTTPMessageAppendBytes(server->incoming_message, server->read_buf, nbr_read)) {
                log_error("couldn't append bytes...\n");
            }

            if (CFHTTPMessageIsHeaderComplete(server->incoming_message)) {
                log_trace("%p conn: http-message completed\n", server);
                proxy_signal_server_req_recv(server);

                CFDataRef data = CFHTTPMessageCopySerializedMessage(server->incoming_message);
                CFIndex length = CFDataGetLength(data);
                UInt8 buff[length + 1];
                CFDataGetBytes(data, CFRangeMake(0, length), buff);
                buff[length] = '\0';
                log_trace(">>>%s<<<\n", (char*) buff);
                CFRelease(data);
            }

            break;
        case kCFStreamEventErrorOccurred:
            log_debug("kCFStreamEventErrorOccurred\n");
            proxy_signal_server_error(server, CFReadStreamGetError(stream));
            break;
        case kCFStreamEventEndEncountered:
            proxy_signal_server_eof(server);
            break;
        case kCFStreamEventOpenCompleted:
            break;
    }
}
