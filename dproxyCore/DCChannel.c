#include "DCChannel.h"
#include "DCConnection.h"
#include "log.h"

#include <CFNetwork/CFNetwork.h>
#include <arpa/inet.h>

#define TRACE(p) log_trace("channel=%p\n", p)

struct __DCChannel {
    DCConnectionRef client;
    DCConnectionRef server;

    SInt32 port;
    CFHostRef host;
    CFHostClientContext dnsContext;
};

DCChannelRef DCChannelCreate() {
    struct __DCChannel *channel = (struct __DCChannel *) calloc(1, sizeof(struct __DCChannel));
    TRACE(channel);
    return channel;
}

static void __DCChannelSetupServer(DCChannelRef channel, CFHTTPMessageRef message) {
    CFURLRef serverURL = CFHTTPMessageCopyRequestURL(message);
    CFStringRef serverHostname = CFURLCopyHostName(serverURL);
    CFStringRef scheme = CFURLCopyScheme(serverURL);

    CFHostRef host = CFHostCreateWithName(kCFAllocatorDefault, serverHostname);
    SInt32 port_nbr = CFURLGetPortNumber(serverURL);

    channel->host = host;
    channel->port = port_nbr;

    if (channel->port == -1) {
        channel->port = CFStringCompare(scheme, CFSTR("http"), kCFCompareCaseInsensitive) == kCFCompareEqualTo ? 80 : 433;
    }

    if (scheme) CFRelease(scheme);
    if (serverHostname) CFRelease(serverHostname);
    if (serverURL) CFRelease(serverURL);

    DCConnectionSetupWithHost(channel->server, host, channel->port);
}

static void __DCChannelLogHTTP(DCConnectionRef connection, CFHTTPMessageRef next) {
    char *type = DCConnectionGetType(connection) == kDCConnectionTypeServer ? "SERVER" : "CLIENT";
    DCChannelRef channel = DCConnectionGetChannel(connection);
    CFSocketNativeHandle fd = DCConnectionGetNativeHandle(connection);

    if (log_get_level() <= LOG_DEBUG && (CFHTTPMessageIsRequest(next))){
        CFStringRef method = CFHTTPMessageCopyRequestMethod(next);
        CFURLRef reqUrl = CFHTTPMessageCopyRequestURL(next);
        CFStringRef url = CFURLGetString(reqUrl);

        char buff[BUFSIZ];
        memset(buff, 0, BUFSIZ);
        CFStringGetCString(method, buff, BUFSIZ, kCFStringEncodingUTF8);

        char buff2[BUFSIZ];
        memset(buff2, 0, BUFSIZ);
        CFStringGetCString(url, buff2, BUFSIZ, kCFStringEncodingUTF8);

        log_debug("%s (fd: %d) (%p) | request => %s %s\n", type, fd, channel, buff, buff2);
    }

    if (log_get_level() <= LOG_DEBUG && (!CFHTTPMessageIsRequest(next))){
        CFIndex responseCode = CFHTTPMessageGetResponseStatusCode(next);
        log_debug("%s (fd: %d) (%p) | response => %d\n", type, fd, channel, responseCode);
    }
}

static void __DCChannelClientConnectionCallback(DCConnectionRef connection, DCConnectionCallbackEvents type, CFDataRef address, const void *data, void *info) {
    DCChannelRef channel = (DCChannelRef) info;
    log_trace("channel=%p, connectionCallback => %p, event => %s\n", channel, connection, DCConnectionCallbackTypeString(type));

    switch (type) {
        case kDCConnectionCallbackTypeIncomingMessage:
            {
                while (DCConnectionHasNext(connection)) {
                    CFHTTPMessageRef next = DCConnectionPopNext(connection);
                    if (!channel->host)
                        __DCChannelSetupServer(channel, next);
                    __DCChannelLogHTTP(connection, next);
                    DCConnectionAddOutgoing(channel->server, next);
                }
            }
            break;
        case kDCConnectionCallbackTypeConnectionEOF:
            log_trace("closing connection=%p\n", connection);
            DCConnectionClose(channel->client);
            DCConnectionClose(channel->server);
            break;
        default:
            break;
    }
}

static void __DCChannelServerConnectionCallback(DCConnectionRef connection, DCConnectionCallbackEvents type, CFDataRef address, const void *data, void *info) {
    DCChannelRef channel = (DCChannelRef) info;
    log_trace("channel=%p, connectionCallback => %p, event => %s\n", channel, connection, DCConnectionCallbackTypeString(type));

    switch (type) {
        case kDCConnectionCallbackTypeIncomingMessage:
            {
                while (DCConnectionHasNext(connection)) {
                    CFHTTPMessageRef next = DCConnectionPopNext(connection);
                    __DCChannelLogHTTP(connection, next);
                    DCConnectionAddOutgoing(channel->client, next);
                }
            }
            break;
        case kDCConnectionCallbackTypeConnectionEOF:
            log_trace("closing connection=%p\n", connection);
            DCConnectionClose(channel->client);
            DCConnectionClose(channel->server);
            break;
        default:
            break;
    }
}

void DCChannelSetupWithFD(DCChannelRef channel, CFSocketNativeHandle fd) {
    channel->client = DCConnectionCreate(channel);
    DCConnectionSetChannel(channel->client, channel);
    DCConnectionSetTalksTo(channel->client, kDCConnectionTypeClient);

    channel->server = DCConnectionCreate(channel);
    DCConnectionSetChannel(channel->server, channel);
    DCConnectionSetTalksTo(channel->server, kDCConnectionTypeServer);

    DCConnectionContext context;
    context.info = channel;

    DCConnectionSetClient(channel->client,
                          kDCConnectionCallbackTypeIncomingMessage |
                          kDCConnectionCallbackTypeConnectionEOF,
                          __DCChannelClientConnectionCallback,
                          &context);

    DCConnectionSetClient(channel->server,
                          kDCConnectionCallbackTypeIncomingMessage |
                          kDCConnectionCallbackTypeConnectionEOF,
                          __DCChannelServerConnectionCallback,
                          &context);

    DCConnectionSetupWithFD(channel->client, fd);
}

void DCChannelRelease(DCChannelRef channel) {
    if (channel->host) CFRelease(channel->host);
    free(channel);
}

