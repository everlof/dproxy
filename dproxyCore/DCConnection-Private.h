#ifndef DCConnection_Private_h
#define DCConnection_Private_h

#include "DCConnection.h"

typedef enum __DCConnectionState {
    kDCConnectionStateNone = 0,
    kDCConnectionStateAvailable = 1,
    kDCConnectionStateReading,
    kDCConnectionStateResolvingHost,
    kDCConnectionStateSending,
    kDCConnectionStateCompleted,
    kDCConnectionStateFailed
} __DCConnectionState;

typedef struct __HTTPWriteMessage {
    CFHTTPMessageRef msg;
    CFDataRef data;
    CFIndex idx;
} __HTTPWriteMessage;

typedef enum __HTTPReadMessageState {
    kHTTPReadMessageStateHeader = 0,
    kHTTPReadMessageStateBody = 1
} __HTTPReadMessageState;

typedef struct __HTTPReadMessage {
    CFHTTPMessageRef msg;
    __HTTPReadMessageState state;
    SInt32 bodyLength;
    CFIndex idx;
    SInt32 eofLeft;
} __HTTPReadMessage;

struct __DCConnection {
    CFSocketNativeHandle fd;
    DCConnectionType type;
    DCChannelRef channel;
    CFStreamClientContext streamContext;

    CFReadStreamRef readStream;
    __HTTPReadMessage readMessage;
    UInt8 readBuffer[4*BUFSIZ];
    CFMutableArrayRef recvUnprocessedMessages;
    CFMutableArrayRef recvProcessedMessages;

    // Where we write our requests
    CFWriteStreamRef writeStream;
    __HTTPWriteMessage writeMessage;
    CFMutableArrayRef outgoingMessages;
    CFMutableArrayRef sentMessages;

    DCConnectionContext context;
    DCConnectionCallback callback;
    DCConnectionCallbackEvents callbackEvents;
};


#endif /* DCConnection_Private_h */
