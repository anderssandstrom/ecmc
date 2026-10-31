/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcPLCDataIF.cpp
*
*  Created on: Oct 4, 2018
*      Author: anderssandstrom
*
\*************************************************************************/

#include "ecmcPLCDataIF.h"
#include "ecmcIoConfig.h"
#include "ecmcErrorsList.h"
#include "ecmcEc.h"
#include "ecmcEcSlave.h"
#include "ecmcRtLogger.h"

ecmcPLCDataIF::ecmcPLCDataIF(int                 plcIndex,
                             double              plcSampleRateMs,
                             ecmcAxisBase       *axis,
                             char               *axisVarName,
                             ecmcAsynPortDriver *asynPortDriver) {
  errorReset();
  initVars();
  plcIndex_       = plcIndex;
  sampleRateMs_   = plcSampleRateMs;
  axis_           = axis;
  varName_        = axisVarName;
  exprTkVarName_  = axisVarName;
  asynPortDriver_ = asynPortDriver;
  source_         = ECMC_RECORDER_SOURCE_AXIS;
  dataSourceAxis_ = parseAxisDataSource(axisVarName);

  if (dataSourceAxis_ == ECMC_AXIS_DATA_NONE) {
    setErrorID(__FILE__,
               __FUNCTION__,
               __LINE__,
               ERROR_PLC_AXIS_DATA_TYPE_ERROR);
    return;
  }
  asynWriteAllow_ = 0;
  int errorCode = initAsyn();
  if (errorCode) {
    setErrorID(__FILE__, __FUNCTION__, __LINE__, errorCode);
  }
}

ecmcPLCDataIF::ecmcPLCDataIF(int                 plcIndex,
                             double              plcSampleRateMs,
                             ecmcDataStorage    *ds,
                             char               *dsVarName,
                             ecmcAsynPortDriver *asynPortDriver) {
  errorReset();
  initVars();
  plcIndex_       = plcIndex;
  sampleRateMs_   = plcSampleRateMs;
  ds_             = ds;
  varName_        = dsVarName;
  exprTkVarName_  = dsVarName;
  asynPortDriver_ = asynPortDriver;
  source_         = ECMC_RECORDER_SOURCE_DATA_STORAGE;
  dataSourceDs_   = parseDataStorageDataSource(dsVarName);

  if (dataSourceDs_ == ECMC_DATA_STORAGE_DATA_NONE) {
    setErrorID(__FILE__,
               __FUNCTION__,
               __LINE__,
               ERROR_PLC_DATA_STORGAE_DATA_TYPE_ERROR);
    return;
  }
  asynWriteAllow_ = 0;
  int errorCode = initAsyn();
  if (errorCode) {
    setErrorID(__FILE__, __FUNCTION__, __LINE__, errorCode);
  }
}

ecmcPLCDataIF::ecmcPLCDataIF(int                 plcIndex,
                             double              plcSampleRateMs,
                             ecmcEc             *ec,
                             char               *ecVarName,
                             ecmcAsynPortDriver *asynPortDriver) {
  errorReset();
  initVars();
  plcIndex_       = plcIndex;
  sampleRateMs_   = plcSampleRateMs;
  ec_             = ec;
  varName_        = ecVarName;
  exprTkVarName_  = ecVarName;
  asynPortDriver_ = asynPortDriver;
  source_         = ECMC_RECORDER_SOURCE_ETHERCAT;
  int errorCode   = parseAndLinkEcDataSource(ecVarName);
  if (errorCode) {
    setErrorID(__FILE__, __FUNCTION__, __LINE__, errorCode);
    return;
  }
  asynWriteAllow_ = 0;
  errorCode = initAsyn();
  if (errorCode) {
    setErrorID(__FILE__, __FUNCTION__, __LINE__, errorCode);
  }
}

ecmcPLCDataIF::ecmcPLCDataIF(int                 plcIndex,
                             double              plcSampleRateMs,
                             char               *varName,
                             ecmcDataSourceType  dataSource,
                             ecmcAsynPortDriver *asynPortDriver) {
  errorReset();
  initVars();
  plcIndex_       = plcIndex;
  sampleRateMs_   = plcSampleRateMs;
  varName_        = varName;
  exprTkVarName_  = varName;
  asynPortDriver_ = asynPortDriver;
  source_         = dataSource;
  asynWriteAllow_ = 1;
  int errorCode = initAsyn();  // Only static and global variables accessible from PLC
  if (errorCode) {
    setErrorID(__FILE__, __FUNCTION__, __LINE__, errorCode);
  }
}

ecmcPLCDataIF::~ecmcPLCDataIF() {
  if (asynDataItem_) asynDataItem_->invalidateDataBindings();
}

void ecmcPLCDataIF::initVars() {
  plcIndex_          = 0;
  axis_              = 0;
  ds_                = 0;
  ec_                = 0;
  data_              = 0;
  dataRead_          = 0;
  dataRaw_           = (uint64_t*)&data_;
  dataReadRaw_       = (uint64_t*)&dataRead_;
  varName_           = "";
  exprTkVarName_     = "";
  dataSourceAxis_    = ECMC_AXIS_DATA_NONE;
  dataSourceDs_      = ECMC_DATA_STORAGE_DATA_NONE;
  source_            = ECMC_RECORDER_SOURCE_NONE;
  readOnly_          = 0;
  asynPortDriver_    = 0;
  asynDataItem_      = 0;
  asynWriteAllow_    = 0;
  isBool_            = 0;
  sampleRateMs_      = 0;
  axisHasController_ = 0;
}

double& ecmcPLCDataIF::getDataRef() {
  return data_;
}

int ecmcPLCDataIF::read() {
  int errorCode = 0;

  switch (source_) {
  case ECMC_RECORDER_SOURCE_NONE:
    errorCode = ERROR_PLC_SOURCE_INVALID;
    break;

  case ECMC_RECORDER_SOURCE_ETHERCAT:
    errorCode = readEc();
    break;

  case ECMC_RECORDER_SOURCE_AXIS:
    errorCode = readAxis();
    break;

  case ECMC_RECORDER_SOURCE_STATIC_VAR:
    return 0;

    break;

  case ECMC_RECORDER_SOURCE_GLOBAL_VAR:
    return 0;

    break;

  case ECMC_RECORDER_SOURCE_DATA_STORAGE:
    errorCode = readDs();
    break;
  }

  dataRead_ = data_;
  return setErrorID(__FILE__, __FUNCTION__, __LINE__, errorCode);
}

int ecmcPLCDataIF::write() {
  // Only write if data changed between read and write

  if (( *dataRaw_ == *dataReadRaw_ ) || readOnly_ ||
      (isBool_ && ((data_ > 0) == (dataRead_ > 0)))) {
    return 0;
  }

  switch (source_) {
  case ECMC_RECORDER_SOURCE_NONE:
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_PLC_SOURCE_INVALID);

    break;

  case ECMC_RECORDER_SOURCE_ETHERCAT:
    return writeEc();

    break;

  case ECMC_RECORDER_SOURCE_AXIS:
    return writeAxis();

    break;

  case ECMC_RECORDER_SOURCE_STATIC_VAR:
    return 0;

    break;

  case ECMC_RECORDER_SOURCE_GLOBAL_VAR:
    return 0;

    break;

  case ECMC_RECORDER_SOURCE_DATA_STORAGE:
    return writeDs();

    break;
  }

  return ERROR_PLC_SOURCE_INVALID;
}

int ecmcPLCDataIF::readEc() {
  double tempData  = 0;
  int    errorCode =
    readDataValueDouble(ECMC_PLC_EC_ENTRY_INDEX, &tempData);

  if (errorCode) {
    return setErrorID(__FILE__, __FUNCTION__, __LINE__, errorCode);
  }

  data_ = tempData;

  return 0;
}

int ecmcPLCDataIF::writeEc() {
  int errorCode = writeDataValueDouble(ECMC_PLC_EC_ENTRY_INDEX, data_);

  if (errorCode) {
    return setErrorID(__FILE__, __FUNCTION__, __LINE__, errorCode);
  }
  return 0;
}

int ecmcPLCDataIF::readDs() {
  if (ds_ == NULL) {
    return ERROR_PLC_DATA_STORAGE_NULL;
  }

  int errorCode   = 0;
  double tempData = 0;

  switch (dataSourceDs_) {
  case ECMC_DATA_STORAGE_DATA_NONE:
    data_ = 0;
    break;

  case ECMC_DATA_STORAGE_DATA_APPEND:
    data_ = NAN;
    break;

  case ECMC_DATA_STORAGE_DATA_SIZE:
    data_ = ds_->getSize();
    break;

  case ECMC_DATA_STORAGE_DATA_INDEX:
    data_ = ds_->getCurrentIndex();
    break;

  case ECMC_DATA_STORAGE_DATA_ERROR:
    data_ = ds_->getErrorID();
    break;

  case ECMC_DATA_STORAGE_DATA_DATA:
    errorCode = ds_->getDataElement(&tempData);

    if (!errorCode) {
      data_ = tempData;
    }
    break;

  case ECMC_DATA_STORAGE_DATA_CLEAR:
    data_ = NAN;
    break;

  case ECMC_DATA_STORAGE_DATA_FULL:
    data_ = static_cast<double>(ds_->isStorageFull());
    break;

  default:
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_PLC_DATA_STORGAE_DATA_TYPE_ERROR);

    break;
  }

  return 0;
}

int ecmcPLCDataIF::writeDs() {
  if (ds_ == NULL) {
    return ERROR_PLC_DATA_STORAGE_NULL;
  }

  switch (dataSourceDs_) {
  case ECMC_DATA_STORAGE_DATA_NONE:
    data_ = 0;
    break;

  case ECMC_DATA_STORAGE_DATA_APPEND:

    // Check for NAN (only append new value)
    if (data_ == data_) {
      ds_->appendData(data_);
    }
    break;

  case ECMC_DATA_STORAGE_DATA_SIZE:
    ds_->setBufferSize(data_);
    break;

  case ECMC_DATA_STORAGE_DATA_INDEX:
    ds_->setCurrentPosition(data_);
    break;

  case ECMC_DATA_STORAGE_DATA_ERROR:
    ;
    break;

  case ECMC_DATA_STORAGE_DATA_DATA:
    ds_->setDataElement(data_);
    break;

  case ECMC_DATA_STORAGE_DATA_CLEAR:

    // Check for NAN (only append new value)
    if ((data_ == data_) && (data_ > 0)) {
      ds_->clearBuffer();
    }
    break;

  case ECMC_DATA_STORAGE_DATA_FULL:
    ;
    break;

  default:
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_PLC_DATA_STORGAE_DATA_TYPE_ERROR);

    break;
  }
  return 0;
}

int ecmcPLCDataIF::readAxis() {
  if (!axis_) return ERROR_PLC_AXIS_NULL;
  if (!axis_->getTraj()) return ERROR_PLC_TRAJ_NULL;
  int error = axis_->readDataSource(dataSourceAxis_, &data_);
  return error ? setErrorID(__FILE__, __FUNCTION__, __LINE__, ERROR_PLC_AXIS_DATA_TYPE_ERROR) : 0;
}

int ecmcPLCDataIF::writeAxis() {
  ecmcAxisBase * const axis = axis_;
  if (axis == NULL) {
    return ERROR_PLC_AXIS_NULL;
  }

  // Write from PLC to Axis allowed?
  if (!axis->getAllowCmdFromPLC() &&
      (dataSourceAxis_ != ECMC_AXIS_DATA_ALLOW_PLC_WRITE)) {
    return 0;
  }

  if (axis->getTraj() == NULL) {
    return ERROR_PLC_TRAJ_NULL;
  }

  if (axis->getMon() == NULL) {
    return ERROR_PLC_MON_NULL;
  }
  // Cache commonly used sub-objects once per PLC cycle call.
  auto * const mon = axis->getMon();
  auto * const seq = axis->getSeq();

  // Only write commands if changed
  switch (dataSourceAxis_) {
  case ECMC_AXIS_DATA_NONE:
    return 0;

    break;

  case ECMC_AXIS_DATA_AXIS_ID:
    return 0;

    break;

  case ECMC_AXIS_DATA_POS_SET:
    return axis->setExtSetPos(data_);

    break;

  case ECMC_AXIS_DATA_POS_ACT:
    return axis->setExtActPos(data_);

    break;

  case ECMC_AXIS_DATA_CNTRL_ERROR:
    return 0;

    break;

  case ECMC_AXIS_DATA_POS_TARGET:
    axis->setTargetPosAndCmd(data_);
    return 0;

    break;

  case ECMC_AXIS_DATA_POS_ERROR:
    return 0;

    break;

  case ECMC_AXIS_DATA_POS_RAW:
    return 0;

    break;

  case ECMC_AXIS_DATA_CNTRL_OUT:
    return 0;

    break;

  case ECMC_AXIS_DATA_VEL_SET:
    return 0;

    break;

  case ECMC_AXIS_DATA_VEL_ACT:
    return 0;

    break;

  case ECMC_AXIS_DATA_VEL_SET_FF_RAW:
    return 0;

    break;

  case ECMC_AXIS_DATA_VEL_SET_RAW:
    return 0;

    break;

  case ECMC_AXIS_DATA_CYCLE_COUNTER:
    return 0;

    break;

  case ECMC_AXIS_DATA_ERROR:
    return 0;

    break;

  case ECMC_AXIS_DATA_COMMAND:
    if (axis->getBlockCom()) {
      return axis->setExternalCommandBlockedError();
    }
    return axis->setCommand((motionCommandTypes)data_);

    break;

  case ECMC_AXIS_DATA_CMD_DATA:
    if (axis->getBlockCom()) {
      return axis->setExternalCommandBlockedError();
    }
    return axis->setCmdData(static_cast<int>(data_));

    break;

  case ECMC_AXIS_DATA_SEQ_STATE:
    return 0;

    break;

  case ECMC_AXIS_DATA_INTERLOCK_TYPE:
    return mon->setPLCInterlock(data_ == 0,
                                ECMC_PLC_INTERLOCK_DIR_BOTH);

    break;

  case ECMC_AXIS_DATA_TRAJ_SOURCE:

    if (data_ >= 1) {
      return seq->setTrajDataSourceType(ECMC_DATA_SOURCE_EXTERNAL);
    } else {
      return seq->setTrajDataSourceType(ECMC_DATA_SOURCE_INTERNAL);
    }

    break;

  case ECMC_AXIS_DATA_ENC_SOURCE:

    if (data_ >= 1) {
      return axis->setEncDataSourceType(ECMC_DATA_SOURCE_EXTERNAL);
    } else {
      return axis->setEncDataSourceType(ECMC_DATA_SOURCE_INTERNAL);
    }

    break;

  case ECMC_AXIS_DATA_ENABLE:
    if (axis->getBlockCom() && data_ >= 1) {
      return axis->setExternalCommandBlockedError();
    }
    return axis->setEnable(static_cast<bool>(data_));

    break;

  case ECMC_AXIS_DATA_ENABLED:
    return 0;

    break;

  case ECMC_AXIS_DATA_EXECUTE:
    if (axis->getBlockCom() && data_ >= 1) {
      return axis->setExternalCommandBlockedError();
    }
    return axis->setExecute(static_cast<bool>(data_));

    break;

  case ECMC_AXIS_DATA_BUSY:
    return 0;

    break;

  case ECMC_AXIS_DATA_AT_TARGET:
    return 0;

    break;

  case ECMC_AXIS_DATA_HOMED:
    axis->setAxisHomed(static_cast<bool>(data_));
    return 0;

    break;

  case ECMC_AXIS_DATA_LIMIT_BWD:
    mon->setLimitSwitchBwdPLCOverrideValue(data_>0);
    return 0;

    break;

  case ECMC_AXIS_DATA_LIMIT_FWD:
    mon->setLimitSwitchFwdPLCOverrideValue(data_>0);
  return 0;

    break;

  case ECMC_AXIS_DATA_HOME_SWITCH:
    mon->setHomeSwitchPLCOverrideValue(data_>0);
    return 0;

    break;

  case ECMC_AXIS_DATA_RESET:

    if (data_) {
      axis->errorReset();
    }
    return 0;

    break;

  case ECMC_AXIS_DATA_VEL_TARGET_SET:
    axis->setTargetVel(data_);
    break;

  case ECMC_AXIS_DATA_ACC_TARGET_SET:
    axis->setAcc(data_);
    break;

  case ECMC_AXIS_DATA_DEC_TARGET_SET:
    axis->setDec(data_);
    break;

  case ECMC_AXIS_DATA_SOFT_LIMIT_BWD:
    mon->setSoftLimitBwd(data_);
    break;

  case ECMC_AXIS_DATA_SOFT_LIMIT_FWD:
    mon->setSoftLimitFwd(data_);
    break;

  case ECMC_AXIS_DATA_SOFT_LIMIT_BWD_ENABLE:
    mon->setEnableSoftLimitBwd(static_cast<bool>(data_));
    break;

  case ECMC_AXIS_DATA_SOFT_LIMIT_FWD_ENABLE:
    mon->setEnableSoftLimitFwd(static_cast<bool>(data_));
    break;

  case ECMC_AXIS_DATA_TRAJ_DIRECTION:
    return 0;

    break;

  case ECMC_AXIS_DATA_ENC_HOMEPOS:
    seq->setHomePosition(data_);
    break;

  case ECMC_AXIS_DATA_BLOCK_COM:
    axis->setBlockCom(data_ >= 1);
    break;

  case ECMC_AXIS_DATA_INTERLOCK_BWD_TYPE:
    return mon->setPLCInterlock(data_ == 0,
                                ECMC_PLC_INTERLOCK_DIR_BWD);

    break;

  case ECMC_AXIS_DATA_INTERLOCK_FWD_TYPE:
    return mon->setPLCInterlock(data_ == 0,
                                ECMC_PLC_INTERLOCK_DIR_FWD);

    break;

  case ECMC_AXIS_DATA_ALLOW_PLC_WRITE:
    return axis->setAllowCmdFromPLC(data_ >= 1);

    break;

  case ECMC_AXIS_DATA_POS_SET_EXTERNAL:
    return axis->setExtSetPos(data_);

    break;

  case ECMC_AXIS_DATA_POS_ACT_EXTERNAL:
    return 0; // axis_->setExtActPos(data_);

    break;

  case ECMC_AXIS_DATA_CTRL_KP:

    if (axisHasController_) {
      axis->setCntrlKp(data_);
    }
    return 0;

    break;

  case ECMC_AXIS_DATA_CTRL_KI:

    if (axisHasController_) {
      axis->setCntrlKi(data_);
    }
    return 0;

    break;

  case ECMC_AXIS_DATA_CTRL_KD:

    if (axisHasController_) {
      axis->setCntrlKd(data_);
    }
    return 0;

    break;

  case ECMC_AXIS_DATA_CTRL_KFF:

    if (axisHasController_) {
      axis->setCntrlKff(data_);
    }
    return 0;

    break;

  default:
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_PLC_AXIS_DATA_TYPE_ERROR);

    break;
  }
  return 0;
}

ecmcDataStorageType ecmcPLCDataIF::parseDataStorageDataSource(
  char *axisDataSource) {
  char *varName;

  varName = strstr(axisDataSource, ECMC_PLC_DATA_STORAGE_STR);

  if (!varName) {
    return ECMC_DATA_STORAGE_DATA_NONE;
  }
  varName = strstr(axisDataSource, ".");

  if (!varName) {
    return ECMC_DATA_STORAGE_DATA_NONE;
  }
  varName++;

  int npos = strcmp(varName, ECMC_DATA_STORAGE_DATA_APPEND_STR);

  if (npos == 0) {
    return ECMC_DATA_STORAGE_DATA_APPEND;
  }

  npos = strcmp(varName, ECMC_DATA_STORAGE_DATA_INDEX_STR);

  if (npos == 0) {
    return ECMC_DATA_STORAGE_DATA_INDEX;
  }

  npos = strcmp(varName, ECMC_DATA_STORAGE_DATA_ERROR_STR);

  if (npos == 0) {
    return ECMC_DATA_STORAGE_DATA_ERROR;
  }

  npos = strcmp(varName, ECMC_DATA_STORAGE_DATA_SIZE_STR);

  if (npos == 0) {
    return ECMC_DATA_STORAGE_DATA_SIZE;
  }

  npos = strcmp(varName, ECMC_DATA_STORAGE_DATA_CLEAR_STR);

  if (npos == 0) {
    isBool_ = 1;
    return ECMC_DATA_STORAGE_DATA_CLEAR;
  }

  npos = strcmp(varName, ECMC_DATA_STORAGE_DATA_DATA_STR);

  if (npos == 0) {
    return ECMC_DATA_STORAGE_DATA_DATA;
  }

  npos = strcmp(varName, ECMC_DATA_STORAGE_DATA_FULL_STR);

  if (npos == 0) {
    isBool_ = 1;
    return ECMC_DATA_STORAGE_DATA_FULL;
  }

  return ECMC_DATA_STORAGE_DATA_NONE;
}

ecmcAxisDataType ecmcPLCDataIF::parseAxisDataSource(char *axisDataSource) {
  return ecmcAxisBase::parseDataSource(axisDataSource, &isBool_);
}

int ecmcPLCDataIF::parseAndLinkEcDataSource(char *ecDataSource) {
  int  masterId = 0;
  int  slaveId  = 0;
  int  bitId    = 0;
  char alias[EC_MAX_OBJECT_PATH_CHAR_LENGTH];
  int  errorCode =
    parseEcPath(ecDataSource, &masterId, &slaveId, alias, &bitId);

  if (errorCode) {
    ecmcRtLoggerLogError("%s/%s:%d: ERROR: Parse %s failed (0x%x).\n",
           __FILE__,
           __FUNCTION__,
           __LINE__,
           ecDataSource,
           errorCode);
    return setErrorID(__FILE__, __FUNCTION__, __LINE__, errorCode);
  }

  std::shared_ptr<ecmcDataAccess> entry;
  errorCode = ecmcResolveIoEntry(masterId, slaveId, alias, entry);
  if (errorCode) {
    if (errorCode == ERROR_MAIN_EC_INDEX_OUT_OF_RANGE) errorCode = ERROR_PLC_EC_MASTER_INVALID;
    else if (errorCode == ERROR_MAIN_EC_SLAVE_NULL) errorCode = ERROR_PLC_EC_SLAVE_NULL;
    else if (errorCode == ERROR_MAIN_EC_ENTRY_NULL) errorCode = ERROR_PLC_EC_ENTRY_NULL;
    return setErrorID(__FILE__, __FUNCTION__, __LINE__, errorCode);
  }
  if (slaveId < 0) {
    std::stringstream name;
    name << ECMC_EC_STR << masterId << "." ECMC_DUMMY_SLAVE_STR << -slaveId << "." << alias;
    if (bitId >= 0) name << "." << bitId;
    exprTkVarName_ = name.str();
  }
  errorCode = setDataAtIndex(entry, ECMC_PLC_EC_ENTRY_INDEX, bitId);

  if (errorCode) {
    return setErrorID(__FILE__, __FUNCTION__, __LINE__, errorCode);
  }

  errorCode = validateData(ECMC_PLC_EC_ENTRY_INDEX);

  if (errorCode) {
    return setErrorID(__FILE__, __FUNCTION__, __LINE__, errorCode);
  }

  // Print warning if data type is "NONE" , "U64" or "S64"
  ecmcEcDataType dt = getDataType(ECMC_PLC_EC_ENTRY_INDEX);

  if ((dt == ECMC_EC_NONE) || (dt == ECMC_EC_U64) || (dt == ECMC_EC_S64)) {
    ecmcRtLoggerLogWarning(
      "%s/%s:%d: WARNING: Entry %s is of type S64, U64 or undefined. PLC values are doubles and might not be able to represent the ethercat value correct.\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      ecDataSource);
  }

  return 0;
}

int ecmcPLCDataIF::parseEcPath(char *ecPath,
                               int  *master,
                               int  *slave,
                               char *alias,
                               int  *bit) {
  int masterId = 0;
  int slaveId  = 0;
  int nvals    = 0;

  nvals = sscanf(ecPath,
                 ECMC_EC_STR "%d." ECMC_SLAVE_CHAR "%d." ECMC_PLC_VAR_FORMAT,
                 &masterId,
                 &slaveId,
                 alias);

  if (nvals == 3) {
    *master = masterId;
    *slave  = slaveId;
    *bit    = -1;
    return 0;
  }
  return ERROR_PLC_EC_VAR_NAME_INVALID;
}

const char * ecmcPLCDataIF::getVarName() {
  return varName_.c_str();
}

const char * ecmcPLCDataIF::getExprTkVarName() {
  return exprTkVarName_.c_str();
}

int ecmcPLCDataIF::validate() {
  if (getErrorID()) {
    return getErrorID();
  }

  switch (source_) {
  case ECMC_RECORDER_SOURCE_NONE:
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_PLC_SOURCE_INVALID);

    break;

  case ECMC_RECORDER_SOURCE_ETHERCAT:

    if (!ec_) {
      return setErrorID(__FILE__, __FUNCTION__, __LINE__, ERROR_PLC_EC_NULL);
    } else {
      return 0;
    }
    break;

  case ECMC_RECORDER_SOURCE_AXIS:

    if (!axis_) {
      return setErrorID(__FILE__, __FUNCTION__, __LINE__, ERROR_PLC_AXIS_NULL);
    } else {
      axisHasController_ = axis_->getCntrl() != NULL;
    }
    return 0;

    break;

  case ECMC_RECORDER_SOURCE_STATIC_VAR:
    return 0;

    break;

  case ECMC_RECORDER_SOURCE_GLOBAL_VAR:
    return 0;

    break;

  case ECMC_RECORDER_SOURCE_DATA_STORAGE:

    if (!ds_) {
      return setErrorID(__FILE__,
                        __FUNCTION__,
                        __LINE__,
                        ERROR_PLC_DATA_STORAGE_NULL);
    } else {
      return 0;
    }
    break;
  }
  return ERROR_PLC_SOURCE_INVALID;
}

double ecmcPLCDataIF::getData() {
  return data_;
}

void ecmcPLCDataIF::setData(double data) {
  data_ = data;
}

int ecmcPLCDataIF::setReadOnly(int readOnly) {
  readOnly_ = readOnly;
  return 0;
}

int ecmcPLCDataIF::initAsyn() {
  char  buffer[EC_MAX_OBJECT_PATH_CHAR_LENGTH];
  char *name             = buffer;
  unsigned int charCount = 0;

  if (plcIndex_ >= 0 && plcIndex_ < ECMC_MAX_PLCS ) { // local variable (plc index)
    // "plc%d.%s"
    charCount = snprintf(buffer,
                         sizeof(buffer),
                         ECMC_PLCS_DATA_STR "." ECMC_PLC_DATA_STR "%d.%s",
                         plcIndex_,
                         varName_.c_str());
  } else if (plcIndex_ >= ECMC_MAX_PLCS) {  // Axis PLC
    // "ax%d.%s"
    charCount = snprintf(buffer,
                         sizeof(buffer),
                         ECMC_PLCS_DATA_STR "." ECMC_AX_STR "%d.%s",
                         plcIndex_-ECMC_MAX_PLCS,  // Axis index
                         varName_.c_str());
  } else { // global variable (no plc index)
    // "%s"
    charCount = snprintf(buffer,
                         sizeof(buffer),
                         ECMC_PLCS_DATA_STR ".%s",
                         varName_.c_str());
  }

  if (charCount >= sizeof(buffer) - 1) {
    ecmcRtLoggerLogError(
      "%s/%s:%d: ERROR: Failed to generate alias. Buffer too small (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      ERROR_ASYN_DATA_BUFFER_TO_SMALL);
    return setErrorID(ERROR_ASYN_DATA_BUFFER_TO_SMALL);
  }

  name          = buffer;
  asynDataItem_ = asynPortDriver_->addNewAvailParam(name,
                                                    asynParamFloat64,
                                                    (uint8_t *)&data_,
                                                    sizeof(data_),
                                                    ECMC_EC_F64,
                                                    sampleRateMs_,
                                                    0);

  if (!asynDataItem_) {
    ecmcRtLoggerLogError(
      "%s/%s:%d: ERROR: Add create default parameter for %s failed (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      name,
      ERROR_ASYN_CREATE_PARAM_FAIL);
    return setErrorID(ERROR_ASYN_CREATE_PARAM_FAIL);
  }

  asynDataItem_->setAllowWriteToEcmc(asynWriteAllow_);
  updateAsyn(1);
  asynPortDriver_->callParamCallbacks(ECMC_ASYN_DEFAULT_LIST,
                                      ECMC_ASYN_DEFAULT_ADDR);
  return 0;
}

int ecmcPLCDataIF::updateAsyn(int force) {
  if (asynDataItem_) {
    asynDataItem_->refreshParamRT(force);
  }
  return 0;
}

void ecmcPLCDataIF::errorReset() {
  ecmcDataLink::errorReset();
  ecmcError::errorReset();
}
