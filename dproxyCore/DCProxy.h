#ifndef DCProxy_h
#define DCProxy_h

#include <stdio.h>
#include <CoreFoundation/CoreFoundation.h>

typedef struct __DCProxy*         DCProxyRef;

DCProxyRef DCProxyCreate(unsigned int port);
void DCProxyRelease(DCProxyRef proxy);

bool DCProxyRunServer(DCProxyRef proxy, bool CurrentThread);
void DCProxyStopServer(DCProxyRef proxy);

#endif /* DCProxy_h */
