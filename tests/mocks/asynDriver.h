#ifndef ASYN_DRIVER_STUB_H_
#define ASYN_DRIVER_STUB_H_

#include <stdint.h>

// Only define types if not provided by EcmcTestStubs
#ifndef ECMC_TEST_STUBS
typedef int asynStatus;
enum {
  asynSuccess = 0,
  asynError   = -1,
};

typedef int8_t  epicsInt8;
typedef uint8_t epicsUInt8;
typedef int16_t epicsInt16;
typedef uint16_t epicsUInt16;
typedef int32_t epicsInt32;
typedef uint32_t epicsUInt32;
typedef int64_t epicsInt64;
typedef uint64_t epicsUInt64;
typedef float   epicsFloat32;
typedef double  epicsFloat64;
#endif

#ifndef ASYN_PARAM_TYPE_DEFINED
#define ASYN_PARAM_TYPE_DEFINED
typedef enum asynParamType {
  asynParamNotDefined = -1,
  asynParamInt32 = 0,
  asynParamUInt32Digital,
  asynParamFloat64,
  asynParamOctet,
  asynParamInt8Array,
  asynParamInt16Array,
  asynParamInt32Array,
  asynParamFloat32Array,
  asynParamFloat64Array,
  asynParamGenericPointer,
  asynParamInt64,
  asynParamInt64Array
} asynParamType;
#endif

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

#endif  /* ASYN_DRIVER_STUB_H_ */
