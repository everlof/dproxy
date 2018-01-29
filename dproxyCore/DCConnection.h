#ifndef DCConnection_h
#define DCConnection_h

#include "DCChannel.h"

#include <stdio.h>

#include <CoreFoundation/CoreFoundation.h>
#include <CFNetwork/CFNetwork.h>

typedef struct __DCConnection*         DCConnectionRef;

typedef struct {
    void *info;
} DCConnectionContext;

extern CFStringRef const kDCConnectionReceivedHTTPRequest;

typedef enum DCConnectionCallbackEvents {
    kDCConnectionCallbackTypeNone = 0,
    kDCConnectionCallbackTypeAvailable = 1,
    kDCConnectionCallbackTypeIncomingMessage = 2,
    kDCConnectionCallbackTypeResolvingHost = 4,
    kDCConnectionCallbackTypeConnectionEOF = 8,
    kDCConnectionCallbackTypeCompleted = 16,
    kDCConnectionCallbackTypeFailed = 32
} DCConnectionCallbackEvents;

typedef enum DCConnectionType {
    kDCConnectionTypeServer = 0,
    kDCConnectionTypeClient = 1
} DCConnectionType;

typedef void (*DCConnectionCallback)(DCConnectionRef connection, DCConnectionCallbackEvents type, CFDataRef address, const void *data, void *info);

DCConnectionRef DCConnectionCreate(DCChannelRef channel);
void DCConnectionRelease(DCConnectionRef connection);
void DCConnectionClose(DCConnectionRef connection);

void DCConnectionSetupWithFD(DCConnectionRef connection, CFSocketNativeHandle fd);
void DCConnectionSetupWithHost(DCConnectionRef connection, CFHostRef host, UInt32 port);

void DCConnectionSetTalksTo(DCConnectionRef connection, DCConnectionType type);
void DCConnectionSetClient(DCConnectionRef connection, DCConnectionCallbackEvents events, DCConnectionCallback clientCB, DCConnectionContext *clientContext);

void DCConnectionAddOutgoing(DCConnectionRef connection, CFHTTPMessageRef outgoingMessage);

bool DCConnectionHasReceived(DCConnectionRef connection);
CFHTTPMessageRef DCConnectionGetNextReceived(DCConnectionRef connection);

char* DCConnectionCallbackTypeString(DCConnectionCallbackEvents type);
char* DCConnectionTypeString(DCConnectionType type);

#endif /* DCConnection_h */
