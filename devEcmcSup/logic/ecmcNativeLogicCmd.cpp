/*************************************************************************\
* Copyright (c) 2026 Paul Scherrer Institute
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcNativeLogicCmd.cpp
*
\*************************************************************************/

#include "ecmcNativeLogicCmd.h"

#include "ecmcNativeLogicLib.h"
#include "ecmcDefinitions.h"
#include "ecmcErrorsList.h"
#include "ecmcGlobalsExtern.h"
#include "ecmcOctetIF.h"

int loadNativeLogic(int logicId, const char* filenameWP, const char* configStr) {
  LOGINFO4("%s/%s:%d logicId=%d filenameWP=%s configStr=%s\n",
           __FILE__,
           __FUNCTION__,
           __LINE__,
           logicId,
           filenameWP ? filenameWP : "(null)",
           configStr ? configStr : "(null)");

  if ((logicId < 0) || (logicId >= ECMC_MAX_PLUGINS)) {
    return ERROR_MAIN_NATIVE_LOGIC_INDEX_OUT_OF_RANGE;
  }

  if (nativeLogics[logicId]) {
    delete nativeLogics[logicId];
    nativeLogics[logicId] = NULL;
  }

  nativeLogics[logicId] = new ecmcNativeLogicLib(logicId);

  if (!nativeLogics[logicId]) {
    return ERROR_MAIN_NATIVE_LOGIC_OBJECT_NULL;
  }

  int errorCode = nativeLogics[logicId]->load(filenameWP, configStr ? configStr : "");
  if (errorCode) {
    delete nativeLogics[logicId];
    nativeLogics[logicId] = NULL;
    return errorCode;
  }

  return 0;
}

int reportNativeLogic(int logicId) {
  LOGINFO4("%s/%s:%d logicId=%d\n",
           __FILE__,
           __FUNCTION__,
           __LINE__,
           logicId);

  if ((logicId < 0) || (logicId >= ECMC_MAX_PLUGINS)) {
    return ERROR_MAIN_NATIVE_LOGIC_INDEX_OUT_OF_RANGE;
  }

  if (!nativeLogics[logicId]) {
    return ERROR_MAIN_NATIVE_LOGIC_OBJECT_NULL;
  }

  nativeLogics[logicId]->report();
  return 0;
}

int loadCppLogic(int logicId, const char* filenameWP, const char* configStr) {
  return loadNativeLogic(logicId, filenameWP, configStr);
}

int reportCppLogic(int logicId) {
  return reportNativeLogic(logicId);
}
