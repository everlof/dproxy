#include "proxy.h"
#include "log.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CFNetwork/CFNetwork.h>

#include <stdio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

// https://github.com/CollinStuart/CFSocketExample/blob/master/Socket/AppDelegate.mm

CFRunLoopSourceRef gSocketSource = NULL;

//CFIndex length = CFStringGetLength(hostname);
//CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
//char *buffer = (char *)malloc(maxSize);
//if (CFStringGetCString(hostname, buffer, maxSize, kCFStringEncodingUTF8)) {
//    printf("Hostname: %s\n", buffer);
//}
//free(buffer); // If we failed

void myCFTimerCallback(CFRunLoopTimerRef timer, void *info) {
    // log_debug("CALLBACK: %s\n", (char*)info);
}

void SocketCallBack(CFSocketRef socket, CFSocketCallBackType type, CFDataRef address, const void *data, void *info)
{
    if (type == kCFSocketConnectCallBack)
    {
        log_debug("connected\n");
    }
    else if (type == kCFSocketDataCallBack)
    {
        log_debug("buffer has read data\n");
    }
    else if (type == kCFSocketWriteCallBack)
    {
        //printf("Buffer Writable\n");
        //
        //const char *sendChar = "what up\n";
        //
        //printf("Writing %s\n", sendChar);
        //CFDataRef sendData = CFDataCreate(kCFAllocatorDefault, (const UInt8 *)sendChar, strlen(sendChar));
        //CFSocketSendData(socket, address, sendData, 30);
        //CFRelease(sendData);
    }
}

void ResolutionCallBackFunction(CFHostRef host, CFHostInfoType typeInfo, const CFStreamError *error, void *info)
{
    /*
    Boolean hostsResolved;
    CFArrayRef addressesArray = CFHostGetAddressing(host, &hostsResolved);

    CFSocketContext context = {0, (void *)info, NULL, NULL, NULL};
    CFSocketRef theSocket =
        CFSocketCreate(kCFAllocatorDefault,
                       PF_INET,
                       SOCK_STREAM,
                       IPPROTO_TCP,
                       kCFSocketConnectCallBack |
                       kCFSocketDataCallBack |
                       kCFSocketWriteCallBack,
                       (CFSocketCallBack)SocketCallBack,
                       &context);

    CFSocketSetSocketFlags(theSocket, kCFSocketCloseOnInvalidate);


    if (addressesArray && CFArrayGetCount(addressesArray))
    {
        CFDataRef socketData = (CFDataRef)CFArrayGetValueAtIndex(addressesArray, 0);

        //Here our connection only accepts port 25 - so we must explicitly change this
        //first, we copy the data so we can change it
        CFDataRef socketDataCopy = CFDataCreateCopy(kCFAllocatorDefault, socketData);
        struct sockaddr_in *addressStruct = (struct sockaddr_in *)CFDataGetBytePtr(socketDataCopy);
        addressStruct->sin_port = htons(2500);

        //connect
        CFSocketError socketError = CFSocketConnectToAddress(theSocket, socketDataCopy, 30);
        CFRelease(socketDataCopy);
        if (socketError != kCFSocketSuccess)
        {
            log_warn("Error sending login fail to socket connection\n");
        }
    }
    if (host)
    {
        CFRelease(host);
    }

    gSocketSource = CFSocketCreateRunLoopSource(kCFAllocatorDefault, theSocket, 0);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), gSocketSource, kCFRunLoopDefaultMode);*/
}

int main(int argc, const char * argv[]) {

    // lsof -n -i | grep -e LISTEN

    char *myText = "Hello dear!";

    CFRunLoopTimerContext context = {0, myText, NULL, NULL, NULL};
    CFRunLoopTimerRef timer = CFRunLoopTimerCreate(kCFAllocatorDefault,
                                                   CFAbsoluteTimeGetCurrent() + 10,
                                                   60.0,
                                                   0,
                                                   0,
                                                   &myCFTimerCallback, &context);

    CFRunLoopRef runLoop = CFRunLoopGetCurrent();
    CFRunLoopAddTimer(runLoop, timer, kCFRunLoopCommonModes);

    // RESOLVE HOST
    CFHostRef host = CFHostCreateWithName(kCFAllocatorDefault, CFSTR("127.0.0.1"));
    CFStreamError streamError;
    CFHostClientContext hostContext = {0, NULL, NULL, NULL, NULL};
    CFHostSetClient(host, ResolutionCallBackFunction, &hostContext);
    CFHostScheduleWithRunLoop(host, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    Boolean started = CFHostStartInfoResolution(host, kCFHostAddresses, &streamError);
    if (!started)
    {
        printf("Could not start info resolution\n");
    }

    proxy_start(49101);

    CFRunLoopRun();
    return 0;
}
