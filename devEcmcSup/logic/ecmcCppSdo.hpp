/* C++ logic wrappers for the existing asynchronous SDO objects.
 * Include alongside ecmcCppLogic.hpp; requires the ecmc runtime symbols.
 */
#ifndef ECMC_CPP_SDO_HPP_
#define ECMC_CPP_SDO_HPP_
#include "ecmcSdoApi.h"

namespace ecmcCpp {
struct SdoRef {
  int slave {0};
  int objectIndex {-1}; // Per-slave async object index, not the EtherCAT OD index.
};

// Resolve once after SDO configuration, for example during logic construction.
inline int sdoFindRTObjectIndex(int slave, const char *name, int& objectIndex) {
  return ecGetSdoRTObjectIndex(slave, name, &objectIndex);
}
inline int sdoFind(int slave, const char *name, SdoRef& sdo) {
  sdo.slave = slave;
  return sdoFindRTObjectIndex(slave, name, sdo.objectIndex);
}
// One call submits one request. Do not call every cycle unless polling is intended.
inline int sdoRead(SdoRef sdo) { return ecSdoRead(sdo.slave, sdo.objectIndex); }
inline int sdoWrite(SdoRef sdo) { return ecSdoWrite(sdo.slave, sdo.objectIndex); }
inline int sdoGetBusy(SdoRef sdo, int& value) {
  return ecSdoGetBusy(sdo.slave, sdo.objectIndex, &value);
}
inline int sdoGetError(SdoRef sdo, int& value) {
  return ecSdoGetError(sdo.slave, sdo.objectIndex, &value);
}
inline int sdoGetSize(SdoRef sdo, int& value) {
  return ecSdoGetSize(sdo.slave, sdo.objectIndex, &value);
}
inline int sdoGetCapacity(SdoRef sdo, int& value) {
  return ecSdoGetCapacity(sdo.slave, sdo.objectIndex, &value);
}
// Array elements are unsigned bytes; scalar objects use element 0.
// Get reads cached upload data; Set only stages the value for a later Write.
inline int sdoGetValue(SdoRef sdo, int element, double& value) {
  return ecSdoGetValue(sdo.slave, sdo.objectIndex, element, &value);
}
inline int sdoSetValue(SdoRef sdo, int element, double value) {
  return ecSdoSetValue(sdo.slave, sdo.objectIndex, element, value);
}
} // namespace ecmcCpp
#endif
