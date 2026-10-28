/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcGeneral.cpp
*
*  Created on: Jan 10, 2019
*      Author: anderssandstrom
*
\*************************************************************************/

#include "ecmcGeneral.h"


// TODO: REMOVE GLOBALS
#include "ecmcGlobalsExtern.h"
#include "ecmcAsynPortDriverUtils.h"
#include "ecmcOctetIF.h"        // Log Macros
#include "ecmcErrorsList.h"
#include "ecmcDefinitions.h"

namespace {
// Keep concrete pointer types: some classes hide ecmcError's accessors.
template <typename T, int Capacity>
struct ControllerErrorObjects {
  T *objects[Capacity] = {};
  int count = 0;

  void rebuild(T *const *source) {
    count = 0;
    for (int i = 0; i < Capacity; ++i) {
      if (source[i]) {
        objects[count++] = source[i];
      }
    }
  }
};

// Only the RT thread enables this snapshot. Configuration and command threads
// continue to inspect the live arrays. Like cyclic_task's execution lists,
// membership is fixed for one runtime entry, but error values are read live.
struct ControllerErrorObjectsRT {
  bool enabled = false;
  ControllerErrorObjects<ecmcDataStorage, ECMC_MAX_DATA_STORAGE_OBJECTS> storages;
  ControllerErrorObjects<ecmcAxisBase, ECMC_MAX_AXES> axes;
  ControllerErrorObjects<ecmcPluginLib, ECMC_MAX_PLUGINS> plugins;
  ControllerErrorObjects<ecmcCppLogicLib, ECMC_MAX_PLUGINS> cppLogics;
};

thread_local ControllerErrorObjectsRT errorObjectsRT;
}

void prepareControllerErrorObjectsRT() {
  errorObjectsRT.storages.rebuild(dataStorages);
  errorObjectsRT.axes.rebuild(axes);
  errorObjectsRT.plugins.rebuild(plugins);
  errorObjectsRT.cppLogics.rebuild(cppLogics);
  errorObjectsRT.enabled = true;
}

void clearControllerErrorObjectsRT() {
  errorObjectsRT.enabled = false;
}

int getControllerError() {
  // EtherCAT errors
  if (ec->getInitDone()) {
    if (ec->getError()) {
      return ec->getErrorID();
    }
  }

  // Data Storages
  auto *const *dataStoragesToCheck = errorObjectsRT.enabled ?
    errorObjectsRT.storages.objects : dataStorages;
  const int dataStoragesCount = errorObjectsRT.enabled ?
    errorObjectsRT.storages.count : ECMC_MAX_DATA_STORAGE_OBJECTS;
  for (int i = 0; i < dataStoragesCount; i++) {
    if (dataStoragesToCheck[i] != NULL) {
      if (dataStoragesToCheck[i]->getError()) {
        return dataStoragesToCheck[i]->getErrorID();
      }
    }
  }

  // PLC:s
  if (plcs != NULL) {
    if (plcs->getError()) {
      return plcs->getErrorID();
    }
  }

  // Axes
  auto *const *axesToCheck = errorObjectsRT.enabled ?
    errorObjectsRT.axes.objects : axes;
  const int axesCount = errorObjectsRT.enabled ?
    errorObjectsRT.axes.count : ECMC_MAX_AXES;
  for (int i = 0; i < axesCount; i++) {
    if (axesToCheck[i] != NULL) {
      if (axesToCheck[i]->getError()) {
        return axesToCheck[i]->getErrorID();
      }
    }
  }

  // PVT motion
  if(pvtCtrl_) {
    if (pvtCtrl_->getError()) {
      return pvtCtrl_->getErrorID();
    }
  }

  // Plugin objects
  auto *const *pluginsToCheck = errorObjectsRT.enabled ?
    errorObjectsRT.plugins.objects : plugins;
  const int pluginsCount = errorObjectsRT.enabled ?
    errorObjectsRT.plugins.count : ECMC_MAX_PLUGINS;
  for (int i = 0; i < pluginsCount; i++) {
    if (pluginsToCheck[i]) {
      if (pluginsToCheck[i]->getError()) {
        return pluginsToCheck[i]->getErrorID();
      }
    }
  }

  auto *const *cppLogicsToCheck = errorObjectsRT.enabled ?
    errorObjectsRT.cppLogics.objects : cppLogics;
  const int cppLogicsCount = errorObjectsRT.enabled ?
    errorObjectsRT.cppLogics.count : ECMC_MAX_PLUGINS;
  for (int i = 0; i < cppLogicsCount; i++) {
    if (cppLogicsToCheck[i]) {
      if (cppLogicsToCheck[i]->getError()) {
        return cppLogicsToCheck[i]->getErrorID();
      }
    }
  }

  // Plugin RTfunc retrun errors
  if (pluginsError) {
    return pluginsError;
  }

  if (cppLogicError) {
    return cppLogicError;
  }

  if (shmAccessError) {
    return shmAccessError;
  }

  return 0;
}

int controllerErrorReset() {
  LOGINFO4("%s/%s:%d\n", __FILE__, __FUNCTION__, __LINE__);
  int errorCodeBackup = getControllerError();

  // EtherCAT errors
  ec->errorReset();

  // Data Storages
  for (int i = 0; i < ECMC_MAX_DATA_STORAGE_OBJECTS; i++) {
    if (dataStorages[i] != NULL) {
      dataStorages[i]->errorReset();
    }
  }

  // Axes
  for (int i = 0; i < ECMC_MAX_AXES; i++) {
    if (axes[i] != NULL) {
      axes[i]->errorReset();
    }
  }

  // PVT motion
  if(pvtCtrl_) {
    pvtCtrl_->errorReset();
  }

  // Plugin objects
  for (int i = 0; i < ECMC_MAX_PLUGINS; i++) {
    if (plugins[i]) {
      plugins[i]->errorReset();
    }
  }

  for (int i = 0; i < ECMC_MAX_PLUGINS; i++) {
    if (cppLogics[i]) {
      cppLogics[i]->errorReset();
    }
  }

  // Plugin RTfunc retrun errors
  pluginsError = 0;
  cppLogicError = 0;
  shmAccessError = 0;

  // PLCs
  if (plcs) {
    plcs->errorReset();
  }

  if (errorCodeBackup != getControllerError()) {
    controllerErrorMsg = getErrorString(controllerError);
    mainAsynParams[ECMC_ASYN_MAIN_PAR_ERROR_ID_ID]->refreshParamRT(1);
    mainAsynParams[ECMC_ASYN_MAIN_PAR_ERROR_MSG_ID]->refreshParamRT(1,
                                                                    (uint8_t *)controllerErrorMsg,
                                                                    strlen(
                                                                      controllerErrorMsg));
  }

  return 0;
}

const char* getErrorString(int error_number) {
  LOGINFO4("%s/%s:%d error_no=%d\n",
           __FILE__,
           __FUNCTION__,
           __LINE__,
           error_number);

  return ecmcError::convertErrorIdToString(error_number);
}

int setEnableTimeDiag(int value) {
  LOGINFO4("%s/%s:%d enable=%d \n", __FILE__, __FUNCTION__, __LINE__, value);

  WRITE_DIAG_BIT(FUNCTION_TIMING_DIAGNOSTICS_BIT, value);

  return 0;
}

int setEnableFunctionCallDiag(int value) {
  LOGINFO4("%s/%s:%d value=%d\n", __FILE__, __FUNCTION__, __LINE__, value);

  WRITE_DIAG_BIT(FUNCTION_CALL_DIAGNOSTICS_BIT, value);

  return 0;
}

int linkEcEntryToObject(char *ecPath, char *objPath) {
  LOGINFO4("%s/%s:%d ecPath=%s axPath=%s\n",
           __FILE__,
           __FUNCTION__,
           __LINE__,
           ecPath,
           objPath);

  int  masterId   = -1;
  int  slaveIndex = -1;
  char alias[EC_MAX_OBJECT_PATH_CHAR_LENGTH];
  int  bitIndex = -1;

  int errorCode =
    parseEcPath(ecPath, &masterId, &slaveIndex, alias, &bitIndex);

  if (errorCode) {
    return errorCode;
  }

  // Get object type
  mainObjectType objType;
  int objIndex      = -1;
  int objFunctionId = 0;
  errorCode = getMainObjectType(objPath, &objIndex, &objType);

  if (errorCode) {
    return errorCode;
  }

  switch (objType) {
  case ECMC_OBJ_INVALID:
    return ERROR_MAIN_ECMC_LINK_INVALID;

    break;

  case ECMC_OBJ_AXIS:
    axisSubObjectType axSubObjType;
    errorCode = getAxSubObjectType(objPath, &axSubObjType);

    if (errorCode) {
      return errorCode;
    }

    switch (axSubObjType) {
    case ECMC_AX_SUB_OBJ_INVALID:
      return ERROR_MAIN_ECMC_LINK_INVALID;

      break;

    case ECMC_AX_SUB_OBJ_DRIVE:
      errorCode = getAxDriveFuncType(objPath, &objFunctionId);

      if (errorCode) {
        return errorCode;
      }
      return linkEcEntryToAxisDrv(slaveIndex,
                                  alias,
                                  objIndex,
                                  objFunctionId,
                                  bitIndex);

      break;

    case ECMC_AX_SUB_OBJ_ENCODER:
      errorCode = getAxEncFuncType(objPath, &objFunctionId);

      if (errorCode) {
        return errorCode;
      }
      return linkEcEntryToAxisEnc(slaveIndex,
                                  alias,
                                  objIndex,
                                  objFunctionId,
                                  bitIndex);

      break;

    case ECMC_AX_SUB_OBJ_MONITOR:
      errorCode = getAxMonFuncType(objPath, &objFunctionId);

      if (errorCode) {
        return errorCode;
      }
      return linkEcEntryToAxisMon(slaveIndex,
                                  alias,
                                  objIndex,
                                  objFunctionId,
                                  bitIndex);

      break;

    case ECMC_AX_SUB_OBJ_CONTROLLER:
      return ERROR_MAIN_ECMC_LINK_INVALID;

      break;

    case ECMC_AX_SUB_OBJ_TRAJECTORY:
      return ERROR_MAIN_ECMC_LINK_INVALID;

      break;

    case ECMC_AX_SUB_OBJ_MAIN:
      errorCode = getAxMainFuncType(objPath, &objFunctionId);

      if (errorCode) {
        return errorCode;
      }

      if (objFunctionId == ECMC_AXIS_ENTRY_INDEX_HEALTH) {
        return linkEcEntryToAxisStatusOutput(slaveIndex, alias, objIndex);
      }

      if (objFunctionId == ECMC_AXIS_ENTRY_MODE_SET) {
        return linkEcEntryToAxisSeqAutoModeSet(slaveIndex, alias, objIndex);
      }

      if (objFunctionId == ECMC_AXIS_ENTRY_MODE_ACT) {
        return linkEcEntryToAxisSeqAutoModeAct(slaveIndex, alias, objIndex);
      }

      return ERROR_MAIN_ECMC_LINK_INVALID;

      break;
    }
    break;

  case ECMC_OBJ_EC:
    errorCode = getEcMainFuncType(objPath, &objFunctionId);

    if (errorCode) {
      return errorCode;
    }

    if (objFunctionId == ECMC_EC_ENTRY_INDEX_HEALTH) {
      return linkEcEntryToEcStatusOutput(slaveIndex, alias);
    }
    return ERROR_MAIN_ECMC_LINK_INVALID;

    break;

  case ECMC_OBJ_DS:
    return ERROR_MAIN_ECMC_LINK_INVALID;

    break;

  case ECMC_OBJ_MAIN:
    return ERROR_MAIN_ECMC_LINK_INVALID;

    break;

  case ECMC_OBJ_THREAD:
    return ERROR_MAIN_ECMC_LINK_INVALID;

    break;
  
  case ECMC_OBJ_PVT:
    
    errorCode = getPVTCtrlFuncType(objPath, &objFunctionId);
    if (errorCode) {
      return errorCode;
    }

    if (objFunctionId == ECMC_EC_ENTRY_INDEX_HEALTH) {
      return linkEcEntryToPVTController(slaveIndex,
                                        alias,
                                        objFunctionId,
                                        bitIndex);
    }
    break;

  }



  return ERROR_MAIN_ECMC_LINK_INVALID;
}

namespace {
int resolveDataInputTarget(char *path, int *axisIndex, ecmcDataLink **target, int *slot) {
  mainObjectType type;
  int error = getMainObjectType(path, axisIndex, &type);
  if (error) return error;
  if (type != ECMC_OBJ_AXIS || *axisIndex < 0 || *axisIndex >= ECMC_MAX_AXES || !axes[*axisIndex])
    return ERROR_MAIN_ECMC_LINK_INVALID;
  axisSubObjectType sub;
  error = getAxSubObjectType(path, &sub);
  if (error) return error;
  auto *axis = axes[*axisIndex];
  if (sub == ECMC_AX_SUB_OBJ_ENCODER) {
    error = getAxEncFuncType(path, slot);
    if (error) return error;
    // Input slots only; commands/control outputs must retain their handlers.
    if (*slot == ECMC_ENCODER_ENTRY_INDEX_LATCH_CONTROL ||
        *slot == ECMC_ENCODER_ENTRY_INDEX_TOUCH_PROBE_CONTROL ||
        *slot == ECMC_ENCODER_ENTRY_INDEX_RESET || *slot == ECMC_ENCODER_ENTRY_INDEX_TRIGG_HOME)
      return ERROR_MAIN_ECMC_LINK_INVALID;
    *target = axis->getConfigEnc();
  } else if (sub == ECMC_AX_SUB_OBJ_MONITOR) {
    error = getAxMonFuncType(path, slot);
    if (error) return error;
    *target = axis->getMon();
  } else if (sub == ECMC_AX_SUB_OBJ_DRIVE) {
    error = getAxDriveFuncType(path, slot);
    if (error) return error;
    if (*slot != ECMC_DRIVEBASE_ENTRY_INDEX_STATUS_WORD &&
        *slot != ECMC_DRIVEBASE_ENTRY_INDEX_WARNING &&
        (*slot < ECMC_DRIVEBASE_ENTRY_INDEX_ALARM_0 || *slot > ECMC_DRIVEBASE_ENTRY_INDEX_ALARM_2))
      return ERROR_MAIN_ECMC_LINK_INVALID;
    *target = axis->getDrv();
  } else return ERROR_MAIN_ECMC_LINK_INVALID;
  return *target ? 0 : ERROR_MAIN_ECMC_LINK_INVALID;
}
}

int linkPLCDataToObject(int producerPLC, const char *source, char *destination, int conversion) {
  if (appModeCmd != ECMC_MODE_CONFIG || appModeStat != ECMC_MODE_CONFIG)
    return ERROR_MAIN_APP_MODE_ALREADY_RUNTIME;
  if (!plcs || !destination || !source || (conversion < 0 || conversion > 2))
    return ERROR_MAIN_ECMC_LINK_INVALID;
  int axisIndex = -1, slot = -1;
  ecmcDataLink *target = nullptr;
  int error = resolveDataInputTarget(destination, &axisIndex, &target, &slot);
  if (error) return error;
  axisSubObjectType sub;
  error = getAxSubObjectType(destination, &sub);
  if (error) return error;
  const bool numericInput =
    (sub == ECMC_AX_SUB_OBJ_ENCODER &&
      (slot == ECMC_ENCODER_ENTRY_INDEX_ACTUAL_POSITION ||
       slot == ECMC_ENCODER_ENTRY_INDEX_LATCH_VALUE ||
       slot == ECMC_ENCODER_ENTRY_INDEX_TOUCH_PROBE_VALUE)) ||
    (sub == ECMC_AX_SUB_OBJ_MONITOR && slot == ECMC_MON_ENTRY_INDEX_ANALOG);
  // PLC scalars are doubles. Require an explicit conversion for integer/bit
  // destinations instead of accepting a link that would fail on its first read.
  if (!numericInput && conversion == 0) return ERROR_MAIN_ECMC_LINK_INVALID;
  error = plcs->setAxisArrayPointer(axes[axisIndex], axisIndex);
  if (error) return error;
  return plcs->linkDataInput(producerPLC, source, axisIndex, target, slot, conversion);
}

int linkDataToObject(const char *source, char *destination, int conversion) {
  if (appModeCmd != ECMC_MODE_CONFIG || appModeStat != ECMC_MODE_CONFIG)
    return ERROR_MAIN_APP_MODE_ALREADY_RUNTIME;
  if (!plcs || !source || !destination || conversion < 0 || conversion > 2)
    return ERROR_MAIN_ECMC_LINK_INVALID;
  int sourceAxis = -1, axisIndex = -1, slot = -1;
  mainObjectType type;
  // The existing parser validates the complete path and axis index.
  int error = getMainObjectType(const_cast<char *>(source), &sourceAxis, &type);
  if (error) return error;
  if (type != ECMC_OBJ_AXIS || sourceAxis < 0 || sourceAxis >= ECMC_MAX_AXES || !axes[sourceAxis])
    return ERROR_MAIN_ECMC_LINK_INVALID;
  ecmcDataLink *target = nullptr;
  error = resolveDataInputTarget(destination, &axisIndex, &target, &slot);
  if (error) return error;
  axisSubObjectType sub;
  error = getAxSubObjectType(destination, &sub);
  if (error) return error;
  const bool numericInput =
    (sub == ECMC_AX_SUB_OBJ_ENCODER &&
      (slot == ECMC_ENCODER_ENTRY_INDEX_ACTUAL_POSITION ||
       slot == ECMC_ENCODER_ENTRY_INDEX_LATCH_VALUE || slot == ECMC_ENCODER_ENTRY_INDEX_TOUCH_PROBE_VALUE)) ||
    (sub == ECMC_AX_SUB_OBJ_MONITOR && slot == ECMC_MON_ENTRY_INDEX_ANALOG);
  // Preserve explicit numeric conversion policy for non-Boolean axis values.
  int boolean = 0;
  const auto sourceType = ecmcAxisBase::parseDataSource(source, &boolean);
  if (sourceType == ECMC_AXIS_DATA_NONE)
    return ERROR_MAIN_ECMC_LINK_INVALID;
  // Compare canonical signal names, not whole axes. Different signals on the
  // same axis and reciprocal cross-axis links retain normal execution order.
  if (sourceAxis == axisIndex && sourceType == ecmcAxisBase::parseDataSource(destination))
    return ERROR_PLC_SOURCE_INVALID;
  if (!numericInput && !boolean && conversion == 0) return ERROR_MAIN_ECMC_LINK_INVALID;
  error = plcs->setAxisArrayPointer(axes[sourceAxis], sourceAxis);
  if (error) return error;
  error = plcs->setAxisArrayPointer(axes[axisIndex], axisIndex);
  return error ? error : plcs->linkAxisDataInput(sourceAxis, source, axisIndex, target, slot, conversion);
}

int unlinkDataFromObject(char *destination) {
  if (appModeCmd != ECMC_MODE_CONFIG || appModeStat != ECMC_MODE_CONFIG)
    return ERROR_MAIN_APP_MODE_ALREADY_RUNTIME;
  if (!plcs || !destination) return ERROR_MAIN_ECMC_LINK_INVALID;
  int axisIndex = -1, slot = -1;
  ecmcDataLink *target = nullptr;
  int error = resolveDataInputTarget(destination, &axisIndex, &target, &slot);
  return error ? error : plcs->unlinkDataInput(target, slot);
}
