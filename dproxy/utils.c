//
//  utils.c
//  dproxy
//
//  Created by David Everlöf on 2018-01-20.
//  Copyright © 2018 David Everlöf. All rights reserved.
//

#include "utils.h"

char* CFStreamEventTypeString(CFStreamEventType type) {
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
