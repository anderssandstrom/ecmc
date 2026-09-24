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
