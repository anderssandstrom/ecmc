// Offline logging boundary. Only used by the standalone target's include path.
#pragma once
#include <stdarg.h>
#include <stdio.h>
typedef struct asynUser { int unused; } asynUser;
enum asynStatus { asynSuccess, asynError };
typedef double epicsFloat64;
#define ASYN_TRACE_ERROR 1
#define ASYN_TRACE_INFO 64
static inline int asynPrint(asynUser *, int, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = vfprintf(stderr, format, args);
  va_end(args);
  return result;
}
