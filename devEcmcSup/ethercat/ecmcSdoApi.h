/* Runtime access to preconfigured async SDO objects. */
#ifndef ECMC_SDO_API_H_
#define ECMC_SDO_API_H_
#include "ecmcSdoErrors.h"
#ifdef __cplusplus
extern "C" {
#endif

/* All calls return 0 on success, otherwise an ecmc error code.
 * objectIndex is a zero-based per-slave object index, NOT an EtherCAT OD index.
 * Use the serialized PLC/C++ logic callback or hold the ecmc asyn port lock.
 * Read/write requests are one-shot calls, not maintained enable inputs.
 */
int ecGetSdoRTObjectIndex(int slaveId, const char *name, int *objectIndex);
int ecSdoRead(int slaveId, int objectIndex);
int ecSdoWrite(int slaveId, int objectIndex);
int ecSdoGetBusy(int slaveId, int objectIndex, int *value);
int ecSdoGetError(int slaveId, int objectIndex, int *value);
/* Size = valid cached upload elements; capacity = download elements.
 * Arrays: elements are bytes. Scalars: one element (element index 0).
 */
int ecSdoGetSize(int slaveId, int objectIndex, int *value);
int ecSdoGetCapacity(int slaveId, int objectIndex, int *value);
int ecSdoGetValue(int slaveId, int objectIndex, int element, double *value);
int ecSdoSetValue(int slaveId, int objectIndex, int element, double value);

#ifdef __cplusplus
}
#endif
#endif
