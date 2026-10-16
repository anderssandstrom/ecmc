/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcEcAsyncSDO.h
*
*  Created on: Dec 15, 2015
*      Author: anderssandstrom
*
\*************************************************************************/

#ifndef ECMCECASYNCSDO_H_
#define ECMCECASYNCSDO_H_
#include <string.h>
#include <vector>
#include "stdio.h"
#include "ecrt.h"
#include "ecmcDefinitions.h"
#include "ecmcError.h"
#include "ecmcAsynPortDriver.h"
#include "ecmcErrorsList.h"

#define ERROR_EC_SDO_ASYNC_BUSY 0x23500
#define ERROR_EC_SDO_ASYNC_ERROR 0x23501
#define ERROR_EC_SDO_ASYNC_OBJ_NULL 0x23502
#define ERROR_EC_SDO_ASYNC_ASYN_OBJ_FAIL 0x23503

#define ERROR_EC_SDO_ASYNC_SIZE 0x23504
#define ERROR_EC_SDO_ASYNC_NOT_FOUND 0x23505
#define ERROR_EC_SDO_ASYNC_VALUE 0x23506
#define ERROR_EC_SDO_ASYNC_NAME_AMBIGUOUS 0x23507
#define ECMC_SDO_ASYNC_MAX_BYTES (1024 * 1024)

#define DEFAULT_SDO_ASYNC_TIMOUT_MS 2000

class ecmcEcAsyncSDO : public ecmcError {
public:
  ecmcEcAsyncSDO(ecmcAsynPortDriver *asynDriver,
                 int                 masterId,
                 int                 slaveId,
                 ec_slave_config_t  *sc, /**< Slave configuration. */
                 uint16_t            sdoIndex, /**< SDO index. */
                 uint8_t             sdoSubIndex, /**< SDO subindex. */
                 ecmcEcDataType      dt,
                 std::string         alias,
                 size_t              arrayBytes = 0);
  ~ecmcEcAsyncSDO();

  asynStatus asynWriteSDO(void         *data,
                          size_t        bytes,
                          asynParamType asynParType);
  asynStatus asynReadSDO(void         *data,
                         size_t        bytes,
                         asynParamType asynParType);

  asynStatus asynWriteArray(void *data, size_t bytes, asynParamType type);
  // Call from the serialized PLC/C++ logic cycle or under the asyn port lock.
  int requestRead();
  int requestWrite();
  const std::string& getName() const { return idString_; }
  int getBusy() const { return busy_; }
  int getSdoError() const { return sdoError_; }
  int getSize() const { return arrayReadBuffer_.empty() ? 1 : arrayValidBytes_; }
  int getCapacity() const { return arrayReadBuffer_.empty() ? 1 : size_; }
  int getValue(int element, double *value) const;
  int setValue(int element, double value);
  int execute();

private:
  int initAsyn();
  int writeValue();
  int readValue();

  ec_sdo_request_t *sdoreq_;
  ec_sdo_request_t *readreq_;
  std::vector<uint8_t> arrayReadBuffer_;
  std::vector<uint8_t> arrayWriteBuffer_;
  bool arrayWriteReady_;
  std::vector<uint8_t> arrayWriteInitialized_;
  size_t arrayInitializedCount_;
  int arrayValidBytes_;
  int32_t arrayReadBytes_;
  ecmcAsynDataItem *asynParamReadBytes_;
  ecmcAsynDataItem *asynParamWriteValue_;
  ec_slave_config_t *sc_;  /**< Slave configuration. */
  uint16_t index_;            /**< SDO index. */
  uint8_t subindex_;             /**< SDO subindex. */
  ecmcAsynPortDriver *asynPortDriver_;
  ecmcAsynDataItem *asynParamWrite_;
  ecmcAsynDataItem *asynParamRead_;
  ecmcAsynDataItem *asynParamValue_;
  ecmcAsynDataItem *asynParamError_;
  ecmcAsynDataItem *asynParamBusy_;
  ecmcEcDataType dt_;
  size_t size_;
  int masterId_;
  int slaveId_;
  uint64_t buffer_;
  int usedSizeBytes_;
  int bitLength_;
  int sdoError_;
  int dummyReadCmd_;
  int dummyWriteCmd_;
  int busy_;
  int8_t *int8Ptr_;
  uint8_t *uint8Ptr_;
  int16_t *int16Ptr_;
  uint16_t *uint16Ptr_;
  int32_t *int32Ptr_;
  uint32_t *uint32Ptr_;
  int64_t *int64Ptr_;
  uint64_t *uint64Ptr_;
  float *float32Ptr_;
  double *float64Ptr_;
  int writeCmdInProcess_;
  int readCmdInProcess_;
  std::string idString_;
  char *idStringChar_;
  ec_request_state_t stateOld_;
  int readTrigg_;
  int writeTrigg_;
};
#endif  /* ECMCECASYNCSDO_H_ */
