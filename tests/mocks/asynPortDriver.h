/*************************************************************************\
* Minimal stub of EPICS asynPortDriver interfaces used by unit tests.
\*************************************************************************/

#ifndef ASYN_PORT_DRIVER_STUB_H_
#define ASYN_PORT_DRIVER_STUB_H_

#include <stdint.h>
#include "asynDriver.h"

#define ASYN_VERSION 4
#define ASYN_REVISION 37
#define ASYN_MODIFICATION 0

#if !defined(ECMC_TEST_STUBS) && !defined(ASYN_PARAM_TYPE_DEFINED)
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

class asynPortDriver {
public:
  asynPortDriver(const char * = nullptr,
                 int         = 0,
                 int         = 0,
                 int         = 0,
                 int         = 0) {}
};

#endif  /* ASYN_PORT_DRIVER_STUB_H_ */
