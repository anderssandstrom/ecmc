/*************************************************************************\
* Minimal stub of EPICS asynPortDriver interfaces used by unit tests.
\*************************************************************************/

#ifndef ASYN_PORT_DRIVER_STUB_H_
#define ASYN_PORT_DRIVER_STUB_H_

#include <stdint.h>

#define ASYN_VERSION 4
#define ASYN_REVISION 37
#define ASYN_MODIFICATION 0

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

class asynPortDriver {};

#endif  /* ASYN_PORT_DRIVER_STUB_H_ */
