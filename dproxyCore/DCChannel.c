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

/*static void __DCChannelResolveComplete(CFHostRef host, CFHostInfoType typeInfo, const CFStreamError *error, void *info)
{
    DCChannelRef channel = (DCChannelRef) info;
    CFHostUnscheduleFromRunLoop(host, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

    Boolean hostsResolved;
    CFArrayRef addressesArray = CFHostGetAddressing(host, &hostsResolved);

    if (!hostsResolved) {
        log_warn("Couldn't resolve host...");
    } else if (addressesArray && CFArrayGetCount(addressesArray) > 0) {
        CFDataRef socketData = (CFDataRef) CFArrayGetValueAtIndex(addressesArray, 0);
        CFDataRef socketDataCopy = CFDataCreateCopy(kCFAllocatorDefault, socketData);
        struct sockaddr *addr = (struct sockaddr *) CFDataGetBytePtr(socketDataCopy);



        DCConnectionSetupWithHost(channel->server, host, channel->port)
        CFRelease(socketDataCopy);
    }
}*/

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

    /*
    channel->dnsContext.version = 0;
    channel->dnsContext.info = (void *)(channel);
    channel->dnsContext.retain = nil;
    channel->dnsContext.release = nil;
    channel->dnsContext.copyDescription = nil;

    CFStreamError streamError;
    CFHostSetClient(channel->host, __DCChannelResolveComplete, &(channel->dnsContext));
    CFHostScheduleWithRunLoop(channel->host, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    Boolean started = CFHostStartInfoResolution(channel->host, kCFHostAddresses, &streamError);
    if (!started)
        log_warn("channel=%s: couldn't start server dns resolution\n", channel);
     */
}

static void __DCChannelConnectionCallback(DCConnectionRef connection, DCConnectionCallbackEvents type, CFDataRef address, const void *data, void *info) {
    DCChannelRef channel = (DCChannelRef) info;
    log_debug("channel=%p, connectionCallback => %p, event => %s\n", channel, connection, DCConnectionCallbackTypeString(type));

    switch (type) {
        case kDCConnectionCallbackTypeIncomingMessage:
        {
            while (DCConnectionHasReceived(connection)) {
                CFHTTPMessageRef next = DCConnectionGetNextReceived(connection);
                if (!channel->host)
                    __DCChannelSetupServer(channel, next);
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

void DCChannelSetupWithFD(DCChannelRef channel, CFSocketNativeHandle fd) {
    channel->client = DCConnectionCreate(channel);
    DCConnectionSetTalksTo(channel->client, kDCConnectionTypeClient);

    channel->server = DCConnectionCreate(channel);
    DCConnectionSetTalksTo(channel->server, kDCConnectionTypeServer);

    DCConnectionContext context;
    context.info = channel;
    DCConnectionSetClient(channel->client,
                          kDCConnectionCallbackTypeIncomingMessage |
                          kDCConnectionCallbackTypeConnectionEOF,
                          __DCChannelConnectionCallback,
                          &context);

    DCConnectionSetupWithFD(channel->client, fd);
}

void DCChannelRelease(DCChannelRef channel) {
    if (channel->host) CFRelease(channel->host);
    free(channel);
}

