#include "DCConnection.h"
#include "log.h"
#include "utils.h"

#include <CFNetwork/CFNetwork.h>

#define TRACE(p) log_trace("connection=%p (%s)\n", p, p->type == kDCConnectionTypeClient ? "CLIENT" : "SERVER")

CFStringRef const kDCConnectionReceivedHTTPRequest = CFSTR("DCConnectionReceivedHTTPRequest");

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

struct __DCConnection {
    DCConnectionType type;
    DCChannelRef channel;
    CFStreamClientContext streamContext;

    CFReadStreamRef readStream;
    CFMutableArrayRef recvUnprocessedMessages;
    CFMutableArrayRef recvProcessedMessages;
    CFHTTPMessageRef readMessage;
    UInt8 readBuffer[BUFSIZ];

    // Where we write our requests
    CFWriteStreamRef writeStream;
    __HTTPWriteMessage writeMessage;
    CFMutableArrayRef outgoingMessages;
    CFMutableArrayRef sentMessages;

    DCConnectionContext context;
    DCConnectionCallback callback;
    DCConnectionCallbackEvents callbackEvents;
};

void DCConnectionClose(DCConnectionRef connection) {
    TRACE(connection);
    if (connection->readStream) CFReadStreamSetClient(connection->readStream, kCFStreamEventNone, NULL, NULL);
    if (connection->writeStream) CFWriteStreamSetClient(connection->writeStream, kCFStreamEventNone, NULL, NULL);
    if (connection->readStream) CFReadStreamClose(connection->readStream);
    if (connection->writeStream) CFWriteStreamClose(connection->writeStream);
}

DCConnectionRef DCConnectionCreate(DCChannelRef channel) {
    struct __DCConnection *connection = (struct __DCConnection *) calloc(1, sizeof(struct __DCConnection));
    TRACE(connection);
    connection->recvUnprocessedMessages = CFArrayCreateMutable(kCFAllocatorDefault, 10, &kCFTypeArrayCallBacks);
    connection->recvProcessedMessages = CFArrayCreateMutable(kCFAllocatorDefault, 10, &kCFTypeArrayCallBacks);
    connection->sentMessages = CFArrayCreateMutable(kCFAllocatorDefault, 10, &kCFTypeArrayCallBacks);
    connection->outgoingMessages = CFArrayCreateMutable(kCFAllocatorDefault, 10, &kCFTypeArrayCallBacks);
    return connection;
}

void DCConnectionRelease(DCConnectionRef connection) {
    TRACE(connection);
    if (connection->recvUnprocessedMessages) CFRelease(connection->recvUnprocessedMessages);
    if (connection->recvProcessedMessages) CFRelease(connection->recvProcessedMessages);
    if (connection->sentMessages) CFRelease(connection->sentMessages);
    if (connection->outgoingMessages) CFRelease(connection->outgoingMessages);
    free(connection);
}

static inline char* __CFStreamEventTypeString(CFStreamEventType type) {
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

inline char* DCConnectionTypeString(DCConnectionType type) {
    switch (type) {
        case kDCConnectionTypeClient: return "kDCConnectionTypeClient";
        case kDCConnectionTypeServer: return "kDCConnectionTypeServer";
    }
    return "INVALID";
}

inline char* DCConnectionCallbackTypeString(DCConnectionCallbackEvents type) {
    switch (type)
    {
        case kDCConnectionCallbackTypeNone: return "kDCConnectionCallbackTypeNone";
        case kDCConnectionCallbackTypeAvailable: return "kDCConnectionCallbackTypeAvailable";
        case kDCConnectionCallbackTypeIncomingMessage: return "kDCConnectionCallbackTypeIncomingMessage";
        case kDCConnectionCallbackTypeResolvingHost: return "kDCConnectionCallbackTypeResolvingHost";
        case kDCConnectionCallbackTypeConnectionEOF: return "kDCConnectionCallbackTypeConnectionEOF";
        case kDCConnectionCallbackTypeCompleted: return "kDCConnectionCallbackTypeCompleted";
        case kDCConnectionCallbackTypeFailed: return "kDCConnectionCallbackTypeFailed";
    }
    return "INVALID";
}

bool DCConnectionHasReceived(DCConnectionRef connection) {
    return CFArrayGetCount(connection->recvUnprocessedMessages) > 0;
}

CFHTTPMessageRef DCConnectionGetNextReceived(DCConnectionRef connection) {
    CFHTTPMessageRef nextReceived = (CFHTTPMessageRef) CFArrayGetValueAtIndex(connection->recvUnprocessedMessages, 0);
    CFArrayRemoveValueAtIndex(connection->recvUnprocessedMessages, 0);
    CFArrayAppendValue(connection->recvProcessedMessages, nextReceived);
    return nextReceived;
}

static void __DCConnectionSendRequestReceivedNotification(DCConnectionRef connection, CFHTTPMessageRef message) {
    TRACE(connection);
    CFStringRef keys[1] = { CFSTR("request") };
    CFHTTPMessageRef values[1] = { message };
    CFDictionaryRef dict = CFDictionaryCreate(kCFAllocatorDefault, (void*) keys, (void*) values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFNotificationCenterPostNotification(CFNotificationCenterGetLocalCenter(), kDCConnectionReceivedHTTPRequest, NULL, dict, false);
}

void DCConnectionSetClient(DCConnectionRef connection, DCConnectionCallbackEvents events, DCConnectionCallback clientCB, DCConnectionContext *clientContext) {
    TRACE(connection);
    connection->callbackEvents = events;
    connection->callback = clientCB;
    memcpy(&(connection->context), clientContext, sizeof(DCConnectionContext));
}

static int __DCReadConsumeBytesToMessage(DCConnectionRef connection, const UInt8 *buffer, CFIndex bytes) {
    TRACE(connection);
    char *EOM = "\r\n\r\n";

    int nbrMessagesCompleted = 0;

    if (bytes == 0) {
        /* EOF - Don't do anything, we'll receive another callback about this */
        return nbrMessagesCompleted;
    }

    if (!connection->readMessage)
        connection->readMessage = CFHTTPMessageCreateEmpty(kCFAllocatorDefault, connection->type == kDCConnectionTypeClient);

    CFIndex bytesLeft = bytes;

    do {
        char *endOfMessage = strnstr((char*) buffer, EOM, bytesLeft);
        if (endOfMessage) {
            CFIndex toConsume = ((const UInt8 *)endOfMessage) - buffer + strlen(EOM);
            CFHTTPMessageAppendBytes(connection->readMessage, buffer, toConsume);
            log_trace("connection=%p message recv => %p\n", connection, connection->readMessage);
            __DCConnectionSendRequestReceivedNotification(connection, connection->readMessage);
            nbrMessagesCompleted++;
            CFArrayAppendValue(connection->recvUnprocessedMessages, connection->readMessage);
            connection->readMessage = CFHTTPMessageCreateEmpty(kCFAllocatorDefault, connection->type == kDCConnectionTypeClient);
            buffer = (const UInt8*) (endOfMessage + strlen(EOM));
            bytesLeft -= toConsume;
            log_trace("connection=%p, toConsume=%d, bytesLeft=%d\n", connection, toConsume, bytesLeft);
        } else {
            // There's no end of an HTTP-request in the rest of our buffer,
            // thus we append the whole buffer to our message and we'll continue
            // to search for a message end in next read.

            CFHTTPMessageAppendBytes(connection->readMessage, buffer, bytesLeft);
            log_trace("connection=%p, bytesLeft=%d\n", connection, bytesLeft);
            bytesLeft = 0;
        }
    } while (bytesLeft > 0);

    return nbrMessagesCompleted;
}

static int __DCReadToMessage(DCConnectionRef connection) {
    TRACE(connection);
    int nbrMessagesCompleted = 0;
    do {
        memset(connection->readBuffer, 0, sizeof(connection->readBuffer));
        CFIndex bytesLeft = CFReadStreamRead(connection->readStream, connection->readBuffer, sizeof(connection->readBuffer));
        dump_hex("CFReadStreamRead", (void*) connection->readBuffer, (int) bytesLeft);
        nbrMessagesCompleted += __DCReadConsumeBytesToMessage(connection, connection->readBuffer, bytesLeft);
    } while (CFReadStreamHasBytesAvailable(connection->readStream));
    return nbrMessagesCompleted;
}

static inline void __DCConnectionReadCallback(CFReadStreamRef stream, CFStreamEventType type, void *info) {
    DCConnectionRef connection = (DCConnectionRef) info;
    log_trace("connection=%p, event => %s\n", connection, __CFStreamEventTypeString(type));

    switch (type) {
        case kCFStreamEventHasBytesAvailable:
            {
                int nbrMessagesCompleted = __DCReadToMessage(connection);
                if (nbrMessagesCompleted > 0 &&
                    (connection->callbackEvents & kDCConnectionCallbackTypeIncomingMessage) != 0 &&
                    connection->callback != NULL) {
                    connection->callback(connection, kDCConnectionCallbackTypeIncomingMessage, NULL, NULL, connection->context.info);
                }
            }
            break;
        case kCFStreamEventErrorOccurred:
            break;
        case kCFStreamEventEndEncountered:
            if ((connection->callbackEvents & kDCConnectionCallbackTypeConnectionEOF) != 0 &&
                connection->callback != NULL)
                connection->callback(connection, kDCConnectionCallbackTypeConnectionEOF, NULL, NULL, connection->context.info);
            break;
        case kCFStreamEventOpenCompleted:
            break;
    }
}

bool __DCProcessSingleMessage(DCConnectionRef connection, CFHTTPMessageRef message) {
    TRACE(connection);

    if (!connection->writeMessage.msg) {
        log_trace("connection=%p, no active message, settings %p to active\n", connection, message);

        connection->writeMessage.msg = message;
        connection->writeMessage.idx = 0;
        connection->writeMessage.data = CFHTTPMessageCopySerializedMessage(message);
    }

    const UInt8 *buffer = CFDataGetBytePtr(connection->writeMessage.data);
    CFIndex bufferLen = CFDataGetLength(connection->writeMessage.data);
    CFIndex bufferLeft = bufferLen - connection->writeMessage.idx;

    CFIndex nbrWritten = CFWriteStreamWrite(connection->writeStream, buffer + connection->writeMessage.idx, bufferLeft);
    connection->writeMessage.idx += nbrWritten;

    if (connection->writeMessage.idx == bufferLen) {
        // Message finished
        CFArrayAppendValue(connection->sentMessages, connection->writeMessage.msg);
        CFRelease(connection->writeMessage.msg);
        CFRelease(connection->writeMessage.data);
        connection->writeMessage.data = NULL;
        memset(&(connection->writeMessage), 0, sizeof(__HTTPWriteMessage));
        return true;
    }

    return false;
}

static inline bool __DCHasOutgoingMessages(DCConnectionRef connection) {
    return connection->writeMessage.msg || CFArrayGetCount(connection->outgoingMessages) > 0;
}

void __DCProcessOutgoingMessages(DCConnectionRef connection) {
    TRACE(connection);
    bool didSend;

    if (!CFWriteStreamCanAcceptBytes(connection->writeStream)) {
        log_trace("connection=%p, can't write without blocking\n", connection);
        return;
    }

    if (connection->writeMessage.msg) {
        didSend = __DCProcessSingleMessage(connection, connection->writeMessage.msg);

        if (!didSend) {
            log_trace("connection=%p, first active wasn't fully processed, won't continue process more\n" ,connection);
            return;
        }
    }

    CFIndex nbrOutgoing = CFArrayGetCount(connection->outgoingMessages);

    if (!nbrOutgoing) {
        log_trace("connection=%p, no messages to process\n");
        return;
    }

    if (nbrOutgoing == 0 || !CFWriteStreamCanAcceptBytes(connection->writeStream))
        return;

    do {
        CFHTTPMessageRef message = (CFHTTPMessageRef) CFArrayGetValueAtIndex(connection->outgoingMessages, 0);
        CFArrayRemoveValueAtIndex(connection->outgoingMessages, 0);
        didSend = __DCProcessSingleMessage(connection, message);
    } while (CFArrayGetCount(connection->outgoingMessages) > 0 && didSend && CFWriteStreamCanAcceptBytes(connection->writeStream));
}

void DCConnectionAddOutgoing(DCConnectionRef connection, CFHTTPMessageRef outgoingMessage) {
    TRACE(connection);
    CFArrayAppendValue(connection->outgoingMessages, outgoingMessage);
    __DCProcessOutgoingMessages(connection);
}

void DCConnectionSetTalksTo(DCConnectionRef connection, DCConnectionType type) {
    log_trace("connection=%p, type => %s\n", connection, DCConnectionTypeString(type));
    connection->type = type;
}

static void __DCConnectionWriteCallback(CFWriteStreamRef stream, CFStreamEventType type, void *info) {
    DCConnectionRef connection = (DCConnectionRef) info;
    log_trace("connection=%p, event => %s\n", connection, __CFStreamEventTypeString(type));

    switch (type) {
        case kCFStreamEventCanAcceptBytes:
            {
                if (__DCHasOutgoingMessages(connection))
                    __DCProcessOutgoingMessages(connection);
            }
            break;
        case kCFStreamEventErrorOccurred:
            break;
        case kCFStreamEventEndEncountered:
            break;
        case kCFStreamEventOpenCompleted:
            break;
    }
}

void __DCFinishSetup(DCConnectionRef connection) {
    TRACE(connection);

    if (connection->readStream) {
        CFReadStreamSetProperty(connection->readStream, kCFStreamPropertyShouldCloseNativeSocket, kCFBooleanTrue);
    }

    if (connection->writeStream) {
        CFWriteStreamSetProperty(connection->writeStream, kCFStreamPropertyShouldCloseNativeSocket, kCFBooleanTrue);
    }

    connection->streamContext.version = 0;
    connection->streamContext.info = (void *)(connection);
    connection->streamContext.retain = nil;
    connection->streamContext.release = nil;
    connection->streamContext.copyDescription = nil;

    CFWriteStreamSetClient(connection->writeStream,
                           kCFStreamEventOpenCompleted     |
                           kCFStreamEventErrorOccurred     |
                           kCFStreamEventEndEncountered    |
                           kCFStreamEventCanAcceptBytes,
                           &__DCConnectionWriteCallback,
                           &connection->streamContext);

    CFWriteStreamScheduleWithRunLoop(connection->writeStream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    CFWriteStreamOpen(connection->writeStream);

    CFReadStreamSetClient(connection->readStream,
                          kCFStreamEventOpenCompleted      |
                          kCFStreamEventErrorOccurred      |
                          kCFStreamEventEndEncountered     |
                          kCFStreamEventHasBytesAvailable,
                          &__DCConnectionReadCallback,
                          &connection->streamContext);

    CFReadStreamScheduleWithRunLoop(connection->readStream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    CFReadStreamOpen(connection->readStream);
}

void DCConnectionSetupWithHost(DCConnectionRef connection, CFHostRef host, UInt32 port) {
    TRACE(connection);
    CFStreamCreatePairWithSocketToCFHost(kCFAllocatorDefault, host, port, &connection->readStream, &connection->writeStream);
    __DCFinishSetup(connection);
}

void DCConnectionSetupWithFD(DCConnectionRef connection, CFSocketNativeHandle fd) {
    TRACE(connection);
    CFStreamCreatePairWithSocket(kCFAllocatorDefault, fd, &connection->readStream, &connection->writeStream);
    __DCFinishSetup(connection);
}

