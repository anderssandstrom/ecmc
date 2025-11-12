#ifndef ASYN_DRIVER_STUB_H_
#define ASYN_DRIVER_STUB_H_

#include <cstdarg>

typedef struct asynUser {
  int dummy;
} asynUser;

static inline int asynPrint(asynUser *,
                            int,
                            const char *,
                            ...) {
  return 0;
}

#define ASYN_TRACE_ERROR 0x0001
#define ASYN_TRACE_INFO  0x0002

#endif  // ASYN_DRIVER_STUB_H_
