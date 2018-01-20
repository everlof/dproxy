#include "conn.h"
#include "log.h"
#include "proxy_channel.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CFNetwork/CFNetwork.h>

// DNS
static void conn_domain_resolution_completed(CFHostRef host, CFHostInfoType typeInfo, const CFStreamError *error, void *info);

// CLIENT
static void client_write_stream_callback(CFWriteStreamRef stream, CFStreamEventType type, void *info);
static void client_read_stream_callback(CFReadStreamRef stream, CFStreamEventType type, void *info);

// SERVER
static void server_write_stream_callback(CFWriteStreamRef stream, CFStreamEventType type, void *info);
static void server_read_stream_callback(CFReadStreamRef stream, CFStreamEventType type, void *info);

// TO REMOVE:
static void server_sock_callback(CFSocketRef socket, CFSocketCallBackType type, CFDataRef address, const void *data, void *info);

const char *sendChar =
"HTTP/1.1 200 OK\r\n"
"Date: Mon, 27 Jul 2009 12:28:53 GMT\r\n"
"Server: Apache/2.2.14 (Win32)\r\n"
"Last-Modified: Wed, 22 Jul 2009 19:15:56 GMT\r\n"
"Content-Length: 48\r\n"
"Content-Type: text/html\r\n"
"Connection: Closed\r\n"
"\r\n"
"<html>"
"<body>"
"<h1>Hello, World!</h1>"
"</body>"
"</html>";

void conn_deinit(struct conn_t* conn) {
    CFReadStreamSetClient(conn->readStream, kCFStreamEventNone, NULL, NULL);
    CFReadStreamClose(conn->readStream);

    CFWriteStreamSetClient(conn->writeStream, kCFStreamEventNone, NULL, NULL);
    CFWriteStreamClose(conn->writeStream);

    // This appearently isn't needed since close does unschedule from `RunLoop`
    // CFReadStreamUnscheduleFromRunLoop((*conn)->readStream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    // CFWriteStreamUnscheduleFromRunLoop((*conn)->writeStream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

    log_trace("%p conn\n", *conn);
}

void conn_init(struct conn_t *c, CFSocketNativeHandle fd) {
    log_trace("%p conn: init, fd=%d\n", c, (int) fd);
    c->fd = fd;

    memset(c->remote_addr_str, 0, sizeof(c->remote_addr_str));
    uint8_t name[SOCK_MAXADDRLEN];
    socklen_t namelen = sizeof(name);
    if (0 == getpeername(c->fd, (struct sockaddr *)name, &namelen)) {
        switch(((struct sockaddr *)name)->sa_family) {
            case AF_INET: {
                struct sockaddr_in *addr_in = (struct sockaddr_in *)name;
                inet_ntop(AF_INET, &(addr_in->sin_addr), c->remote_addr_str, INET_ADDRSTRLEN);
                break;
            }
            case AF_INET6: {
                struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)name;
                inet_ntop(AF_INET6, &(addr_in6->sin6_addr), c->remote_addr_str, INET6_ADDRSTRLEN);
                break;
            }
            default:
                break;
        }

        if (c->is_client)
            log_trace("%p conn: client ip connected => %s:%d\n", c, c->remote_addr_str, ((struct sockaddr_in *)name)->sin_port);
        else
            log_trace("%p conn: connected to server => %s:%d\n", c, c->remote_addr_str, ((struct sockaddr_in *)name)->sin_port);
    }

    // Setup our socket pairs
    CFStreamCreatePairWithSocket(kCFAllocatorDefault,
                                 c->fd,
                                 &(c->readStream),
                                 &(c->writeStream));

    if (c->readStream && c->writeStream) {
        CFReadStreamSetProperty(c->readStream, kCFStreamPropertyShouldCloseNativeSocket, kCFBooleanTrue);
        CFWriteStreamSetProperty(c->writeStream, kCFStreamPropertyShouldCloseNativeSocket, kCFBooleanTrue);
    }

    c->stream_context.version = 0;
    c->stream_context.info = (void *)(c);
    c->stream_context.retain = nil;
    c->stream_context.release = nil;
    c->stream_context.copyDescription = nil;

    CFWriteStreamSetClient(c->writeStream,
                           kCFStreamEventOpenCompleted     |
                           kCFStreamEventErrorOccurred     |
                           kCFStreamEventEndEncountered    |
                           kCFStreamEventCanAcceptBytes,
                           c->is_client ? &client_write_stream_callback : &server_write_stream_callback,
                           &c->stream_context);

    CFWriteStreamScheduleWithRunLoop(c->writeStream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    CFWriteStreamOpen(c->writeStream);

    CFReadStreamSetClient(c->readStream,
                          kCFStreamEventOpenCompleted      |
                          kCFStreamEventErrorOccurred      |
                          kCFStreamEventEndEncountered     |
                          kCFStreamEventHasBytesAvailable,
                          c->is_client ? &client_read_stream_callback : &server_read_stream_callback,
                          &c->stream_context);

    CFReadStreamScheduleWithRunLoop(c->readStream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    CFReadStreamOpen(c->readStream);
}

char* CFStreamEventTypeString(CFStreamEventType type) {
    switch (type)
    {
        case kCFStreamEventNone: return "kCFStreamEventNone";
        case kCFStreamEventOpenCompleted: return "kCFStreamEventOpenCompleted";
        case kCFStreamEventHasBytesAvailable: return "kCFStreamEventHasBytesAvailable";
        case kCFStreamEventCanAcceptBytes: return "kCFStreamEventCanAcceptBytes";
        case kCFStreamEventErrorOccurred: return "kCFStreamEventErrorOccurred";
        case kCFStreamEventEndEncountered: return "kCFStreamEventEndEncountered";
    }
    return "INVALID";
}

void conn_domain_resolution_completed(CFHostRef host, CFHostInfoType typeInfo, const CFStreamError *error, void *info)
{
    CFHostUnscheduleFromRunLoop(host, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

    struct conn_t *c = (struct conn_t *) info;

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
        log_info("%p conn: dns for server resolved to => %s\n", c, server_ip_addr);
        free(server_ip_addr);

        //CFSocketContext context = {0, (void *)c, NULL, NULL, NULL};
        //CFSocketRef socket = CFSocketCreate(kCFAllocatorDefault, PF_INET, SOCK_STREAM, IPPROTO_TCP, kCFSocketConnectCallBack, (CFSocketCallBack)server_sock_callback, &context);
        //CFSocketRef socket = CFSocketCreate(kCFAllocatorDefault, PF_INET, SOCK_STREAM, IPPROTO_TCP, 0, NULL, NULL);
        // CFSocketSetSocketFlags(socket, kCFSocketCloseOnInvalidate);

        // FIX FOR IPv6
        /*
        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_len = sizeof(sin);
        sin.sin_family = addr->sa_family;
        sin.sin_port = htons(80); // TODO
        sin.sin_addr.s_addr = ((struct sockaddr_in *)addr)->sin_addr.s_addr;

        CFDataRef sincfd = CFDataCreate(kCFAllocatorDefault, (UInt8 *)&sin, sizeof(sin));
        c->socket = socket;
        conn_init(c, CFSocketGetNative(c->socket));
        CFSocketConnectToAddress(socket, sincfd, -1);
        CFRelease(sincfd);*/

        c->outgoing_message = CFHTTPMessageCreateCopy(kCFAllocatorDefault, PCHAN(c)->client.incoming_message);

        CFStreamCreatePairWithSocketToCFHost(kCFAllocatorDefault, host, c->port_nbr, &c->readStream, &c->writeStream);

        if (c->readStream && c->writeStream) {
            CFReadStreamSetProperty(c->readStream, kCFStreamPropertyShouldCloseNativeSocket, kCFBooleanTrue);
            CFWriteStreamSetProperty(c->writeStream, kCFStreamPropertyShouldCloseNativeSocket, kCFBooleanTrue);
        }

        c->stream_context.version = 0;
        c->stream_context.info = (void *)(c);
        c->stream_context.retain = nil;
        c->stream_context.release = nil;
        c->stream_context.copyDescription = nil;

        CFWriteStreamSetClient(c->writeStream,
                               kCFStreamEventOpenCompleted     |
                               kCFStreamEventErrorOccurred     |
                               kCFStreamEventEndEncountered    |
                               kCFStreamEventCanAcceptBytes,
                               c->is_client ? &client_write_stream_callback : &server_write_stream_callback,
                               &c->stream_context);

        CFWriteStreamScheduleWithRunLoop(c->writeStream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        CFWriteStreamOpen(c->writeStream);

        CFReadStreamSetClient(c->readStream,
                              kCFStreamEventOpenCompleted      |
                              kCFStreamEventErrorOccurred      |
                              kCFStreamEventEndEncountered     |
                              kCFStreamEventHasBytesAvailable,
                              c->is_client ? &client_read_stream_callback : &server_read_stream_callback,
                              &c->stream_context);

        CFReadStreamScheduleWithRunLoop(c->readStream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        CFReadStreamOpen(c->readStream);
    }

    if (host) CFRelease(host);
}


void conn_start_resolve(struct conn_t *conn, CFHostRef host) {
    conn->dns_context.version = 0;
    conn->dns_context.info = (void *)(conn);
    conn->dns_context.retain = nil;
    conn->dns_context.release = nil;
    conn->dns_context.copyDescription = nil;

    CFStreamError streamError;
    CFHostSetClient(host, conn_domain_resolution_completed, &(conn->dns_context));
    CFHostScheduleWithRunLoop(host, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    Boolean started = CFHostStartInfoResolution(host, kCFHostAddresses, &streamError);
    if (!started) log_warn("%s conn: couldn't start server dns resolution\n");
}

static void client_read_stream_callback(CFReadStreamRef stream, CFStreamEventType type, void *info) {
    struct conn_t *c = (struct conn_t *) info;
    log_trace("%p conn: read-event: %s on fd=%d\n", c, CFStreamEventTypeString(type), c->fd);

    switch (type) {
        case kCFStreamEventHasBytesAvailable:
            if (c->incoming_message == nil) {
                c->incoming_message = CFHTTPMessageCreateEmpty(kCFAllocatorDefault, true);
            }

            memset(c->read_buf, 0, sizeof(c->read_buf));
            CFIndex nbr_read = CFReadStreamRead(stream, c->read_buf, sizeof(c->read_buf));

            if (!CFHTTPMessageAppendBytes(c->incoming_message, c->read_buf, nbr_read)) {
                log_error("couldn't append bytes...\n");
            }

            if (CFHTTPMessageIsHeaderComplete(c->incoming_message)) {
                log_trace("%p conn: http-message completed\n", c);
                proxy_channel_signal_request_receievd(c);

                //CFDictionaryRef dict = CFHTTPMessageCopyAllHeaderFields(c->incomingMessage);
                //CFIndex nbrItems = CFDictionaryGetCount(dict);
                //CFTypeRef *keysTypeRef = (CFTypeRef *) malloc( nbrItems * sizeof(CFTypeRef) );
                //CFDictionaryGetKeysAndValues(dict, (const void **) keysTypeRef, NULL);
                //const void **keys = (const void **) keysTypeRef;
                //CFCopyDescription((CFTypeRef*) **keys);
                //CFDictionaryApplyFunction(dict, printKeys, NULL)
                //CFRelease(dict);

                /*
                 static void printKeys (const void* key, const void* value, void* context) {
                 CFShow(key);
                 CFShow(value);
                 }
                 */
            }

            break;
        case kCFStreamEventErrorOccurred:
            log_debug("kCFStreamEventErrorOccurred\n");
            CFStreamError error = CFReadStreamGetError(stream);

            if (error.domain == kCFStreamErrorDomainPOSIX) {
                log_warn("Error occured: %s\n", strerror(error.error));
            } else {
                log_error("UNHANDLED ERROR: domain: %d\n", error.domain);
            }

            break;
        case kCFStreamEventEndEncountered:
            proxy_channel_signal_end(c);
            break;
        case kCFStreamEventOpenCompleted:
            break;
    }
}

static void client_write_stream_callback(CFWriteStreamRef stream, CFStreamEventType type, void *info) {
    struct conn_t *c = (struct conn_t *) info;
    log_trace("%p conn: write-event: %s on fd=%d\n", c, CFStreamEventTypeString(type), c->fd);

    switch (type) {
        case kCFStreamEventCanAcceptBytes:
            c->can_accept_bytes = true;
            break;
        case kCFStreamEventErrorOccurred:
            log_debug("kCFStreamEventErrorOccurred\n");
            CFStreamError error = CFWriteStreamGetError(stream);

            if (error.domain == kCFStreamErrorDomainPOSIX) {
                log_warn("Error occured: %s\n", strerror(error.error));
            } else {
                log_error("UNHANDLED ERROR: domain: %d\n", error.domain);
            }

            break;
        case kCFStreamEventEndEncountered:
            proxy_channel_signal_end(c);
            break;
        case kCFStreamEventOpenCompleted:
            break;
    }
}

static void server_write_stream_callback(CFWriteStreamRef stream, CFStreamEventType type, void *info) {
    struct conn_t *c = (struct conn_t *) info;
    log_trace("%p conn: write-event: %s on fd=%d\n", c, CFStreamEventTypeString(type), c->fd);

    switch (type) {
        case kCFStreamEventCanAcceptBytes:
            if (c->outgoing_message) {
                CFDataRef data = CFHTTPMessageCopySerializedMessage(c->outgoing_message);
                CFIndex length = CFDataGetLength(data);
                UInt8 buff[length];
                CFDataGetBytes(data, CFRangeMake(0, length), buff);
                CFWriteStreamWrite(c->writeStream, buff, length);
                CFRelease(c->outgoing_message);
                CFRelease(data);
                c->outgoing_message = NULL;
            }
            break;
        case kCFStreamEventErrorOccurred:
            log_debug("kCFStreamEventErrorOccurred\n");
            CFStreamError error = CFWriteStreamGetError(stream);

            if (error.domain == kCFStreamErrorDomainPOSIX) {
                log_warn("Error occured: %s\n", strerror(error.error));
            } else {
                log_error("UNHANDLED ERROR: domain: %d\n", error.domain);
            }

            break;
        case kCFStreamEventEndEncountered:
            proxy_channel_signal_end(c);
            break;
        case kCFStreamEventOpenCompleted:
            break;
    }
}

static void server_read_stream_callback(CFReadStreamRef stream, CFStreamEventType type, void *info) {
    struct conn_t *c = (struct conn_t *) info;
    log_trace("%p conn: read-event: %s on fd=%d\n", c, CFStreamEventTypeString(type), c->fd);

    switch (type) {
        case kCFStreamEventHasBytesAvailable:
            if (c->incoming_message == nil) {
                c->incoming_message = CFHTTPMessageCreateEmpty(kCFAllocatorDefault, true);
            }

            memset(c->read_buf, 0, sizeof(c->read_buf));
            CFIndex nbr_read = CFReadStreamRead(stream, c->read_buf, sizeof(c->read_buf));

            if (!CFHTTPMessageAppendBytes(c->incoming_message, c->read_buf, nbr_read)) {
                log_error("couldn't append bytes...\n");
            }

            if (CFHTTPMessageIsHeaderComplete(c->incoming_message)) {
                log_trace("%p conn: http-message completed\n", c);
                proxy_channel_signal_request_receievd(c);

                CFDataRef data = CFHTTPMessageCopySerializedMessage(c->incoming_message);
                CFIndex length = CFDataGetLength(data);
                UInt8 buff[length + 1];
                CFDataGetBytes(data, CFRangeMake(0, length), buff);
                buff[length] = '\0';
                log_trace(">>>%s<<<\n", (char*) buff);
                CFRelease(data);

                //CFDictionaryRef dict = CFHTTPMessageCopyAllHeaderFields(c->incomingMessage);
                //CFIndex nbrItems = CFDictionaryGetCount(dict);
                //CFTypeRef *keysTypeRef = (CFTypeRef *) malloc( nbrItems * sizeof(CFTypeRef) );
                //CFDictionaryGetKeysAndValues(dict, (const void **) keysTypeRef, NULL);
                //const void **keys = (const void **) keysTypeRef;
                //CFCopyDescription((CFTypeRef*) **keys);
                //CFDictionaryApplyFunction(dict, printKeys, NULL)
                //CFRelease(dict);

                /*
                 static void printKeys (const void* key, const void* value, void* context) {
                 CFShow(key);

                 }
                 */
            }

            break;
        case kCFStreamEventErrorOccurred:
            log_debug("kCFStreamEventErrorOccurred\n");
            CFStreamError error = CFReadStreamGetError(stream);

            if (error.domain == kCFStreamErrorDomainPOSIX) {
                log_warn("Error occured: %s\n", strerror(error.error));
            } else {
                log_error("UNHANDLED ERROR: domain: %d\n", error.domain);
            }

            break;
        case kCFStreamEventEndEncountered:
            proxy_channel_signal_end(c);
            break;
        case kCFStreamEventOpenCompleted:
            break;
    }
}
