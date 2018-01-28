#include "DCProxy.h"
#include "DCChannel.h"
#include "log.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CFNetwork/CFNetwork.h>
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define TRACE(p) log_trace("proxy=%p\n", p)

struct __DCProxy {
    unsigned int port;
    CFRunLoopTimerRef timer;
};

DCProxyRef DCProxyCreate(unsigned int port) {
    struct __DCProxy *proxy = (struct __DCProxy *) calloc(1, sizeof(struct __DCProxy));
    if (proxy) {
        proxy->port = port;
    }
    return proxy;
}

static int tick = 0;
void __DCProxyTimerTick(CFRunLoopTimerRef timer, void *info) {
    if (tick % 2)
        printf("tock\n");
    else
        printf("tick\n");
    tick++;
}

void __DCProxyAccept(CFSocketRef socket, CFSocketCallBackType type, CFDataRef address, const void *data, void *info)
{
    assert(kCFSocketAcceptCallBack == type);
    // DCProxyRef proxy = (DCProxyRef) data;
    DCChannelRef channel = DCChannelCreate();
    DCChannelSetupWithFD(channel, *(CFSocketNativeHandle *)data);
}

void* __DCProxyRunServer(void* data) {
    DCProxyRef proxy = (DCProxyRef) data;

    // CREATE AND SCHEDULE TIMER
    CFRunLoopTimerContext timerContext = {0, NULL, NULL, NULL, NULL};
    CFRunLoopTimerRef timer = CFRunLoopTimerCreate(kCFAllocatorDefault,
                                                   CFAbsoluteTimeGetCurrent(),
                                                   1,
                                                   0,
                                                   0,
                                                   &__DCProxyTimerTick, &timerContext);

    CFRunLoopRef runLoop = CFRunLoopGetCurrent();
    CFRunLoopAddTimer(runLoop, timer, kCFRunLoopCommonModes);

    CFSocketContext socketAcceptContext;
    socketAcceptContext.info = proxy;
    socketAcceptContext.copyDescription = NULL;
    socketAcceptContext.release = NULL;
    socketAcceptContext.retain = NULL;
    socketAcceptContext.version = 0;

    // CREATE SOCKET FOR ACCEPT
    CFSocketRef serverSocket = CFSocketCreate(kCFAllocatorDefault,
                                              PF_INET,
                                              SOCK_STREAM,
                                              IPPROTO_TCP,
                                              kCFSocketAcceptCallBack,
                                              __DCProxyAccept,
                                              &socketAcceptContext);

    // CONFIGURE SOCKET
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
    sin.sin_port = htons(proxy->port);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);

    CFDataRef sincfd = CFDataCreate(kCFAllocatorDefault, (UInt8 *)&sin, sizeof(sin));
    CFSocketSetAddress(serverSocket, sincfd);
    CFRelease(sincfd);

    // ADD SOCKET TO RUNLOOP
    CFRunLoopSourceRef socketSource = CFSocketCreateRunLoopSource(kCFAllocatorDefault, serverSocket, 0);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), socketSource, kCFRunLoopDefaultMode);

    CFRunLoopRun();

    return NULL;
}

bool DCProxyRunServer(DCProxyRef proxy, bool CurrentThread) {
    log_set_level(LOG_TRACE);

    if (CurrentThread) {
        __DCProxyRunServer((void*) proxy);
    } else {
        // Create the thread using POSIX routines.
        pthread_attr_t  attr;
        pthread_t       posixThreadID;
        int             returnVal;

        returnVal = pthread_attr_init(&attr);
        assert(!returnVal);
        returnVal = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        assert(!returnVal);

        int threadError = pthread_create(&posixThreadID, &attr, &__DCProxyRunServer, proxy);

        returnVal = pthread_attr_destroy(&attr);
        assert(!returnVal);
        if (threadError != 0)
        {
            // Report an error.
        }
    }

    return true;
}

void DCProxyStopServer(DCProxyRef proxy) {
    CFRunLoopStop(CFRunLoopGetCurrent());
}

void DCProxyRelease(DCProxyRef proxy) {
    free(proxy);
}
