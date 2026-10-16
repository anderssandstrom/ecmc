/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcEcAsyncSDO.cpp
*
*  Created on: Dec 15, 2015
*      Author: anderssandstrom
*
\*************************************************************************/

#include <vector>
#include <exception>
#include <stdexcept>
#include <cmath>
#include <limits>
#include <algorithm>
#include "ecmcEcAsyncSDO.h"
#include "ecmcRtLogger.h"


/**
 * Callback function for asynWrites (control word)
 * userObj = axis object
 *
 * */
asynStatus asynWriteSDOCmd(void         *data,
                           size_t        bytes,
                           asynParamType asynParType,
                           void         *userObj) {
  if (!userObj) {
    return asynError;
  }
  return ((ecmcEcAsyncSDO *)userObj)->asynWriteSDO(data, bytes, asynParType);
}

asynStatus asynReadSDOCmd(void         *data,
                          size_t        bytes,
                          asynParamType asynParType,
                          void         *userObj) {
  if (!userObj) {
    return asynError;
  }
  return ((ecmcEcAsyncSDO *)userObj)->asynReadSDO(data, bytes, asynParType);
}

static asynStatus asynWriteSDOArray(void *data, size_t bytes,
                                    asynParamType type, void *userObj) {
  return static_cast<ecmcEcAsyncSDO *>(userObj)->asynWriteArray(data, bytes, type);
}

ecmcEcAsyncSDO::ecmcEcAsyncSDO(ecmcAsynPortDriver *asynDriver,
                               int                 masterId,
                               int                 slaveId,
                               ec_slave_config_t  *sc, /**< Slave configuration. */
                               uint16_t            sdoIndex, /**< SDO index. */
                               uint8_t             sdoSubindex, /**< SDO subindex. */
                               ecmcEcDataType      dt,
                               std::string         alias,
                               size_t              arrayBytes) {
  if (arrayBytes > ECMC_SDO_ASYNC_MAX_BYTES) {
    throw std::invalid_argument("Async SDO array exceeds maximum size");
  }
  arrayReadBuffer_.resize(arrayBytes, 0);
  arrayWriteBuffer_.resize(arrayBytes, 0);
  arrayWriteReady_ = false;
  arrayWriteInitialized_.resize(arrayBytes, 0);
  arrayInitializedCount_ = 0;
  arrayValidBytes_ = 0;
  arrayReadBytes_ = 0;
  asynParamReadBytes_ = NULL;
  asynParamWriteValue_ = NULL;
  masterId_          = masterId;
  slaveId_           = slaveId;
  asynPortDriver_    = asynDriver;
  sc_                = sc;
  index_             = sdoIndex;
  subindex_          = sdoSubindex;
  dt_                = dt;
  buffer_            = 0;
  asynParamWrite_    = NULL;
  asynParamRead_     = NULL;
  asynParamValue_    = NULL;
  asynParamError_    = NULL;
  asynParamBusy_     = NULL;
  sdoError_          = 0;
  dummyReadCmd_      = 0;
  dummyWriteCmd_     = 0;
  usedSizeBytes_     = 0;
  readTrigg_         = 0;
  writeTrigg_        = 0;
  bitLength_         = getEcDataTypeBits(dt);
  size_              = arrayBytes ? arrayBytes : getEcDataTypeByteSize(dt);
  busy_              = 0;
  writeCmdInProcess_ = 0;
  readCmdInProcess_  = 0;
  idString_          = alias;
  idStringChar_      = strdup(idString_.c_str());
  stateOld_          = EC_REQUEST_UNUSED;
  int8Ptr_           = (int8_t *)&buffer_;
  uint8Ptr_          = (uint8_t *)&buffer_;
  int16Ptr_          = (int16_t *)&buffer_;
  uint16Ptr_         = (uint16_t *)&buffer_;
  int32Ptr_          = (int32_t *)&buffer_;
  uint32Ptr_         = (uint32_t *)&buffer_;
  int64Ptr_          = (int64_t *)&buffer_;
  uint64Ptr_         = (uint64_t *)&buffer_;
  float32Ptr_        = (float *)&buffer_;
  float64Ptr_        = (double *)&buffer_;

  // create object
  sdoreq_ = ecrt_slave_config_create_sdo_request(
    sc_,                      /**< Slave configuration. */
    index_,                      /**< SDO index. */
    subindex_,                      /**< SDO subindex. */
    size_
    );

  if (sdoreq_ == NULL) {
    ecmcRtLoggerLogError(
      "%s/%s:%d: ERROR: Failed creation of SDO async object.\n",
      __FILE__,
      __FUNCTION__,
      __LINE__);

    throw std::bad_alloc();
  }

  // Uploads change the request data size. Keep a separate, fixed-size
  // download request for arrays so short uploads cannot shorten downloads.
  readreq_ = arrayBytes ? ecrt_slave_config_create_sdo_request(
    sc_, index_, subindex_, size_) : sdoreq_;
  if (!readreq_) {
    throw std::bad_alloc();
  }
  ecrt_sdo_request_timeout(readreq_, DEFAULT_SDO_ASYNC_TIMOUT_MS);
  ecrt_sdo_request_timeout(sdoreq_, DEFAULT_SDO_ASYNC_TIMOUT_MS);

  if (initAsyn() != 0) {
    ecmcRtLoggerLogError(
      "%s/%s:%d: ERROR: initAsyn() failed.\n",
      __FILE__,
      __FUNCTION__,
      __LINE__);

    throw std::bad_alloc();
  }
}

ecmcEcAsyncSDO::~ecmcEcAsyncSDO() {
  free(idStringChar_);
}

asynStatus ecmcEcAsyncSDO::asynWriteArray(void *data, size_t bytes,
                                         asynParamType type) {
  // Reject partial/oversized writes rather than downloading stale tail bytes.
  if (!data || arrayWriteBuffer_.empty() || type != asynParamInt8Array ||
      bytes != size_ || busy_) {
    sdoError_ = busy_ ? ERROR_EC_SDO_ASYNC_BUSY : ERROR_EC_SDO_ASYNC_SIZE;
    asynParamError_->refreshParam(1);
    return asynError;
  }
  memcpy(arrayWriteBuffer_.data(), data, size_);
  arrayWriteReady_ = true;
  asynParamWriteValue_->refreshParam(1);
  return asynSuccess;
}

asynStatus ecmcEcAsyncSDO::asynWriteSDO(void         *data,
                                        size_t        bytes,
                                        asynParamType asynParType) {
  if ((bytes != 4) || (asynParType != asynParamInt32)) {
    ecmcRtLoggerLogError(
      "%s/%s:%d: ERROR: SDO read failed. Write cmd trigger data size or datatype mismatch.\n",
      __FILE__,
      __FUNCTION__,
      __LINE__);
    return asynError;
  }

  int trigg = 0;
  memcpy(&trigg, data, bytes);

  // Check if positive edge
  if (!(trigg && !writeTrigg_)) {
    writeTrigg_ = trigg;
    return asynSuccess;
  }

  writeTrigg_ = trigg;
  return requestWrite() ? asynError : asynSuccess;
}

int ecmcEcAsyncSDO::requestWrite() {

  if (!sdoreq_) {
    ecmcRtLoggerLogError(
      "%s/%s:%d: ERROR: SDO write failed. SDO async object is NULL (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__, ERROR_EC_SDO_ASYNC_OBJ_NULL);
    sdoError_ = ERROR_EC_SDO_ASYNC_OBJ_NULL;
    asynParamError_->refreshParam(1);
    return sdoError_;
  }


  ec_request_state_t state = ecrt_sdo_request_state(sdoreq_);

  if (busy_ || state == EC_REQUEST_BUSY) {
    ecmcRtLoggerLogError(
      "%s/%s:%d: ERROR: SDO write failed. SDO object is busy (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      ERROR_EC_SDO_ASYNC_BUSY);
    sdoError_ = ERROR_EC_SDO_ASYNC_BUSY;
    asynParamError_->refreshParam(1);
    return sdoError_;
  }

  if (!arrayWriteBuffer_.empty() && !arrayWriteReady_) {
    sdoError_ = ERROR_EC_SDO_ASYNC_SIZE;
    asynParamError_->refreshParam(1);
    return sdoError_;
  }
  sdoError_ = 0;
  asynParamError_->refreshParam(1);
  busy_ = 1;
  asynParamBusy_->refreshParam(1);
  writeCmdInProcess_ = true;

  // write data to sdo req obj (buffer_ data should already be updated by asyn)
  writeValue();

  if (asynParamWriteValue_) asynParamWriteValue_->refreshParamRT(1);
  // request/initiate write
  ecrt_sdo_request_write(sdoreq_);

  return 0;
}

asynStatus ecmcEcAsyncSDO::asynReadSDO(void         *data,
                                       size_t        bytes,
                                       asynParamType asynParType) {
  if ((bytes != 4) || (asynParType != asynParamInt32)) {
    ecmcRtLoggerLogError(
      "%s/%s:%d: ERROR: SDO read failed. Read cmd trigger data size or datatype mismatch..\n",
      __FILE__,
      __FUNCTION__,
      __LINE__);
    return asynError;
  }

  int trigg = 0;
  memcpy(&trigg, data, bytes);

  // Check if positive edge
  if (!(trigg && !readTrigg_)) {
    readTrigg_ = trigg;
    return asynSuccess;
  }

  readTrigg_ = trigg;
  return requestRead() ? asynError : asynSuccess;
}

int ecmcEcAsyncSDO::requestRead() {

  if (!readreq_) {
    ecmcRtLoggerLogError(
      "%s/%s:%d: ERROR: SDO read failed. SDO async object is NULL (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__, ERROR_EC_SDO_ASYNC_OBJ_NULL);
    sdoError_ = ERROR_EC_SDO_ASYNC_OBJ_NULL;
    asynParamError_->refreshParam(1);
    return sdoError_;
  }

  ec_request_state_t state = ecrt_sdo_request_state(readreq_);

  if (busy_ || state == EC_REQUEST_BUSY) {
    ecmcRtLoggerLogError(
      "%s/%s:%d: ERROR: SDO read failed. SDO object is busy (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      ERROR_EC_SDO_ASYNC_BUSY);
    sdoError_ = ERROR_EC_SDO_ASYNC_BUSY;
    asynParamError_->refreshParam(1);
    return sdoError_;
  }

  sdoError_ = 0;
  asynParamError_->refreshParam(1);
  busy_ = 1;
  asynParamBusy_->refreshParam(1);

  ecrt_sdo_request_read(readreq_);
  readCmdInProcess_ = true;
  return 0;
}

int ecmcEcAsyncSDO::execute() {
  if (!readCmdInProcess_ && !writeCmdInProcess_) {
    return 0;
  }

  ec_request_state_t state = ecrt_sdo_request_state(readCmdInProcess_ ? readreq_ : sdoreq_);

  stateOld_ = state;

  switch (state) {
  case EC_REQUEST_UNUSED:
    break;

  case EC_REQUEST_BUSY:
    return 0;   // keep waiting for finalizing request

    break;

  case EC_REQUEST_SUCCESS:

    if (readCmdInProcess_) {
      const int error = readValue();
      if (error) {
        sdoError_ = error;
        asynParamError_->refreshParamRT(1);
      } else {
        asynParamValue_->refreshParamRT(1);
      }
      if (asynParamReadBytes_) {
        asynParamReadBytes_->refreshParamRT(1);
      }
    }
    readCmdInProcess_  = 0;
    writeCmdInProcess_ = 0;
    busy_              = 0;
    asynParamBusy_->refreshParamRT(1);
    break;

  case EC_REQUEST_ERROR:
    sdoError_ = ERROR_EC_SDO_ASYNC_ERROR;
    asynParamError_->refreshParamRT(1);
    readCmdInProcess_  = 0;
    writeCmdInProcess_ = 0;
    busy_              = 0;
    asynParamBusy_->refreshParamRT(1);
    break;
  }
  return 0;
}

int ecmcEcAsyncSDO::initAsyn() {
  char  buffer[EC_MAX_OBJECT_PATH_CHAR_LENGTH];
  char *name = buffer;

  // "ec%d.s%d.sdo.value"
  unsigned int charCount = snprintf(buffer,
                                    sizeof(buffer),
                                    ECMC_EC_STR "%d." ECMC_SLAVE_CHAR "%d." ECMC_SDO_STR ".%s." ECMC_VALUE_STR,
                                    masterId_,
                                    slaveId_,
                                    idStringChar_);

  if (charCount >= sizeof(buffer) - 1) {
    ecmcRtLoggerLogError(
      "%s/%s:%d: ERROR: Failed to generate alias. Buffer too small (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      ERROR_EC_SDO_ASYNC_ASYN_OBJ_FAIL);
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SDO_ASYNC_ASYN_OBJ_FAIL);
  }
  name            = buffer;
  asynParamValue_ = asynPortDriver_->addNewAvailParam(name,
                                                      arrayReadBuffer_.empty() ? asynParamInt32 : asynParamInt8Array,
                                                      arrayReadBuffer_.empty() ? (uint8_t *)&buffer_ : arrayReadBuffer_.data(),
                                                      arrayReadBuffer_.empty() ? sizeof(buffer_) : size_,
                                                      dt_,
                                                      0);

  if (!asynParamValue_) {
    ecmcRtLoggerLogError(
      "%s/%s:%d: ERROR: Add create default parameter for %s failed.\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      name);
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_MAIN_ASYN_CREATE_PARAM_FAIL);
  }

  if (!arrayReadBuffer_.empty()) {
    asynParamValue_->setAllowWriteToEcmc(false);
    asynParamValue_->refreshParam(1, size_t(0));
    const std::string prefix = "ec" + std::to_string(masterId_) + ".s" +
      std::to_string(slaveId_) + ".sdo." + idString_;
    asynParamWriteValue_ = asynPortDriver_->addNewAvailParam(
      (prefix + ".writevalue").c_str(), asynParamInt8Array,
      arrayWriteBuffer_.data(), size_, ECMC_EC_U8, 0);
    asynParamReadBytes_ = asynPortDriver_->addNewAvailParam(
      (prefix + ".bytes").c_str(), asynParamInt32,
      reinterpret_cast<uint8_t *>(&arrayReadBytes_), sizeof(arrayReadBytes_),
      ECMC_EC_S32, 0);
    if (!asynParamWriteValue_ || !asynParamReadBytes_) {
      return ERROR_EC_SDO_ASYNC_ASYN_OBJ_FAIL;
    }
    asynParamWriteValue_->setAllowWriteToEcmc(true);
    asynParamWriteValue_->setExeCmdFunctPtr(asynWriteSDOArray, this);
    asynParamWriteValue_->refreshParam(1);
    asynParamReadBytes_->setAllowWriteToEcmc(false);
    asynParamReadBytes_->refreshParam(1);
  } else {
    // Add supported types
    switch (dt_) {
    case ECMC_EC_NONE:
      return 0;

      break;

    case ECMC_EC_B1:
      asynParamValue_->addSupportedAsynType(asynParamInt32);
      asynParamValue_->addSupportedAsynType(asynParamUInt32Digital);
      asynParamValue_->addSupportedAsynType(asynParamFloat64);
      usedSizeBytes_ = 1;
      break;

    case ECMC_EC_B2:
      asynParamValue_->addSupportedAsynType(asynParamInt32);
      asynParamValue_->addSupportedAsynType(asynParamUInt32Digital);
      asynParamValue_->addSupportedAsynType(asynParamFloat64);
      usedSizeBytes_ = 1;
      break;

    case ECMC_EC_B3:
      asynParamValue_->addSupportedAsynType(asynParamInt32);
      asynParamValue_->addSupportedAsynType(asynParamUInt32Digital);
      asynParamValue_->addSupportedAsynType(asynParamFloat64);
      usedSizeBytes_ = 1;
      break;

    case ECMC_EC_B4:
      asynParamValue_->addSupportedAsynType(asynParamInt32);
      asynParamValue_->addSupportedAsynType(asynParamUInt32Digital);
      asynParamValue_->addSupportedAsynType(asynParamFloat64);
      usedSizeBytes_ = 1;
      break;

    case ECMC_EC_U8:
      asynParamValue_->addSupportedAsynType(asynParamInt32);
      asynParamValue_->addSupportedAsynType(asynParamUInt32Digital);
      asynParamValue_->addSupportedAsynType(asynParamFloat64);
      usedSizeBytes_ = 1;
      break;

    case ECMC_EC_S8:
      asynParamValue_->addSupportedAsynType(asynParamInt32);
      asynParamValue_->addSupportedAsynType(asynParamUInt32Digital);
      asynParamValue_->addSupportedAsynType(asynParamFloat64);
      usedSizeBytes_ = 1;
      break;

    case ECMC_EC_S8_TO_U8:
      asynParamValue_->addSupportedAsynType(asynParamInt32);
      asynParamValue_->addSupportedAsynType(asynParamUInt32Digital);
      asynParamValue_->addSupportedAsynType(asynParamFloat64);
      usedSizeBytes_ = 1;
      break;

    case ECMC_EC_U16:
      asynParamValue_->addSupportedAsynType(asynParamInt32);
      asynParamValue_->addSupportedAsynType(asynParamUInt32Digital);
      asynParamValue_->addSupportedAsynType(asynParamFloat64);
      usedSizeBytes_ = 2;
      break;

    case ECMC_EC_S16:
      asynParamValue_->addSupportedAsynType(asynParamInt32);
      asynParamValue_->addSupportedAsynType(asynParamUInt32Digital);
      asynParamValue_->addSupportedAsynType(asynParamFloat64);
      usedSizeBytes_ = 2;
      break;

    case ECMC_EC_S16_TO_U16:
      asynParamValue_->addSupportedAsynType(asynParamInt32);
      asynParamValue_->addSupportedAsynType(asynParamUInt32Digital);
      asynParamValue_->addSupportedAsynType(asynParamFloat64);
      usedSizeBytes_ = 2;
      break;

    case ECMC_EC_U32:
      asynParamValue_->addSupportedAsynType(asynParamInt32);
      asynParamValue_->addSupportedAsynType(asynParamUInt32Digital);
      asynParamValue_->addSupportedAsynType(asynParamFloat64);
      usedSizeBytes_ = 4;
      break;

    case ECMC_EC_S32:
      asynParamValue_->addSupportedAsynType(asynParamInt32);
      asynParamValue_->addSupportedAsynType(asynParamUInt32Digital);
      asynParamValue_->addSupportedAsynType(asynParamFloat64);
      usedSizeBytes_ = 4;
      break;

    case ECMC_EC_S32_TO_U32:
      asynParamValue_->addSupportedAsynType(asynParamInt32);
      asynParamValue_->addSupportedAsynType(asynParamUInt32Digital);
      asynParamValue_->addSupportedAsynType(asynParamFloat64);
      usedSizeBytes_ = 4;
      break;

    case ECMC_EC_U64:
      asynParamValue_->addSupportedAsynType(asynParamInt32);
      asynParamValue_->addSupportedAsynType(asynParamUInt32Digital);
      asynParamValue_->addSupportedAsynType(asynParamFloat64);

  #ifdef ECMC_ASYN_ASYNPARAMINT64
      asynParamValue_->addSupportedAsynType(asynParamInt64);
  #endif //ECMC_ASYN_ASYNPARAMINT64

      usedSizeBytes_ = 8;
      break;

    case ECMC_EC_S64:
      asynParamValue_->addSupportedAsynType(asynParamInt32);
      asynParamValue_->addSupportedAsynType(asynParamUInt32Digital);
      asynParamValue_->addSupportedAsynType(asynParamFloat64);

  #ifdef ECMC_ASYN_ASYNPARAMINT64
      asynParamValue_->addSupportedAsynType(asynParamInt64);
  #endif //ECMC_ASYN_ASYNPARAMINT64

      usedSizeBytes_ = 8;
      break;

    case ECMC_EC_S64_TO_U64:
      asynParamValue_->addSupportedAsynType(asynParamInt32);
      asynParamValue_->addSupportedAsynType(asynParamUInt32Digital);
      asynParamValue_->addSupportedAsynType(asynParamFloat64);

  #ifdef ECMC_ASYN_ASYNPARAMINT64
      asynParamValue_->addSupportedAsynType(asynParamInt64);
  #endif //ECMC_ASYN_ASYNPARAMINT64

      usedSizeBytes_ = 8;
      break;

    case ECMC_EC_F32:
      asynParamValue_->addSupportedAsynType(asynParamFloat64);
      usedSizeBytes_ = 4;
      break;

    case ECMC_EC_F64:
      asynParamValue_->addSupportedAsynType(asynParamFloat64);
      usedSizeBytes_ = 8;
      break;
    }
    asynParamValue_->setEcmcDataSize(usedSizeBytes_);

    asynParamValue_->setAllowWriteToEcmc(true);
    asynParamValue_->setEcmcBitCount(bitLength_);
    asynParamValue_->setEcmcMinValueInt(getEcDataTypeMinVal(dt_));
    asynParamValue_->setEcmcMaxValueInt(getEcDataTypeMaxVal(dt_));

    asynParamValue_->refreshParam(1);

  } // scalar value setup

  // "ec%d.s%d.sdo.error"
  charCount = snprintf(buffer,
                       sizeof(buffer),
                       ECMC_EC_STR "%d." ECMC_SLAVE_CHAR "%d." ECMC_SDO_STR ".%s." ECMC_ERROR_STR,
                       masterId_,
                       slaveId_,
                       idStringChar_);

  if (charCount >= sizeof(buffer) - 1) {
    ecmcRtLoggerLogError(
      "%s/%s:%d: ERROR: Failed to generate alias. Buffer too small (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      ERROR_EC_SDO_ASYNC_ASYN_OBJ_FAIL);
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SDO_ASYNC_ASYN_OBJ_FAIL);
  }
  name            = buffer;
  asynParamError_ = asynPortDriver_->addNewAvailParam(name,
                                                      asynParamInt32, // default type
                                                      (uint8_t *)&(sdoError_),
                                                      sizeof(sdoError_),
                                                      ECMC_EC_S32,
                                                      0);

  if (!asynParamError_) {
    ecmcRtLoggerLogError(
      "%s/%s:%d: ERROR: Add create default parameter for %s failed.\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      name);
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_MAIN_ASYN_CREATE_PARAM_FAIL);
  }
  asynParamError_->refreshParam(1);

  // "ec%d.s%d.sdo.readcmd"
  charCount = snprintf(buffer,
                       sizeof(buffer),
                       ECMC_EC_STR "%d." ECMC_SLAVE_CHAR "%d." ECMC_SDO_STR ".%s." ECMC_READCMD_STR,
                       masterId_,
                       slaveId_,
                       idStringChar_);

  if (charCount >= sizeof(buffer) - 1) {
    ecmcRtLoggerLogError(
      "%s/%s:%d: ERROR: Failed to generate alias. Buffer too small (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      ERROR_EC_SDO_ASYNC_ASYN_OBJ_FAIL);
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SDO_ASYNC_ASYN_OBJ_FAIL);
  }
  name           = buffer;
  asynParamRead_ = asynPortDriver_->addNewAvailParam(name,
                                                     asynParamInt32,

                                                     // default type
                                                     (uint8_t *)&(dummyReadCmd_),
                                                     sizeof(dummyReadCmd_),
                                                     ECMC_EC_S32,
                                                     0);

  if (!asynParamRead_) {
    ecmcRtLoggerLogError(
      "%s/%s:%d: ERROR: Add create default parameter for %s failed.\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      name);
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_MAIN_ASYN_CREATE_PARAM_FAIL);
  }
  asynParamRead_->setAllowWriteToEcmc(true);
  asynParamRead_->setExeCmdFunctPtr(asynReadSDOCmd, this); // Access to this axis
  asynParamRead_->refreshParam(1);

  // "ec%d.s%d.sdo.writecmd"
  charCount = snprintf(buffer,
                       sizeof(buffer),
                       ECMC_EC_STR "%d." ECMC_SLAVE_CHAR "%d." ECMC_SDO_STR ".%s." ECMC_WRITECMD_STR,
                       masterId_,
                       slaveId_,
                       idStringChar_);

  if (charCount >= sizeof(buffer) - 1) {
    ecmcRtLoggerLogError(
      "%s/%s:%d: ERROR: Failed to generate alias. Buffer too small (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      ERROR_EC_SDO_ASYNC_ASYN_OBJ_FAIL);
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SDO_ASYNC_ASYN_OBJ_FAIL);
  }
  name            = buffer;
  asynParamWrite_ = asynPortDriver_->addNewAvailParam(name,
                                                      asynParamInt32, // default type
                                                      (uint8_t *)&(
                                                        dummyWriteCmd_),
                                                      sizeof(dummyWriteCmd_),
                                                      ECMC_EC_S32,
                                                      0);

  if (!asynParamWrite_) {
    ecmcRtLoggerLogError(
      "%s/%s:%d: ERROR: Add create default parameter for %s failed.\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      name);
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_MAIN_ASYN_CREATE_PARAM_FAIL);
  }
  asynParamWrite_->setAllowWriteToEcmc(true);
  asynParamWrite_->setExeCmdFunctPtr(asynWriteSDOCmd, this); // Access to this axis
  asynParamWrite_->refreshParam(1);

  // "ec%d.s%d.sdo.busy"
  charCount = snprintf(buffer,
                       sizeof(buffer),
                       ECMC_EC_STR "%d." ECMC_SLAVE_CHAR "%d." ECMC_SDO_STR ".%s." ECMC_BUSY_STR,
                       masterId_,
                       slaveId_,
                       idStringChar_);

  if (charCount >= sizeof(buffer) - 1) {
    ecmcRtLoggerLogError(
      "%s/%s:%d: ERROR: Failed to generate alias. Buffer too small (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      ERROR_EC_SDO_ASYNC_ASYN_OBJ_FAIL);
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SDO_ASYNC_ASYN_OBJ_FAIL);
  }
  name           = buffer;
  asynParamBusy_ = asynPortDriver_->addNewAvailParam(name,
                                                     asynParamInt32, // default type
                                                     (uint8_t *)&(busy_),
                                                     sizeof(busy_),
                                                     ECMC_EC_S32,
                                                     0);

  if (!asynParamBusy_) {
    ecmcRtLoggerLogError(
      "%s/%s:%d: ERROR: Add create default parameter for %s failed.\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      name);
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_MAIN_ASYN_CREATE_PARAM_FAIL);
  }
  asynParamBusy_->refreshParam(1);

  asynPortDriver_->callParamCallbacks(ECMC_ASYN_DEFAULT_LIST,
                                      ECMC_ASYN_DEFAULT_ADDR);

  return 0;
}

int ecmcEcAsyncSDO::readValue() {
  const size_t bytes = ecrt_sdo_request_data_size(readreq_);
  if (!arrayReadBuffer_.empty()) {
    arrayReadBytes_ = static_cast<int32_t>(bytes <= INT32_MAX ? bytes : INT32_MAX);
    if (bytes > size_) {
      return ERROR_EC_SDO_ASYNC_SIZE; // Retain previous good waveform.
    }
    memcpy(arrayReadBuffer_.data(), ecrt_sdo_request_data(readreq_), bytes);
    arrayValidBytes_ = static_cast<int>(bytes);
    asynParamValue_->setEcmcDataSize(bytes);
    return 0;
  }
  if (bytes != size_) {
    return ERROR_EC_SDO_ASYNC_SIZE;
  }
  switch (dt_) {
  case ECMC_EC_NONE:
    buffer_ = 0;
    break;

  case ECMC_EC_B1:
    buffer_ = 0;
    break;

  case ECMC_EC_B2:
    buffer_ = 0;
    break;

  case ECMC_EC_B3:
    buffer_ = 0;
    break;

  case ECMC_EC_B4:
    buffer_ = 0;
    break;

  case ECMC_EC_U8:
    *uint8Ptr_ = EC_READ_U8(ecrt_sdo_request_data(sdoreq_));
    break;

  case ECMC_EC_S8:
    *int8Ptr_ = EC_READ_S8(ecrt_sdo_request_data(sdoreq_));
    break;

  case ECMC_EC_S8_TO_U8:
    *uint8Ptr_ = static_cast<uint8_t>(EC_READ_S8(ecrt_sdo_request_data(sdoreq_)));
    break;

  case ECMC_EC_U16:
    *uint16Ptr_ = EC_READ_U16(ecrt_sdo_request_data(sdoreq_));
    break;

  case ECMC_EC_S16:
    *int16Ptr_ = EC_READ_S16(ecrt_sdo_request_data(sdoreq_));
    break;

  case ECMC_EC_S16_TO_U16:
    *uint16Ptr_ = static_cast<uint16_t>(EC_READ_S16(ecrt_sdo_request_data(sdoreq_)));
    break;

  case ECMC_EC_U32:
    *uint32Ptr_ = EC_READ_U32(ecrt_sdo_request_data(sdoreq_));
    break;

  case ECMC_EC_S32:
    *int32Ptr_ = EC_READ_S32(ecrt_sdo_request_data(sdoreq_));
    break;

  case ECMC_EC_S32_TO_U32:
    *uint32Ptr_ = static_cast<uint32_t>(EC_READ_S32(ecrt_sdo_request_data(sdoreq_)));
    break;

#ifdef EC_READ_U64
  case ECMC_EC_U64:
    *uint64Ptr_ = EC_READ_U64(ecrt_sdo_request_data(sdoreq_));
    break;
#endif // ifdef EC_READ_U64

#ifdef EC_READ_S64
  case ECMC_EC_S64:
    *int64Ptr_ = EC_READ_S64(ecrt_sdo_request_data(sdoreq_));
    break;

  case ECMC_EC_S64_TO_U64:
    *uint64Ptr_ = static_cast<uint64_t>(EC_READ_S64(ecrt_sdo_request_data(sdoreq_)));
    break;
#endif // ifdef EC_READ_S64

#ifdef EC_READ_REAL
  case ECMC_EC_F32:
    *float32Ptr_ = EC_READ_REAL(ecrt_sdo_request_data(sdoreq_));
    break;
#endif // ifdef EC_READ_REAL

#ifdef EC_READ_LREAL
  case ECMC_EC_F64:
    *float64Ptr_ = EC_READ_LREAL(ecrt_sdo_request_data(sdoreq_));
    break;

#endif // ifdef EC_READ_LREAL
  default:
    buffer_ = 0;
    break;
  }

  return 0;
}

int ecmcEcAsyncSDO::writeValue() {
  if (!arrayWriteBuffer_.empty()) {
    memcpy(ecrt_sdo_request_data(sdoreq_), arrayWriteBuffer_.data(), size_);
    return 0;
  }
  switch (dt_) {
  case ECMC_EC_NONE:
    buffer_ = 0;
    break;

  case ECMC_EC_B1:
    buffer_ = 0;
    break;

  case ECMC_EC_B2:
    buffer_ = 0;
    break;

  case ECMC_EC_B3:
    buffer_ = 0;
    break;

  case ECMC_EC_B4:
    buffer_ = 0;
    break;

  case ECMC_EC_U8:
    EC_WRITE_U8(ecrt_sdo_request_data(sdoreq_), *uint8Ptr_);
    break;

  case ECMC_EC_S8:
    EC_WRITE_S8(ecrt_sdo_request_data(sdoreq_), *int8Ptr_);
    break;

  case ECMC_EC_S8_TO_U8:
    EC_WRITE_S8(ecrt_sdo_request_data(sdoreq_), static_cast<int8_t>(*uint8Ptr_));
    break;

  case ECMC_EC_U16:
    EC_WRITE_U16(ecrt_sdo_request_data(sdoreq_), *uint16Ptr_);
    break;

  case ECMC_EC_S16:
    EC_WRITE_S16(ecrt_sdo_request_data(sdoreq_), *int16Ptr_);
    break;

  case ECMC_EC_S16_TO_U16:
    EC_WRITE_S16(ecrt_sdo_request_data(sdoreq_), static_cast<int16_t>(*uint16Ptr_));
    break;

  case ECMC_EC_U32:
    EC_WRITE_U32(ecrt_sdo_request_data(sdoreq_), *uint32Ptr_);
    break;

  case ECMC_EC_S32:
    EC_WRITE_S32(ecrt_sdo_request_data(sdoreq_), *int32Ptr_);
    break;

  case ECMC_EC_S32_TO_U32:
    EC_WRITE_S32(ecrt_sdo_request_data(sdoreq_), static_cast<int32_t>(*uint32Ptr_));
    break;

#ifdef EC_WRITE_U64
  case ECMC_EC_U64:
    EC_WRITE_U64(ecrt_sdo_request_data(sdoreq_), *uint64Ptr_);
    break;
#endif // ifdef EC_WRITE_U64

#ifdef EC_WRITE_S64
  case ECMC_EC_S64:
    EC_WRITE_S64(ecrt_sdo_request_data(sdoreq_), *int64Ptr_);
    break;

  case ECMC_EC_S64_TO_U64:
    EC_WRITE_S64(ecrt_sdo_request_data(sdoreq_), static_cast<int64_t>(*uint64Ptr_));
    break;
#endif // ifdef EC_WRITE_S64

#ifdef EC_WRITE_REAL
  case ECMC_EC_F32:
    EC_WRITE_REAL(ecrt_sdo_request_data(sdoreq_), *float32Ptr_);
    break;
#endif // ifdef EC_WRITE_REAL

#ifdef EC_WRITE_LREAL
  case ECMC_EC_F64:
    EC_WRITE_LREAL(ecrt_sdo_request_data(sdoreq_), *float64Ptr_);
    break;
#endif // ifdef EC_WRITE_LREAL

  default:
    buffer_ = 0;
    break;
  }

  return 0;
}

namespace {
template<typename T>
int sdoAssignValue(double value, T *target) {
  if (!std::isfinite(value)) return ERROR_EC_SDO_ASYNC_VALUE;
  if (std::numeric_limits<T>::is_integer) {
    const double upper = std::ldexp(1.0, std::numeric_limits<T>::digits);
    const double lower = std::numeric_limits<T>::is_signed ? -upper : 0.0;
    if (std::trunc(value) != value || value < lower || value >= upper) {
      return ERROR_EC_SDO_ASYNC_VALUE;
    }
  } else if (std::fabs(value) > std::numeric_limits<T>::max()) {
    return ERROR_EC_SDO_ASYNC_VALUE;
  }
  *target = static_cast<T>(value);
  return 0;
}
}

int ecmcEcAsyncSDO::getValue(int element, double *value) const {
  if (!value || element < 0 || element >= getSize()) {
    return ERROR_EC_SDO_ASYNC_SIZE;
  }
  if (!arrayReadBuffer_.empty()) {
    *value = arrayReadBuffer_[element];
    return 0;
  }
  switch (dt_) {
  case ECMC_EC_U8: *value = static_cast<double>(*uint8Ptr_); return 0;
  case ECMC_EC_S8: *value = static_cast<double>(*int8Ptr_); return 0;
  case ECMC_EC_S8_TO_U8: *value = static_cast<double>(*uint8Ptr_); return 0;
  case ECMC_EC_U16: *value = static_cast<double>(*uint16Ptr_); return 0;
  case ECMC_EC_S16: *value = static_cast<double>(*int16Ptr_); return 0;
  case ECMC_EC_S16_TO_U16: *value = static_cast<double>(*uint16Ptr_); return 0;
  case ECMC_EC_U32: *value = static_cast<double>(*uint32Ptr_); return 0;
  case ECMC_EC_S32: *value = static_cast<double>(*int32Ptr_); return 0;
  case ECMC_EC_S32_TO_U32: *value = static_cast<double>(*uint32Ptr_); return 0;
  case ECMC_EC_U64: *value = static_cast<double>(*uint64Ptr_); return 0;
  case ECMC_EC_S64: *value = static_cast<double>(*int64Ptr_); return 0;
  case ECMC_EC_S64_TO_U64: *value = static_cast<double>(*uint64Ptr_); return 0;
  case ECMC_EC_F32: *value = static_cast<double>(*float32Ptr_); return 0;
  case ECMC_EC_F64: *value = static_cast<double>(*float64Ptr_); return 0;
  default: return ERROR_EC_SDO_ASYNC_VALUE;
  }
}

int ecmcEcAsyncSDO::setValue(int element, double value) {
  if (busy_) return ERROR_EC_SDO_ASYNC_BUSY;
  if (element < 0 || element >= getCapacity()) return ERROR_EC_SDO_ASYNC_SIZE;
  if (!arrayWriteBuffer_.empty()) {
    const int error = sdoAssignValue(value, &arrayWriteBuffer_[element]);
    if (error) return error;
    if (!arrayWriteInitialized_[element]) {
      arrayWriteInitialized_[element] = 1;
      ++arrayInitializedCount_;
    }
    if (arrayInitializedCount_ == size_) arrayWriteReady_ = true;
    // Publish staged bytes when a download is requested, not once per byte.
    return 0;
  }
  int error = 0;
  switch (dt_) {
  case ECMC_EC_U8: error = sdoAssignValue(value, uint8Ptr_); break;
  case ECMC_EC_S8: error = sdoAssignValue(value, int8Ptr_); break;
  case ECMC_EC_S8_TO_U8: error = sdoAssignValue(value, uint8Ptr_); break;
  case ECMC_EC_U16: error = sdoAssignValue(value, uint16Ptr_); break;
  case ECMC_EC_S16: error = sdoAssignValue(value, int16Ptr_); break;
  case ECMC_EC_S16_TO_U16: error = sdoAssignValue(value, uint16Ptr_); break;
  case ECMC_EC_U32: error = sdoAssignValue(value, uint32Ptr_); break;
  case ECMC_EC_S32: error = sdoAssignValue(value, int32Ptr_); break;
  case ECMC_EC_S32_TO_U32: error = sdoAssignValue(value, uint32Ptr_); break;
  case ECMC_EC_U64: error = sdoAssignValue(value, uint64Ptr_); break;
  case ECMC_EC_S64: error = sdoAssignValue(value, int64Ptr_); break;
  case ECMC_EC_S64_TO_U64: error = sdoAssignValue(value, uint64Ptr_); break;
  case ECMC_EC_F32: error = sdoAssignValue(value, float32Ptr_); break;
  case ECMC_EC_F64: error = sdoAssignValue(value, float64Ptr_); break;
  default: return ERROR_EC_SDO_ASYNC_VALUE;
  }
  if (!error) asynParamValue_->refreshParamRT(1);
  return error;
}
