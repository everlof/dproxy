#ifndef DCChannel_h
#define DCChannel_h

#include <stdio.h>
#include <CoreFoundation/CoreFoundation.h>

typedef struct __DCChannel*         DCChannelRef;

DCChannelRef DCChannelCreate(void);
void DCChannelSetupWithFD(DCChannelRef channel, CFSocketNativeHandle fd);
void DCChannelRelease(DCChannelRef channel);

#endif /* DCChannel_h */
