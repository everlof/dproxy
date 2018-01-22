#include "proxy.h"
#include "log.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CFNetwork/CFNetwork.h>

#include <stdio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

// https://github.com/CollinStuart/CFSocketExample/blob/master/Socket/AppDelegate.mm
// https://developer.apple.com/library/content/samplecode/MiniSOAP/Listings/HTTPServer_m.html#//apple_ref/doc/uid/DTS40009323-HTTPServer_m-DontLinkElementID_4
// https://github.com/robbiehanson/CocoaAsyncSocket/blob/d0adf58ca694e733c75a8a157635e3deb66c061e/Source/GCD/GCDAsyncSocket.m
// lsof -n -i | grep -e LISTEN

void myCFTimerCallback(CFRunLoopTimerRef timer, void *info) {
    // log_debug("CALLBACK: %s\n", (char*)info);
}

int main(int argc, const char * argv[]) {

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
    log_set_level(LOG_TRACE);
    proxy_start(1080);
    CFRunLoopRun();

    return 0;
}
