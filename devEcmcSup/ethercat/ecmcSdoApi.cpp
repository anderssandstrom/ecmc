#include "ecmcSdoApi.h"
#include "ecmcEc.h"
#include "ecmcEcSlave.h"
#include "ecmcEcAsyncSDO.h"

extern ecmcEc *ec;

namespace {
ecmcEcSlave *sdoSlave(int slaveId, int *error) {
  *error = 0;
  if (!ec) { *error = ERROR_MAIN_EC_MASTER_NULL; return nullptr; }
  if (slaveId < 0 || slaveId > 0xffff) {
    *error = ERROR_EC_SDO_ASYNC_VALUE; return nullptr;
  }
  auto *slave = ec->findSlave(slaveId);
  if (!slave) *error = ERROR_MAIN_EC_SLAVE_NULL;
  return slave;
}
ecmcEcAsyncSDO *sdoObject(int slaveId, int index, int *error) {
  auto *slave = sdoSlave(slaveId, error);
  if (!slave) return nullptr;
  auto *sdo = slave->getSDOAsync(index);
  if (!sdo) *error = ERROR_EC_SDO_ASYNC_NOT_FOUND;
  return sdo;
}
}

int ecGetSdoRTObjectIndex(int slaveId, const char *name, int *index) {
  if (!index) return ERROR_EC_SDO_ASYNC_VALUE;
  *index = -1;
  int error = 0;
  auto *slave = sdoSlave(slaveId, &error);
  return slave ? slave->findSDOAsyncIndex(name, index) : error;
}

int ecSdoRead(int slaveId, int index) {
  int error = 0;
  auto *sdo = sdoObject(slaveId, index, &error);
  return sdo ? sdo->requestRead() : error;
}

int ecSdoWrite(int slaveId, int index) {
  int error = 0;
  auto *sdo = sdoObject(slaveId, index, &error);
  return sdo ? sdo->requestWrite() : error;
}

int ecSdoGetBusy(int slaveId, int index, int *value) {
  if (!value) return ERROR_EC_SDO_ASYNC_VALUE;
  int error = 0;
  auto *sdo = sdoObject(slaveId, index, &error);
  if (sdo) *value = sdo->getBusy();
  return error;
}

int ecSdoGetError(int slaveId, int index, int *value) {
  if (!value) return ERROR_EC_SDO_ASYNC_VALUE;
  int error = 0;
  auto *sdo = sdoObject(slaveId, index, &error);
  if (sdo) *value = sdo->getSdoError();
  return error;
}

int ecSdoGetSize(int slaveId, int index, int *value) {
  if (!value) return ERROR_EC_SDO_ASYNC_VALUE;
  int error = 0;
  auto *sdo = sdoObject(slaveId, index, &error);
  if (sdo) *value = sdo->getSize();
  return error;
}

int ecSdoGetCapacity(int slaveId, int index, int *value) {
  if (!value) return ERROR_EC_SDO_ASYNC_VALUE;
  int error = 0;
  auto *sdo = sdoObject(slaveId, index, &error);
  if (sdo) *value = sdo->getCapacity();
  return error;
}

int ecSdoGetValue(int slaveId, int index, int element, double *value) {
  int error = 0;
  auto *sdo = sdoObject(slaveId, index, &error);
  return sdo ? sdo->getValue(element, value) : error;
}

int ecSdoSetValue(int slaveId, int index, int element, double value) {
  int error = 0;
  auto *sdo = sdoObject(slaveId, index, &error);
  return sdo ? sdo->setValue(element, value) : error;
}
