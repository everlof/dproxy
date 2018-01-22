//
//  utils.h
//  dproxy
//
//  Created by David Everlöf on 2018-01-20.
//  Copyright © 2018 David Everlöf. All rights reserved.
//

#ifndef utils_h
#define utils_h

#include <CoreFoundation/CoreFoundation.h>

char* CFStreamEventTypeString(CFStreamEventType);

void dump_hex(char *desc, void *addr, int len);

#endif /* utils_h */
