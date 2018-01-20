#include "proxy.h"
#include "proxy_channel.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CFNetwork/CFNetwork.h>

#include "log.h"

#include <stdio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>


// https://developer.apple.com/library/content/samplecode/MiniSOAP/Listings/TCPServer_m.html
// https://developer.apple.com/library/content/documentation/Networking/Conceptual/CFNetwork/CFStreamTasks/CFStreamTasks.html

void proxy_accept_callback(CFSocketRef socket, CFSocketCallBackType type, CFDataRef address, const void *data, void *info)
{
    assert(kCFSocketAcceptCallBack == type);
    (void) proxy_channel_create(*(CFSocketNativeHandle *)data);
}

void proxy_start(int port)
{
    // Socket for `accept`
    CFSocketRef serverSocket = CFSocketCreate(kCFAllocatorDefault,
                                              PF_INET,
                                              SOCK_STREAM,
                                              IPPROTO_TCP,
                                              kCFSocketAcceptCallBack,
                                              proxy_accept_callback,
                                              NULL);

    int reuse = true;
    int fileDescriptor = CFSocketGetNative(serverSocket);
    if (setsockopt(fileDescriptor, SOL_SOCKET, SO_REUSEADDR, (void *)&reuse, sizeof(int)) != 0)
    {
        log_error("Coulnd't set SO_REUSEADDR for server socket.\n");
    }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_len = sizeof(sin);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);

    CFDataRef sincfd = CFDataCreate(kCFAllocatorDefault, (UInt8 *)&sin, sizeof(sin));
    CFSocketSetAddress(serverSocket, sincfd);
    CFRelease(sincfd);

    CFRunLoopSourceRef socketSource = CFSocketCreateRunLoopSource(kCFAllocatorDefault, serverSocket, 0);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), socketSource, kCFRunLoopDefaultMode);

    log_trace("server running on port=%d\n", port);
}
