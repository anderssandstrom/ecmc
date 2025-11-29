/*************************************************************************\
* Minimal epicsTime stub for unit tests.
\*************************************************************************/

#ifndef EPICS_TIME_STUB_H_
#define EPICS_TIME_STUB_H_

#include <stdint.h>

typedef uint32_t epicsUInt32;

typedef struct epicsTimeStamp {
  epicsUInt32 secPastEpoch;
  epicsUInt32 nsec;
} epicsTimeStamp;

#endif  /* EPICS_TIME_STUB_H_ */
