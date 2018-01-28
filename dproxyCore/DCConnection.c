#include "DCConnection.h"
#include "log.h"
#include "utils.h"

#include <CFNetwork/CFNetwork.h>

#define TRACE(p) log_trace("connection=%p (%s)\n", p, p->talksTo == kDCConnectionTalksToClient ? "CLIENT" : "SERVER")

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

struct __DCConnection {
    DCConnectionTalksTo talksTo;
    DCChannelRef channel;
    CFStreamClientContext streamContext;

    CFReadStreamRef readStream;
    CFMutableArrayRef receivedMessages;
    CFHTTPMessageRef readMessage;
    UInt8 readBuffer[BUFSIZ];

    CFWriteStreamRef writeStream;
    CFHTTPMessageRef activeWriteMessage;
    CFDataRef activeWriteData;
    CFIndex activeWriteIndex;
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
    connection->receivedMessages = CFArrayCreateMutable(kCFAllocatorDefault, 10, &kCFTypeArrayCallBacks);
    connection->sentMessages = CFArrayCreateMutable(kCFAllocatorDefault, 10, &kCFTypeArrayCallBacks);
    connection->outgoingMessages = CFArrayCreateMutable(kCFAllocatorDefault, 10, &kCFTypeArrayCallBacks);
    return connection;
}

void DCConnectionRelease(DCConnectionRef connection) {
    TRACE(connection);
    CFRelease(connection->receivedMessages);
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

inline char* DCConnectionTalksToString(DCConnectionTalksTo talksTo) {
    switch (talksTo) {
        case kDCConnectionTalksToNone: return "kDCConnectionTalksToNone";
        case kDCConnectionTalksToServer: return "kDCConnectionTalksToServer";
        case kDCConnectionTalksToClient: return "kDCConnectionTalksToClient";
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

CFIndex DCConnectionGetNbrReceivedMessages(DCConnectionRef connection) {
    TRACE(connection);
    return CFArrayGetCount(connection->receivedMessages);
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

static CFMutableArrayRef __DCReadConsumeBytesToMessage(DCConnectionRef connection, CFMutableArrayRef received, const UInt8 *buffer, CFIndex bytes) {
    TRACE(connection);
    char *EOM = "\r\n\r\n";
    int messagesCompleted = 0;

    if (bytes == 0) {
        /* EOF - Don't do anything, we'll receive another callback about this */
        return received;
    }

    if (!connection->readMessage)
        connection->readMessage = CFHTTPMessageCreateEmpty(kCFAllocatorDefault, connection->talksTo == kDCConnectionTalksToClient);

    CFIndex bytesLeft = bytes;

    do {
        char *endOfMessage = strnstr((char*) buffer, EOM, bytesLeft);
        if (endOfMessage) {
            CFIndex toConsume = ((const UInt8 *)endOfMessage) - buffer + strlen(EOM);
            CFHTTPMessageAppendBytes(connection->readMessage, buffer, toConsume);
            log_trace("connection=%p message completed => %p\n", connection, connection->readMessage);
            __DCConnectionSendRequestReceivedNotification(connection, connection->readMessage);
            messagesCompleted++;
            CFArrayAppendValue(connection->receivedMessages, connection->readMessage);
            CFArrayAppendValue(received, connection->readMessage);
            connection->readMessage = CFHTTPMessageCreateEmpty(kCFAllocatorDefault, connection->talksTo == kDCConnectionTalksToClient);
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

    return received;
}

static CFMutableArrayRef __DCReadToMessage(DCConnectionRef connection) {
    TRACE(connection);
    CFMutableArrayRef receivedMessages = CFArrayCreateMutable(kCFAllocatorDefault, 4, &kCFTypeArrayCallBacks);
    do {
        memset(connection->readBuffer, 0, sizeof(connection->readBuffer));
        CFIndex bytesLeft = CFReadStreamRead(connection->readStream, connection->readBuffer, sizeof(connection->readBuffer));
        dump_hex("CFReadStreamRead", (void*) connection->readBuffer, (int) bytesLeft);
        __DCReadConsumeBytesToMessage(connection, receivedMessages, connection->readBuffer, bytesLeft);
    } while (CFReadStreamHasBytesAvailable(connection->readStream));
    return receivedMessages;
}

static inline void __DCConnectionReadCallback(CFReadStreamRef stream, CFStreamEventType type, void *info) {
    DCConnectionRef connection = (DCConnectionRef) info;
    log_trace("connection=%p, type => %s\n", connection, __CFStreamEventTypeString(type));

    switch (type) {
        case kCFStreamEventHasBytesAvailable:
            {
                CFMutableArrayRef receivedMessages = __DCReadToMessage(connection);
                if (CFArrayGetCount(receivedMessages) > 0 &&
                    (connection->callbackEvents & kDCConnectionCallbackTypeIncomingMessage) != 0 &&
                    connection->callback != NULL) {
                    connection->callback(connection, kDCConnectionCallbackTypeIncomingMessage, NULL, (void*) receivedMessages, connection->context.info);
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

bool __DCProcessSingleMessage(DCConnectionRef connection) {
    TRACE(connection);

    if (!connection->activeWriteData) {
        connection->activeWriteData = CFHTTPMessageCopySerializedMessage(connection->activeWriteMessage);
        connection->activeWriteIndex = 0;
    }

    const UInt8 *buffer = CFDataGetBytePtr(connection->activeWriteData);
    CFIndex bufferLen = CFDataGetLength(connection->activeWriteData);
    CFIndex bufferLeft = bufferLen - connection->activeWriteIndex;

    CFIndex nbrWritten = CFWriteStreamWrite(connection->writeStream, buffer + connection->activeWriteIndex, bufferLeft);
    connection->activeWriteIndex += nbrWritten;

    if (connection->activeWriteIndex == bufferLen) {
        // Message finished
        CFArrayAppendValue(connection->sentMessages, connection->activeWriteMessage);
        CFRelease(connection->activeWriteMessage);
        CFRelease(connection->activeWriteData);
        connection->activeWriteData = NULL;
        connection->activeWriteMessage = NULL;
        return true;
    }

    return false;
}

static inline bool __DCHasOutgoingMessages(DCConnectionRef connection) {
    return connection->activeWriteMessage || CFArrayGetCount(connection->outgoingMessages) > 0;
}

void __DCProcessOutgoingMessages(DCConnectionRef connection) {
    TRACE(connection);
    bool didSend;

    if (!CFWriteStreamCanAcceptBytes(connection->writeStream)) {
        log_trace("connection=%p, can't write without blocking\n", connection);
        return;
    }

    if (connection->activeWriteMessage) {
        didSend = __DCProcessSingleMessage(connection);

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
        connection->activeWriteMessage = message;
        didSend = __DCProcessSingleMessage(connection);
    } while (CFArrayGetCount(connection->outgoingMessages) > 0 && didSend && CFWriteStreamCanAcceptBytes(connection->writeStream));
}

void DCConnectionAddOutgoing(DCConnectionRef connection, CFHTTPMessageRef outgoingMessage) {
    TRACE(connection);
    CFArrayAppendValue(connection->outgoingMessages, outgoingMessage);
    __DCProcessOutgoingMessages(connection);
}

void DCConnectionSetTalksTo(DCConnectionRef connection, DCConnectionTalksTo talksTo) {
    log_trace("connection=%p, talksTo => %s\n", connection, DCConnectionTalksToString(talksTo));
    connection->talksTo = talksTo;
}

static void __DCConnectionWriteCallback(CFWriteStreamRef stream, CFStreamEventType type, void *info) {
    DCConnectionRef connection = (DCConnectionRef) info;
    log_trace("connection=%p, type => %s\n", connection, __CFStreamEventTypeString(type));

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

