/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcAsynPortDriver.cpp
*
*  Created on: Jan 29, 2019
*      Author: anderssandstrom
*
\*************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <string>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>
#include <iocsh.h>

#include <epicsTypes.h>
#include <epicsTime.h>
#include <epicsThread.h>
#include <epicsString.h>
#include <epicsTimer.h>
#include <epicsMutex.h>
#include <epicsExport.h>
#include <epicsEvent.h>
#include <envDefs.h>
#include <dbCommon.h>
#include <dbBase.h>
#include <dbStaticLib.h>
#include <dbAccess.h>
#include <initHooks.h>
#include <alarm.h>

#include "ecmcAsynPortDriver.h"
#include "ecmcOctetIF.h"
#include "ecmcCmdParser.h"
#include "gitversion.h"
#include "ecmcAsynPortDriverUtils.h"

#include "ecmcMainThread.h"
#include "ecmcEthercat.h"
#include "ecmcGeneral.h"
#include "ecmcCom.h"

#include "exprtkWrap.h"  // Other module

static const char *driverName = "ecmcAsynPortDriver";

extern double mcuFrequency;
extern double mcuPeriod;

static int allowCallbackEpicsState         = 0;
static initHookState currentEpicsState     = initHookAtIocBuild;
static ecmcAsynPortDriver *ecmcAsynPortObj = NULL;

/** Callback hook for EPICS state.
 * \param[in] state EPICS state
 * \return void
 * Will be called be the EPICS framework with the current EPICS state as it changes.
 */
static void getEpicsState(initHookState state) {
  const char *functionName = "getEpicsState";

  if (!ecmcAsynPortObj) {
    printf("%s:%s: ERROR: ecmcAsynPortObj==NULL\n", driverName, functionName);
    return;
  }

  // asynUser *asynTraceUser = ecmcAsynPortObj->getTraceAsynUser();
  ecmcAsynPortObj->setEpicsState(state);

  switch (state) {
  // case initHookAfterScanInit:
  case initHookAfterIocRunning:
    allowCallbackEpicsState = 1;
    ecmcAsynPortObj->calcFastestUpdateRate();

    /** Make all callbacks if data arrived from callback before interrupts
      were registered (before allowCallbackEpicsState==1)
      */
    ecmcAsynPortObj->refreshAllInUseParamsRT();

    break;

  default:
    break;
  }

  currentEpicsState = state;

  /*asynPrint(asynTraceUser,
            ASYN_TRACE_INFO,
            "%s:%s: EPICS state: %s (%d). Allow callbacks: %s.\n",
            driverName,
            functionName,
            epicsStateToString((int)state),
            (int)state,
            ecmcAsynPortObj->getAllowRtThreadCom() ? "true" : "false");*/
}

/** Register EPICS hook function
 * \return void
 */
int initHook(void) {
  return initHookRegister(getEpicsState);
}

/** Constructor for the ecmcAsynPortDriver class.
  * Calls constructor for the asynPortDriver base class.
  * \param[in] portName The name of the asyn port driver to be created. */
ecmcAsynPortDriver::ecmcAsynPortDriver(
  const char *portName /*, int maxPoints*/,
  int         paramTableSize,
  int         autoConnect,
  int         priority,
  double      defaultSampleRateMS)
  : asynPortDriver(portName,
                   1,

                   /* maxAddr */
                   asynInt32Mask | asynFloat64Mask | asynFloat32ArrayMask |
                   asynFloat64ArrayMask | asynEnumMask | asynDrvUserMask |
                   asynOctetMask | asynInt8ArrayMask | asynInt16ArrayMask |
                   asynInt32ArrayMask | asynUInt32DigitalMask
#ifdef ECMC_ASYN_ASYNPARAMINT64
                   | asynInt64Mask | asynInt64ArrayMask
#endif //ECMC_ASYN_ASYNPARAMINT64
                   ,

                   /* Interface mask */
                   asynInt32Mask | asynFloat64Mask | asynFloat32ArrayMask |
                   asynFloat64ArrayMask | asynEnumMask | asynDrvUserMask |
                   asynOctetMask | asynInt8ArrayMask | asynInt16ArrayMask |
                   asynInt32ArrayMask | asynUInt32DigitalMask
#ifdef ECMC_ASYN_ASYNPARAMINT64
                   | asynInt64Mask | asynInt64ArrayMask
#endif //ECMC_ASYN_ASYNPARAMINT64
                   ,

                   /* Interrupt mask */
                   ASYN_CANBLOCK,  /*NOT ASYN_MULTI_DEVICE*/
                   autoConnect,

                   /* Autoconnect */
                   priority,

                   /* Default priority */
                   ECMC_STACK_SIZE) {
  initVars();

  const char *functionName = "ecmcAsynPortDriver";
  allowRtThreadCom_     = 1; // Allow at startup (RT thread not started)
  pEcmcParamInUseArray_ = new ecmcAsynDataItem *[paramTableSize];
  pEcmcParamAvailArray_ = new ecmcAsynDataItem *[paramTableSize];

  for (int i = 0; i < paramTableSize; i++) {
    pEcmcParamInUseArray_[i] = NULL;
    pEcmcParamAvailArray_[i] = NULL;
  }
  paramTableSize_           = paramTableSize;
  autoConnect_              = autoConnect;
  priority_                 = priority;
  defaultSampleTimeMS_      = defaultSampleRateMS;
  fastestParamUpdateCycles_ =
    (int32_t)(defaultSampleRateMS / 1000.0 * mcuFrequency);

  /* If paramTableSize_==1 then only stream device or motor record
  can use the driver through the "default access" param below.
  */
  if (paramTableSize_ < 1) {
    asynPrint(pasynUserSelf,
              ASYN_TRACE_ERROR,
              "%s:%s: Param table size to small: %d\n",
              driverName,
              functionName,
              paramTableSize_);
    exit(1);
    return;
  }

  // Add first param for other access (like motor record or stream device).
  ecmcAsynDataItem *paramTemp = new ecmcAsynDataItem(this,
                                                     ECMC_ASYN_PAR_OCTET_NAME,
                                                     asynParamNotDefined,
                                                     ECMC_EC_NONE,
                                                     mcuPeriod / 1e6);

  if (paramTemp->createParam()) {
    asynPrint(pasynUserSelf,
              ASYN_TRACE_ERROR,
              "%s:%s: createParam for %s failed.\n",
              driverName,
              functionName,
              ECMC_ASYN_PAR_OCTET_NAME);
    delete paramTemp;
    paramTemp = NULL;
    exit(1);
  }

  if (appendAvailParam(paramTemp, 1) != asynSuccess) {
    asynPrint(pasynUserSelf,
              ASYN_TRACE_ERROR,
              "%s:%s: Append asyn octet param %s failed.\n",
              driverName,
              functionName,
              ECMC_ASYN_PAR_OCTET_NAME);
    delete paramTemp;
    paramTemp = NULL;
    exit(1);
  }

  if (appendInUseParam(paramTemp, 1) != asynSuccess) {
    asynPrint(pasynUserSelf,
              ASYN_TRACE_ERROR,
              "%s:%s: Append asyn octet param %s failed.\n",
              driverName,
              functionName,
              ECMC_ASYN_PAR_OCTET_NAME);
    delete paramTemp;
    paramTemp = NULL;
    exit(1);
  }

  int errorCode = ecmcInit((void *)this);

  if (errorCode) {
    asynPrint(pasynUserSelf,
              ASYN_TRACE_ERROR,
              "ecmcAsynPortDriverConfigure: ERROR: Init ECMC failed (0x%x).\n",
              errorCode);
    exit(1);
  }
}

ecmcAsynPortDriver::~ecmcAsynPortDriver() {
  delete pEcmcParamInUseArray_;
  pEcmcParamInUseArray_ = NULL;
  delete pEcmcParamAvailArray_;
  pEcmcParamAvailArray_ = NULL;

  // ecmcCleanup(1);
}

/**
 * Initiate variables
 */
void ecmcAsynPortDriver::initVars() {
  allowRtThreadCom_      = 0;
  pEcmcParamInUseArray_  = NULL;
  pEcmcParamAvailArray_  = NULL;
  ecmcParamInUseCount_   = 0;
  ecmcParamAvailCount_   = 0;
  paramTableSize_        = 0;
  defaultSampleTimeMS_   = 0;
  defaultMaxDelayTimeMS_ = 0;
  defaultTimeSource_     = ECMC_TIME_BASE_ECMC;
  autoConnect_           = 0;
  priority_              = 0;
  epicsState_            = 0;
}

int ecmcAsynPortDriver::getEpicsState() {
  return epicsState_;
}

void ecmcAsynPortDriver::setEpicsState(int state) {
  epicsState_ = state;
}

/** Append parameter to in-use list\n
 * All parameters that are linked to records are found in this list\n
 *
  * \param[in] dataItem Parameter to add\n
  * \param[in] dieIfFail Exit if add fails\n
  *
  * returns asynError or asunSuccess
  * */
asynStatus ecmcAsynPortDriver::appendInUseParam(ecmcAsynDataItem *dataItem,
                                                bool              dieIfFail) {
  const char *functionName = "appendInUseParam";

  if (!dataItem) {
    asynPrint(pasynUserSelf,
              ASYN_TRACE_ERROR,
              "%s:%s: Error: DataItem NULL.",
              driverName,
              functionName);

    if (dieIfFail) {
      exit(1);
    }
    return asynError;
  }

  if (ecmcParamInUseCount_ >= (paramTableSize_ - 1)) {
    asynPrint(pasynUserSelf,
              ASYN_TRACE_ERROR,
              "%s:%s: Parameter table full. Parameter with name %s will be discarded.",
              driverName,
              functionName,
              dataItem->getParamInfo()->name);

    if (dieIfFail) {
      exit(1);
    }
    return asynError;
  }
  pEcmcParamInUseArray_[ecmcParamInUseCount_] = dataItem;
  ecmcParamInUseCount_++;
  return asynSuccess;
}

/** Append parameter to list of availabe parameters
  * \param[in] dataItem Parameter to add
  * \param[in] dieIfFail Exit if add fails.
  *
  * returns asynError or asunSuccess
  * */
asynStatus ecmcAsynPortDriver::appendAvailParam(ecmcAsynDataItem *dataItem,
                                                bool              dieIfFail) {
  const char *functionName = "appendAvailParam";

  if (!dataItem) {
    asynPrint(pasynUserSelf,
              ASYN_TRACE_ERROR,
              "%s:%s: Error: DataItem NULL.\n",
              driverName,
              functionName);

    if (dieIfFail) {
      exit(1);
    }
    return asynError;
  }

  if (ecmcParamAvailCount_ >= (paramTableSize_ - 1)) {
    asynPrint(pasynUserSelf,
              ASYN_TRACE_ERROR,
              "%s:%s: ERROR: Parameter table full (available params).\n"
              "Parameter with name %s will be discarded (max params = %d).\n"
              "Increase paramtable size in call to ecmcAsynPortDriverConfigure().\n",
              driverName,
              functionName,
              dataItem->getParamInfo()->name,
              paramTableSize_);

    if (dieIfFail) {
      exit(1);
    }
    return asynError;
  }
  pEcmcParamAvailArray_[ecmcParamAvailCount_] = dataItem;
  ecmcParamAvailCount_++;
  return asynSuccess;
}

/** Find parameter in list of available parameters by name\n
  * \param[in] name Parameter name\n
  *
  * returns parameter of type ecmcAsynDataItem if found otherwise NULL\n
  * */
ecmcAsynDataItem * ecmcAsynPortDriver::findAvailParam(const char *name) {
  // const char* functionName = "findAvailParam";
  for (int i = 0; i < ecmcParamAvailCount_; i++) {
    if (pEcmcParamAvailArray_[i]) {
      if (strcmp(pEcmcParamAvailArray_[i]->getParamName(), name) == 0) {
        return pEcmcParamAvailArray_[i];
      }
    }
  }
  return NULL;
}

/** Find emcDataItem in list by name\n
  * \param[in] name Parameter name\n
  *
  * returns ecmcDataItem if found otherwise NULL\n
  * \Note: Very similar to findAvailParam but returns baseclass instead
  **/
ecmcDataItem * ecmcAsynPortDriver::findAvailDataItem(const char *name) {
  // const char* functionName = "findAvailParam";
  for (int i = 0; i < ecmcParamAvailCount_; i++) {
    if (pEcmcParamAvailArray_[i]) {
      if (strcmp(pEcmcParamAvailArray_[i]->getName(), name) == 0) {
        return (ecmcDataItem *)pEcmcParamAvailArray_[i];
      }
    }
  }
  return NULL;
}

ecmcAsynDataItem * ecmcAsynPortDriver::addNewAvailParam(const char    *name,
                                                        asynParamType  type,
                                                        uint8_t       *data,
                                                        size_t         bytes,
                                                        ecmcEcDataType dt,
                                                        bool           dieIfFail)
{
  return addNewAvailParam(name,
                          type,
                          data,
                          bytes,
                          dt,
                          -1,
                          dieIfFail);
}

bool ecmcAsynPortDriver::checkParamExist(const char *name) {
  for (int i = 0; i < ecmcParamAvailCount_; i++) {
    if (!pEcmcParamAvailArray_[i]) {
      return 0;
    }

    if ((strstr(name, pEcmcParamAvailArray_[i]->getParamName()) != NULL) &&
        (strlen(name) == strlen(pEcmcParamAvailArray_[i]->getParamName()))) {
      return true;
    }
  }
  return false;
}

/** Create and add new parameter to list of available parameters\n
  * \param[in] name Parameter name\n
  * \param[in] type Asyn parameter type\n
  * \param[in] dt   Data type\n
  * \param[in] data Pointer to data\n
  * \param[in] bytes size of data\n
  * \param[in] sampleTimeMs parameter update rate
  * \param[in] dieIfFail Exit if method fails\n
  *
  * returns parameter (ecmcAsynDataItem)\n
  * */
ecmcAsynDataItem * ecmcAsynPortDriver::addNewAvailParam(const char    *name,
                                                        asynParamType  type,
                                                        uint8_t       *data,
                                                        size_t         bytes,
                                                        ecmcEcDataType dt,
                                                        double         sampleTimeMs,
                                                        bool           dieIfFail)
{
  const char *functionName = "addNewAvailParam";

  // make sure param name is unique
  if (checkParamExist(name)) {
    asynPrint(pasynUserSelf,
              ASYN_TRACE_ERROR,
              "%s:%s: ERROR: Parameter name (%s) already exists, must be unique.\n",
              driverName,
              functionName,
              name);

    if (dieIfFail) {
      exit(1);
    }
    return NULL;
  }

  ecmcAsynDataItem *paramTemp = new ecmcAsynDataItem(this,
                                                     name,
                                                     type,
                                                     dt,
                                                     sampleTimeMs);

  int errorCode = paramTemp->setEcmcDataPointer(data, bytes);

  if (errorCode) {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
              "%s:%s: ERROR: Set data pointer to asyn parameter %s failed.\n",
              driverName,
              functionName,
              name);
    delete paramTemp;
    paramTemp = NULL;

    if (dieIfFail) {
      exit(1);
    }

    return NULL;
  }
  asynStatus status = appendAvailParam(paramTemp, 0);

  if (status != asynSuccess) {
    /*asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
               "%s:%s: ERROR: Append asyn parameter %s to list failed.\n",
               driverName,
               functionName,
               name);*/
    delete paramTemp;
    paramTemp = NULL;

    if (dieIfFail) {
      exit(1);
    }
    return NULL;
  }

  return paramTemp;
}

asynStatus ecmcAsynPortDriver::readOctet(asynUser *pasynUser,
                                         char     *value,
                                         size_t    maxChars,
                                         size_t   *nActual,
                                         int      *eomReason) {
  size_t thisRead   = 0;
  int    reason     = 0;
  asynStatus status = asynSuccess;

  // int function = pasynUser->reason;

  /*
   * Feed what writeIt() gave us into the MCU
   */

  *value = '\0';

  // lock();
  if (CMDreadIt(value, maxChars)) status = asynError;

  if (status == asynSuccess) {
    thisRead = strlen(value);
    *nActual = thisRead;

    /* May be not enough space ? */

    if (thisRead > maxChars - 1) {
      reason |= ASYN_EOM_CNT;
    } else {
      reason |= ASYN_EOM_EOS;
    }

    if ((thisRead == 0) && (pasynUser->timeout == 0)) {
      status = asynTimeout;
    }
  } else {
    asynPrint(pasynUser, ASYN_TRACE_ERROR,
              "%s FAIL in ecmcAsynPortDriver::readOctet.\n", portName);
  }

  if (eomReason) *eomReason = reason;

  *nActual = thisRead;
  asynPrint(pasynUser, ASYN_TRACE_FLOW,
            "%s thisRead=%zu data=\"%s\"\n",
            portName,
            thisRead,
            value);

  // unlock();

  return status;
}

asynStatus ecmcAsynPortDriver::writeOctet(asynUser   *pasynUser,
                                          const char *value,
                                          size_t      maxChars,
                                          size_t     *nActual) {
  // int function = pasynUser->reason;
  size_t thisWrite  = 0;
  asynStatus status = asynError;

  asynPrint(pasynUser,
            ASYN_TRACE_FLOW,
            "%s write.\n",
            portName);
  asynPrintIO(pasynUser,
              ASYN_TRACEIO_DRIVER,
              value,
              maxChars,
              "%s write %zu\n",
              portName,
              maxChars);
  *nActual = 0;

  if (maxChars == 0) {
    return asynSuccess;
  }

  // lock();
  if (!(CMDwriteIt(value, maxChars))) {
    thisWrite = maxChars;
    *nActual  = thisWrite;
    status    = asynSuccess;
  }

  // unlock();
  asynPrint(pasynUser,
            ASYN_TRACE_FLOW,
            "%s wrote %zu return %s.\n",
            portName,
            *nActual,
            pasynManager->strStatus(status));
  return status;
}

/**
 * Ensure name and id is valid for parameter at reads or writes\n
 * \param[in] paramIndex Index of parameter.\n
 * \param[in] functionName Name of calling function.\n
 *
 * \return asynSuccess or asynError.
 */
asynStatus ecmcAsynPortDriver::checkParamNameAndId(int         paramIndex,
                                                   const char *functionName) {
  const char *paramName;

  /* Fetch the parameter string name for possible use in debugging */
  getParamName(paramIndex, &paramName);

  // Check object
  if (!pEcmcParamInUseArray_[paramIndex]) {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR,
              "%s:%s: Error: Parameter object NULL for function %d (%s).\n",
              driverName, functionName, paramIndex, paramName);
    return asynError;
  }

  // Check name
  if (strcmp(paramName,
             pEcmcParamInUseArray_[paramIndex]->getParamName()) != 0) {
    asynPrint(pasynUserSelf,
              ASYN_TRACE_ERROR,
              "%s:%s: Error: Parameter name missmatch for function %d (%s != %s).\n",
              driverName,
              functionName,
              paramIndex,
              paramName,
              pEcmcParamInUseArray_[paramIndex]->getParamName());
    return asynError;
  }

  return asynSuccess;
}

asynStatus ecmcAsynPortDriver::writeInt32(asynUser  *pasynUser,
                                          epicsInt32 value) {
  int function             = pasynUser->reason;
  const char *functionName = "writeInt32";

  if (checkParamNameAndId(function, functionName) != asynSuccess) {
    return asynError;
  }

  return pEcmcParamInUseArray_[function]->writeInt32(value);
}

asynStatus ecmcAsynPortDriver::readInt32(asynUser   *pasynUser,
                                         epicsInt32 *value) {
  int function             = pasynUser->reason;
  const char *functionName = "readInt32";

  if (checkParamNameAndId(function, functionName) != asynSuccess) {
    return asynError;
  }

  return pEcmcParamInUseArray_[function]->readInt32(value);
}

asynStatus ecmcAsynPortDriver::writeUInt32Digital(asynUser   *pasynUser,
                                                  epicsUInt32 value,
                                                  epicsUInt32 mask) {
  int function             = pasynUser->reason;
  const char *functionName = "writeUInt32Digital";

  if (checkParamNameAndId(function, functionName) != asynSuccess) {
    return asynError;
  }

  return pEcmcParamInUseArray_[function]->writeUInt32Digital(value, mask);
}

asynStatus ecmcAsynPortDriver::readUInt32Digital(asynUser    *pasynUser,
                                                 epicsUInt32 *value,
                                                 epicsUInt32  mask) {
  int function             = pasynUser->reason;
  const char *functionName = "readUInt32Digital";

  if (checkParamNameAndId(function, functionName) != asynSuccess) {
    return asynError;
  }

  return pEcmcParamInUseArray_[function]->readUInt32Digital(value, mask);
}

asynStatus ecmcAsynPortDriver::writeFloat64(asynUser    *pasynUser,
                                            epicsFloat64 value) {
  int function             = pasynUser->reason;
  const char *functionName = "writeFloat64";

  if (checkParamNameAndId(function, functionName) != asynSuccess) {
    return asynError;
  }

  return pEcmcParamInUseArray_[function]->writeFloat64(value);
}

asynStatus ecmcAsynPortDriver::readFloat64(asynUser     *pasynUser,
                                           epicsFloat64 *value) {
  int function             = pasynUser->reason;
  const char *functionName = "readFloat64";

  if (checkParamNameAndId(function, functionName) != asynSuccess) {
    return asynError;
  }

  return pEcmcParamInUseArray_[function]->readFloat64(value);
}

asynUser * ecmcAsynPortDriver::getTraceAsynUser() {
  return pasynUserSelf;
}

/** Overrides asynPortDriver::writeInt8Array.
 * Writes int8Array
 * \param[in] pasynUser Pointer to asyn user structure
 * \param[in] value Input data buffer.
 * \param[in] nElements Input data size.
 *
 * \return asynSuccess or asynError.
 */
asynStatus ecmcAsynPortDriver::writeInt8Array(asynUser  *pasynUser,
                                              epicsInt8 *value,
                                              size_t     nElements) {
  int function             = pasynUser->reason;
  const char *functionName = "writeInt8Array";

  if (checkParamNameAndId(function, functionName) != asynSuccess) {
    return asynError;
  }

  return pEcmcParamInUseArray_[function]->writeInt8Array(value, nElements);
}

asynStatus ecmcAsynPortDriver::readInt8Array(asynUser  *pasynUser,
                                             epicsInt8 *value,
                                             size_t     nElements,
                                             size_t    *nIn) {
  int function             = pasynUser->reason;
  const char *functionName = "readInt8Array";

  if (checkParamNameAndId(function, functionName) != asynSuccess) {
    return asynError;
  }

  return pEcmcParamInUseArray_[function]->readInt8Array(value, nElements, nIn);
}

/** Overrides asynPortDriver::writeInt16Array.
 * Writes int16Array
 * \param[in] pasynUser Pointer to asyn user structure
 * \param[in] value Input data buffer.
 * \param[in] nElements Input data size.
 *
 * \return asynSuccess or asynError.
 */
asynStatus ecmcAsynPortDriver::writeInt16Array(asynUser   *pasynUser,
                                               epicsInt16 *value,
                                               size_t      nElements) {
  int function             = pasynUser->reason;
  const char *functionName = "writeInt16Array";

  if (checkParamNameAndId(function, functionName) != asynSuccess) {
    return asynError;
  }

  return pEcmcParamInUseArray_[function]->writeInt16Array(value, nElements);
}

asynStatus ecmcAsynPortDriver::readInt16Array(asynUser   *pasynUser,
                                              epicsInt16 *value,
                                              size_t      nElements,
                                              size_t     *nIn) {
  int function             = pasynUser->reason;
  const char *functionName = "readInt16Array";

  if (checkParamNameAndId(function, functionName) != asynSuccess) {
    return asynError;
  }

  return pEcmcParamInUseArray_[function]->readInt16Array(value, nElements,
                                                         nIn);
}

/** Overrides asynPortDriver::writeInt32Array.
 * Writes int32Array
 * \param[in] pasynUser Pointer to asyn user structure
 * \param[in] value Input data buffer.
 * \param[in] nElements Input data size.
 *
 * \return asynSuccess or asynError.
 */
asynStatus ecmcAsynPortDriver::writeInt32Array(asynUser   *pasynUser,
                                               epicsInt32 *value,
                                               size_t      nElements) {
  int function             = pasynUser->reason;
  const char *functionName = "writeInt32Array";

  if (checkParamNameAndId(function, functionName) != asynSuccess) {
    return asynError;
  }

  return pEcmcParamInUseArray_[function]->writeInt32Array(value, nElements);
}

asynStatus ecmcAsynPortDriver::readInt32Array(asynUser   *pasynUser,
                                              epicsInt32 *value,
                                              size_t      nElements,
                                              size_t     *nIn) {
  int function             = pasynUser->reason;
  const char *functionName = "readInt32Array";

  if (checkParamNameAndId(function, functionName) != asynSuccess) {
    return asynError;
  }

  return pEcmcParamInUseArray_[function]->readInt32Array(value, nElements,
                                                         nIn);
}

/** Overrides asynPortDriver::writeFloat32Array.
 * Writes float32Array
 * \param[in] pasynUser Pointer to asyn user structure
 * \param[in] value Input data buffer.
 * \param[in] nElements Input data size.
 *
 * \return asynSuccess or asynError.
 */
asynStatus ecmcAsynPortDriver::writeFloat32Array(asynUser     *pasynUser,
                                                 epicsFloat32 *value,
                                                 size_t        nElements) {
  int function             = pasynUser->reason;
  const char *functionName = "writeFloat32Array";

  if (checkParamNameAndId(function, functionName) != asynSuccess) {
    return asynError;
  }

  return pEcmcParamInUseArray_[function]->writeFloat32Array(value, nElements);
}

asynStatus ecmcAsynPortDriver::readFloat32Array(asynUser     *pasynUser,
                                                epicsFloat32 *value,
                                                size_t        nElements,
                                                size_t       *nIn) {
  int function             = pasynUser->reason;
  const char *functionName = "readFloat32Array";

  if (checkParamNameAndId(function, functionName) != asynSuccess) {
    return asynError;
  }

  return pEcmcParamInUseArray_[function]->readFloat32Array(value,
                                                           nElements,
                                                           nIn);
}

/** Overrides asynPortDriver::writeFloat64Array.
 * Writes float6432Array
 * \param[in] pasynUser Pointer to asyn user structure
 * \param[in] value Input data buffer.
 * \param[in] nElements Input data size.
 *
 * \return asynSuccess or asynError.
 */
asynStatus ecmcAsynPortDriver::writeFloat64Array(asynUser     *pasynUser,
                                                 epicsFloat64 *value,
                                                 size_t        nElements) {
  int function             = pasynUser->reason;
  const char *functionName = "writeFloat64Array";

  if (checkParamNameAndId(function, functionName) != asynSuccess) {
    return asynError;
  }

  return pEcmcParamInUseArray_[function]->writeFloat64Array(value, nElements);
}

asynStatus ecmcAsynPortDriver::readFloat64Array(asynUser     *pasynUser,
                                                epicsFloat64 *value,
                                                size_t        nElements,
                                                size_t       *nIn) {
  int function             = pasynUser->reason;
  const char *functionName = "readFloat64Array";

  if (checkParamNameAndId(function, functionName) != asynSuccess) {
    return asynError;
  }

  return pEcmcParamInUseArray_[function]->readFloat64Array(value,
                                                           nElements,
                                                           nIn);
}

#ifdef ECMC_ASYN_ASYNPARAMINT64

asynStatus ecmcAsynPortDriver::readInt64(asynUser   *pasynUser,
                                         epicsInt64 *value) {
  int function             = pasynUser->reason;
  const char *functionName = "readInt64";

  if (checkParamNameAndId(function, functionName) != asynSuccess) {
    return asynError;
  }

  return pEcmcParamInUseArray_[function]->readInt64(value);
}

asynStatus ecmcAsynPortDriver::writeInt64(asynUser  *pasynUser,
                                          epicsInt64 value) {
  int function             = pasynUser->reason;
  const char *functionName = "writeInt64";

  if (checkParamNameAndId(function, functionName) != asynSuccess) {
    return asynError;
  }

  return pEcmcParamInUseArray_[function]->writeInt64(value);
}

asynStatus ecmcAsynPortDriver::writeInt64Array(asynUser   *pasynUser,
                                               epicsInt64 *value,
                                               size_t      nElements) {
  int function             = pasynUser->reason;
  const char *functionName = "writeInt64Array";

  if (checkParamNameAndId(function, functionName) != asynSuccess) {
    return asynError;
  }

  return pEcmcParamInUseArray_[function]->writeInt64Array(value, nElements);
}

asynStatus ecmcAsynPortDriver::readInt64Array(asynUser   *pasynUser,
                                              epicsInt64 *value,
                                              size_t      nElements,
                                              size_t     *nIn) {
  int function             = pasynUser->reason;
  const char *functionName = "readInt64Array";

  if (checkParamNameAndId(function, functionName) != asynSuccess) {
    return asynError;
  }

  return pEcmcParamInUseArray_[function]->readInt64Array(value, nElements,
                                                         nIn);
}

#endif //ECMC_ASYN_ASYNPARAMINT64

void ecmcAsynPortDriver::setAllowRtThreadCom(bool allowRtCom) {
  allowRtThreadCom_ = allowRtCom;
}

bool ecmcAsynPortDriver::getAllowRtThreadCom() {
  return allowRtThreadCom_;
}

/** Overrides asynPortDriver::drvUserCreate.
 * This function is called by the asyn-framework for each record that is linked to this asyn port.
 * \param[in] pasynUser Pointer to asyn user structure
 * \param[in] drvInfo String containing information about the parameter.
 * \param[out] pptypeName
 * \param[out] psize size of pptypeName.
 * \return asynSuccess or asynError.
 * The drvInfo string is what is after the asyn() in the "INP" or "OUT"
 * field of an record.
 */
asynStatus ecmcAsynPortDriver::drvUserCreate(asynUser    *pasynUser,
                                             const char  *drvInfo,
                                             const char **pptypeName,
                                             size_t      *psize) {
  const char *functionName = "drvUserCreate";

  asynPrint(pasynUser,
            ASYN_TRACE_FLOW,
            "%s:%s: drvInfo: %s\n",
            driverName,
            functionName,
            drvInfo);

  int addr          = 0;
  asynStatus status = getAddress(pasynUser, &addr);

  if (status != asynSuccess) {
    asynPrint(pasynUser,
              ASYN_TRACE_ERROR,
              "%s:%s: getAddress() failed.\n",
              driverName,
              functionName);
    return status;
  }

  ecmcAsynDataItem *newParam = new ecmcAsynDataItem(this);
  status = newParam->setDrvInfo(drvInfo);

  if (status != asynSuccess) {
    delete newParam;
    return asynError;
  }

  int index = 0;
  status = findParam(ECMC_ASYN_DEFAULT_LIST, newParam->getParamName(), &index);

  if (status != asynSuccess) {
    // Param not found see if found in available list
    ecmcAsynDataItem *param = findAvailParam(newParam->getParamName());

    if (!param) {
      asynPrint(pasynUser,
                ASYN_TRACE_ERROR,
                "%s:%s: Parameter %s not found (drvInfo=%s).\n",
                driverName,
                functionName,
                newParam->getParamName(),
                drvInfo);
      delete newParam;
      return asynError;
    }

    // Set drvInfo to found param
    status = param->setDrvInfo(drvInfo);

    if (status != asynSuccess) {
      asynPrint(pasynUser,
                ASYN_TRACE_ERROR,
                "%s:%s: Error: setDrvInfo failed.\n",
                driverName,
                functionName);
      return asynError;
    }

    // Ensure that type is supported
    if (!param->asynTypeSupported(newParam->getAsynType())) {
      asynPrint(pasynUser,
                ASYN_TRACE_ERROR,
                "%s:%s: Error: asynType %s not supported for parameter %s. Supported types are:\n",
                driverName,
                functionName,
                newParam->getAsynTypeName(),
                param->getParamName());

      for (int i = 0; i < param->getSupportedAsynTypeCount(); i++) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s:%s: AsynType: %s (%d)\n",
                  driverName, functionName,
                  asynTypeToString((long)param->getSupportedAsynType(i)),
                  param->getSupportedAsynType(i));
      }
      delete newParam;
      return asynError;
    }

    // Type supported so use it.
    param->setAsynParameterType(newParam->getAsynType());
    param->getParamInfo()->cmdFloat64ToInt32 =
      newParam->getParamInfo()->cmdFloat64ToInt32;
    param->getParamInfo()->cmdInt64ToFloat64 =
      newParam->getParamInfo()->cmdInt64ToFloat64;
    param->getParamInfo()->cmdUint64ToFloat64 =
      newParam->getParamInfo()->cmdUint64ToFloat64;

    // Add parameter to In use list
    status = appendInUseParam(param, 0);

    if (status != asynSuccess) {
      asynPrint(pasynUser,
                ASYN_TRACE_ERROR,
                "%s:%s: Append parameter %s to in-use list failed.\n",
                driverName,
                functionName,
                newParam->getParamName());
      delete newParam;
      return asynError;
    }

    // Create asyn param
    int errorCode = param->createParam();

    if (errorCode) {
      asynPrint(pasynUser,
                ASYN_TRACE_ERROR,
                "%s:%s: Create parameter %s failed (0x%x).\n",
                driverName,
                functionName,
                newParam->getParamName(),
                errorCode);
      delete newParam;
      return asynError;
    }

    // Ensure that parameter index is correct
    if (param->getAsynParameterIndex() != (ecmcParamInUseCount_ - 1)) {
      asynPrint(pasynUser,
                ASYN_TRACE_ERROR,
                "%s:%s: Parameter index missmatch for  %s  (%d != %d).\n",
                driverName,
                functionName,
                newParam->getParamName(),
                param->getAsynParameterIndex(),
                ecmcParamInUseCount_ - 1);
      delete newParam;
      return asynError;
    }

    // Update index
    index = param->getAsynParameterIndex();

    /*asynPrint(pasynUser,
              ASYN_TRACE_INFO,
              "%s:%s: Parameter %s linked to record (asyn reason %d).\n",
              driverName,
              functionName,
              newParam->getParamName(),
              index);*/

    // Now we have linked a parameter from available list into the inUse list successfully
  }

  // Asyn parameter found. Update with new info from record defs
  if (!pEcmcParamInUseArray_[index]) {
    asynPrint(pasynUser,
              ASYN_TRACE_ERROR,
              "%s:%s:pAdsParamArray_[%d]==NULL (drvInfo=%s).\n",
              driverName,
              functionName,
              index,
              drvInfo);
    delete newParam;
    return asynError;
  }

  ecmcParamInfo *existentParInfo =
    pEcmcParamInUseArray_[index]->getParamInfo();

  if (!existentParInfo->initialized) {
    /*pEcmcParamInUseArray_[index]->setAsynParSampleTimeMS(newParam->getSampleTimeMs());
    existentParInfo->recordName=strdup(newParam->getRecordName());
    existentParInfo->recordType=strdup(newParam->getRecordType());
    existentParInfo->dtyp=strdup(newParam->getDtyp());
    existentParInfo->drvInfo=strdup(newParam->getDrvInfo());*/

    if (existentParInfo->cmdInt64ToFloat64 &&
        (pEcmcParamInUseArray_[index]->getEcmcBitCount() != 64)) {
      asynPrint(pasynUser,
                ASYN_TRACE_ERROR,
                "%s:%s: Command " ECMC_OPTION_CMD_INT_TO_FLOAT64 " is only valid for 8 byte parameters (drvInfo = %s).\n",
                driverName,
                functionName,
                drvInfo);
      delete newParam;
      return asynError;
    }

    if (existentParInfo->cmdUint64ToFloat64 &&
        (pEcmcParamInUseArray_[index]->getEcmcBitCount() != 64)) {
      asynPrint(pasynUser,
                ASYN_TRACE_ERROR,
                "%s:%s: Command " ECMC_OPTION_CMD_UINT64_TO_FLOAT64 " is only valid for 8 byte parameters (drvInfo = %s).\n",
                driverName,
                functionName,
                drvInfo);
      delete newParam;
      return asynError;
    }

    if (existentParInfo->cmdUint32ToFloat64 &&
        (pEcmcParamInUseArray_[index]->getEcmcBitCount() != 32)) {
      asynPrint(pasynUser,
                ASYN_TRACE_ERROR,
                "%s:%s: Command " ECMC_OPTION_CMD_UINT32_TO_FLOAT64 " is only valid for 4 byte parameters (drvInfo = %s).\n",
                driverName,
                functionName,
                drvInfo);
      delete newParam;
      return asynError;
    }

    if (existentParInfo->cmdFloat64ToInt32 &&
        (pEcmcParamInUseArray_[index]->getEcmcDataSize() != 8)) {
      asynPrint(pasynUser,
                ASYN_TRACE_ERROR,
                "%s:%s: Command " ECMC_OPTION_CMD_FLOAT64_INT " is only valid for 8 byte parameters (drvInfo = %s).\n",
                driverName,
                functionName,
                drvInfo);
      delete newParam;
      return asynError;
    }
  }

  // Ensure that sample time is the shortest (if several records
  // on the same parameter)
  if (newParam->getSampleTimeMs() <  existentParInfo->sampleTimeMS) {
    if (newParam->getSampleTimeMs() >= 0) {
      pEcmcParamInUseArray_[index]->setAsynParSampleTimeMS(
        newParam->getSampleTimeMs());
    }
  }

  if (pasynUser->timeout < newParam->getSampleTimeMs() * 2 / 1000.0) {
    pasynUser->timeout = (newParam->getSampleTimeMs() * 2) / 1000;
  }

  delete newParam;

  existentParInfo->initialized = 1;
  pEcmcParamInUseArray_[index]->refreshParam(1);
  callParamCallbacks(ECMC_ASYN_DEFAULT_LIST, ECMC_ASYN_DEFAULT_ADDR);

  return asynPortDriver::drvUserCreate(pasynUser,
                                       existentParInfo->name,
                                       pptypeName,
                                       psize);
}

int32_t ecmcAsynPortDriver::getFastestUpdateRate() {
  return fastestParamUpdateCycles_;
}

int32_t ecmcAsynPortDriver::calcFastestUpdateRate() {
  fastestParamUpdateCycles_ =
    (int32_t)(defaultSampleTimeMS_ / 1000.0 * mcuFrequency);

  for (int i = 0; i < ecmcParamInUseCount_; i++) {
    if (pEcmcParamInUseArray_[i]) {
      if (!pEcmcParamInUseArray_[i]->linkedToAsynClient()) {
        continue;
      }

      if ((pEcmcParamInUseArray_[i]->getSampleTimeCycles() <
           fastestParamUpdateCycles_) &&
          (pEcmcParamInUseArray_[i]->getSampleTimeCycles() >= 0)) {
        fastestParamUpdateCycles_ =
          pEcmcParamInUseArray_[i]->getSampleTimeCycles();
      }
    }
  }
  return fastestParamUpdateCycles_;
}

void ecmcAsynPortDriver::refreshAllInUseParamsRT() {
  for (int i = 0; i < ecmcParamInUseCount_; i++) {
    if (pEcmcParamInUseArray_[i]) {
      if (!pEcmcParamInUseArray_[i]->linkedToAsynClient()) {
        continue;
      }
      pEcmcParamInUseArray_[i]->refreshParamRT(1);
    }
  }
}

void ecmcAsynPortDriver::reportParamInfo(FILE             *fp,
                                         ecmcAsynDataItem *param,
                                         int               listIndex,
                                         int               details) {
  if (!param) {
    return;
  }
  ecmcParamInfo *paramInfo = param->getParamInfo();

  if (!paramInfo) {
    return;
  }

  if( details < 0 ) {
   fprintf(fp, "p[%d] = %s\n",listIndex, param->getName());
   return;
  }

  fprintf(fp, "  Parameter %d:\n",                   listIndex);
  fprintf(fp, "    Param name:                %s\n", paramInfo->name);
  fprintf(fp, "    Param index:               %d\n", paramInfo->index);
  fprintf(fp, "    Param type:                %s (%d)\n",
          asynTypeToString((long)paramInfo->asynType), paramInfo->asynType);

  // supported types
  fprintf(fp, "    Supported asyn types:\n");

  for (int i = 0; i < param->getSupportedAsynTypeCount(); i++) {
    fprintf(fp, "      - %s (%d)\n", asynTypeToString(
              (long)param->getSupportedAsynType(i)),
            param->getSupportedAsynType(i));
  }
  fprintf(fp,
          "    Param linked to record:    %s\n",
          paramInfo->initialized ? "true" : "false");

  if (!paramInfo->initialized) {  // No record linked to record (no more valid data)
    fprintf(fp, "    ECMC name:                 %s\n", param->getName());
    fprintf(fp,
            "    ECMC data pointer valid:   %s\n",
            param->getEcmcDataPointerValid() ? "true" : "false");
    fprintf(fp,
            "    ECMC size [bytes]:         %zu\n",
            param->getEcmcDataSize());
    fprintf(fp,
            "    ECMC data is array:        %s\n",
            paramInfo->dataIsArray ? "true" : "false");
    fprintf(fp,
            "    ECMC write allowed:        %s\n",
            param->getAllowWriteToEcmc() ? "true" : "false");
    fprintf(fp, "    ECMC Data type:            %s\n",
            getEcDataTypeStr(param->getEcmcDataType()));

    if (param->getEcmcDataMaxSize() == sizeof(uint64_t)) {
      fprintf(fp,
              "    ECMC value:                0x%" PRIx64 "\n",
              *((uint64_t *)param->getDataPtr()));
    }
    fprintf(fp, "\n");
    return;
  }
  fprintf(fp, "    Param drvInfo:             %s\n", paramInfo->drvInfo);
  fprintf(fp, "    Param sample time [ms]:    %.0lf\n",
          paramInfo->sampleTimeMS);
  fprintf(fp,
          "    Param sample cycles []:    %d\n",
          paramInfo->sampleTimeCycles);
  fprintf(fp,
          "    Param isIOIntr:            %s\n",
          paramInfo->isIOIntr ? "true" : "false");
  fprintf(fp, "    Param asyn addr:           %d\n", paramInfo->asynAddr);
  fprintf(fp, "    Param alarm:               %d\n",
          paramInfo->alarmStatus);
  fprintf(fp,
          "    Param severity:            %d\n",
          paramInfo->alarmSeverity);
  fprintf(fp, "    ECMC name:                 %s\n", param->getName());
  fprintf(fp,
          "    ECMC data pointer valid:   %s\n",
          param->getEcmcDataPointerValid() ? "true" : "false");
  fprintf(fp,
          "    ECMC size [bits]:          %zu\n",
          param->getEcmcBitCount());
  fprintf(fp,
          "    ECMC size [bytes]:         %zu\n",
          param->getEcmcDataSize());
  fprintf(fp,
          "    ECMC max size [bytes]:     %zu\n",
          param->getEcmcDataMaxSize());
  fprintf(fp,
          "    ECMC data is array:        %s\n",
          paramInfo->dataIsArray ? "true" : "false");
  fprintf(fp,
          "    ECMC write allowed:        %s\n",
          param->getAllowWriteToEcmc() ? "true" : "false");
  fprintf(fp, "    ECMC Data type:            %s\n",
          getEcDataTypeStr(param->getEcmcDataType()));

  if (param->getEcmcDataMaxSize() == sizeof(uint64_t)) {
    fprintf(fp,
            "    ECMC value:                0x%" PRIx64 "\n",
            *((uint64_t *)param->getDataPtr()));
  }

  // Value range only applicable for ints
  if (param->getEcmcMinValueInt() != param->getEcmcMaxValueInt()) {
    fprintf(fp,
            "    ECMC Value Range:          %" PRId64 "..%" PRIu64 ", %zu bit(s)\n",
            param->getEcmcMinValueInt(),
            param->getEcmcMaxValueInt(),
            param->getEcmcBitCount());
  }
  fprintf(fp,
          "    ECMC Cmd: Uint642Float64:    %s\n",
          paramInfo->cmdUint64ToFloat64 ? "true" : "false");
  fprintf(fp,
          "    ECMC Cmd: Uint322Float64:    %s\n",
          paramInfo->cmdUint32ToFloat64 ? "true" : "false");
  fprintf(fp,
          "    ECMC Cmd: Int2Float64:     %s\n",
          paramInfo->cmdInt64ToFloat64 ? "true" : "false");
  fprintf(fp,
          "    ECMC Cmd: Float642Int:     %s\n",
          paramInfo->cmdFloat64ToInt32 ? "true" : "false");
  fprintf(fp, "    Record name:               %s\n", paramInfo->recordName);
  fprintf(fp, "    Record type:               %s\n", paramInfo->recordType);
  fprintf(fp, "    Record dtyp:               %s\n", paramInfo->dtyp);
  fprintf(fp, "\n");
}

/** Report of configured parameters.
 * \param[in] fp Output file.
 * \param[in] details Details of printout. A higher number results in more
 *            details.
 * \return void
 * Check ads state of all connected ams ports and reconnects if needed.
 */
void ecmcAsynPortDriver::report(FILE *fp, int details) {
  const char *functionName = "report";

  asynPrint(pasynUserSelf,
            ASYN_TRACE_FLOW,
            "%s:%s:\n",
            driverName,
            functionName);

  if (!fp) {
    printf("%s:%s: ERROR: File NULL.\n", driverName, functionName);
    return;
  }

  if (details >= 0) {
    fprintf(fp,
            "####################################################################:\n");
    fprintf(fp, "General information:\n");
    fprintf(fp, "  Port:                           %s\n", portName);
    fprintf(fp,
            "  Auto-connect:                   %s\n",
            autoConnect_ ? "true" : "false");
    fprintf(fp, "  Priority:                       %d\n", priority_);
    fprintf(fp, "  Param. table size:              %d\n", paramTableSize_);
    fprintf(fp, "  Param. count:                   %d\n",
            ecmcParamInUseCount_);
    fprintf(fp, "  Default sample time [ms]:       %d\n",
            defaultSampleTimeMS_);
    fprintf(fp,
            "  Fastest update rate [cycles]:   %d\n",
            fastestParamUpdateCycles_);
    fprintf(fp, "  Realtime loop rate [Hz]:        %lf\n", mcuFrequency);
    fprintf(fp, "  Realtime loop sample time [ms]: %lf\n", mcuPeriod / 1E6);
    fprintf(fp, "\n");
  }

  if (details >= 1 ) {
    // print all parameters in use
    fprintf(fp,
            "####################################################################:\n");
    fprintf(fp, "Parameters in use:\n");

    for (int i = 0; i < ecmcParamInUseCount_; i++) {
      if (!pEcmcParamInUseArray_[i]) {
        fprintf(fp,
                "%s:%s: ERROR: Parameter array null at index %d\n",
                driverName,
                functionName,
                i);
        return;
      }
      reportParamInfo(fp, pEcmcParamInUseArray_[i], i, details);
    }
  }

  if (details >= 2 || details < 0) {
    // print all available parameters
    fprintf(fp,
            "####################################################################:\n");
    fprintf(fp, "Available parameters:\n");

    for (int i = 0; i < ecmcParamAvailCount_; i++) {
      if (!pEcmcParamAvailArray_[i]) {
        fprintf(fp,
                "%s:%s: ERROR: Parameter array null at index %d\n",
                driverName,
                functionName,
                i);
        return;
      }
      reportParamInfo(fp, pEcmcParamAvailArray_[i], i, details);
    }
  }

  if (details >= 0) {
    fprintf(fp,
            "####################################################################:\n");
  }

  if (details >= 3) {
    fprintf(fp, "Report from base class (asynPortDriver):\n");
    asynPortDriver::report(fp, details);
    fprintf(fp,
            "####################################################################:\n");
  }
}

void ecmcAsynPortDriver::grepParam(FILE *fp, const char *pattern, int details) {
  const char *functionName = "grepParam";

  asynPrint(pasynUserSelf,
            ASYN_TRACE_FLOW,
            "%s:%s:\n",
            driverName,
            functionName);

  if (!fp) {
    printf("%s:%s: ERROR: File NULL.\n", driverName, functionName);
    return;
  }

  // print all parameters that fit pattern
  fprintf(fp,
          "####################################################################:\n");
  fprintf(fp, "ecmc parameters that fit pattern %s:\n", pattern);

  for (int i = 0; i < ecmcParamAvailCount_; i++) {
    if (pEcmcParamAvailArray_[i]) {
      ecmcParamInfo *paramInfo = pEcmcParamAvailArray_[i]->getParamInfo();

      if (paramInfo) {
        if (epicsStrGlobMatch(paramInfo->name, pattern)) {
          reportParamInfo(fp, pEcmcParamAvailArray_[i], i,details);
        }
      }
    }
  }
}

void ecmcAsynPortDriver::grepRecord(FILE *fp, const char *pattern) {
  const char *functionName = "grepRecord";

  asynPrint(pasynUserSelf,
            ASYN_TRACE_FLOW,
            "%s:%s:\n",
            driverName,
            functionName);

  if (!fp) {
    printf("%s:%s: ERROR: File NULL.\n", driverName, functionName);
    return;
  }

  // print all parameters that fit pattern
  fprintf(fp,
          "####################################################################:\n");
  fprintf(fp, "ecmc records that fit pattern %s:\n", pattern);

  for (int i = 0; i < ecmcParamAvailCount_; i++) {
    if (pEcmcParamAvailArray_[i]) {
      ecmcParamInfo *paramInfo = pEcmcParamAvailArray_[i]->getParamInfo();

      if (paramInfo) {
        // Match param-name or record-name
        if (paramInfo->initialized) {
          if (epicsStrGlobMatch(paramInfo->recordName, pattern)) {
            reportParamInfo(fp, pEcmcParamAvailArray_[i], i, 2);
          }
        }
      }
    }
  }
}

int ecmcAsynPortDriver::getDefaultSampleTimeMs() {
  return defaultSampleTimeMS_;
}

/* Configuration routine.  Called directly, or from the iocsh function below */

extern "C" {
static int maxParameters;
static int parameterCounter;
static int ecmcInitialized = 0;

/* global asynUser for Printing */
asynUser *pPrintOutAsynUser;


/** EPICS iocsh callable function to call constructor for the ecmcAsynPortDriver class.
  * \param[in] portName The name of the asyn port driver to be created.
  * \param[in] paramTableSize The max number of parameters.
  * \param[in] priority Priority.
  * \param[in] disableAutoConnect Disable auto connect
  * \param[in] default parameter update rate in milli seconds (for I/O intr records)*/
int ecmcAsynPortDriverConfigure(const char *portName,
                                int         paramTableSize,
                                int         priority,
                                int         disableAutoConnect,
                                double      defaultSampleRateMS) {
  if (ecmcInitialized) {
    printf(
      "ecmcAsynPortDriverConfigure: Error: ECMC already initialized. Command ignored.\n");
    return asynError;
  }

  if (portName == NULL) {
    printf("ecmcAsynPortDriverConfigure: Error: portName missing.\n");
    return asynError;
  }

  int paramTableMin = ECMC_ASYN_MAIN_PAR_COUNT + ECMC_ASYN_EC_PAR_COUNT + 1;

  if (paramTableSize < paramTableMin) {
    paramTableSize = paramTableMin;
    printf(
      "ecmcAsynPortDriverConfigure: WARNING: Parameter table to small. Increasing to minimum value (%d)\n",
      paramTableMin);
  }

  parameterCounter = 0;
  maxParameters    = paramTableSize;
  ecmcAsynPortObj  = new ecmcAsynPortDriver(portName,
                                            paramTableSize,
                                            disableAutoConnect == 0,
                                            priority,
                                            defaultSampleRateMS);

  ecmcInitialized = 1;

  initHook();

  printf(
    "ecmcAsynPortDriverConfigure: portName = %s, paramTableSize = %d, disableAutoConnect = %d, priority = %d, defaultSampleRateMS = %lf\n",
    portName,
    paramTableSize,
    disableAutoConnect,
    priority,
    defaultSampleRateMS);

  if (ecmcAsynPortObj) {
    asynUser *traceUser = ecmcAsynPortObj->getTraceAsynUser();

    if (!traceUser) {
      printf(
        "ecmcAsynPortDriverConfigure: ERROR: Failed to retrieve asynUser for trace. \n");
      return asynError;
    }

    pPrintOutAsynUser = pasynManager->duplicateAsynUser(traceUser, 0, 0);

    if (!pPrintOutAsynUser) {
      printf(
        "ecmcAsynPortDriverConfigure: ERROR: Failed to duplicate asynUser for trace. \n");
      return asynError;
    }

    asynPrint(pPrintOutAsynUser,
              ASYN_TRACE_INFO,
              "ecmcAsynPortDriverConfigure: INFO: New AsynPortDriver success (%s,%i,%i,%i).",
              portName,
              paramTableSize,
              disableAutoConnect == 0,
              priority);

    return asynSuccess;
  } else {
    asynPrint(pPrintOutAsynUser,
              ASYN_TRACE_ERROR,
              "ecmcAsynPortDriverConfigure: ERROR: New AsynPortDriver failed.");
    return asynError;
  }
}

/* EPICS iocsh shell command: ecmcAsynPortDriverConfigure*/

static const iocshArg initArg0 = { "port name", iocshArgString };
static const iocshArg initArg1 = { "parameter table size", iocshArgInt };
static const iocshArg initArg2 = { "priority", iocshArgInt };
static const iocshArg initArg3 = { "disable auto connect", iocshArgInt };
static const iocshArg initArg4 =
{ "default param sample rate (ms)", iocshArgDouble };

static const iocshArg *const initArgs[] = { &initArg0,
                                            &initArg1,
                                            &initArg2,
                                            &initArg3,
                                            &initArg4 };
static const iocshFuncDef    initFuncDef =
{ "ecmcAsynPortDriverConfigure", 5, initArgs };
static void initCallFunc(const iocshArgBuf *args) {
  ecmcAsynPortDriverConfigure(args[0].sval,
                              args[1].ival,
                              args[2].ival,
                              args[3].ival,
                              args[4].dval);
}

/** \brief Obsolete EPICS iocsh command for adding asyn-parameter(s)
 */
int ecmcAsynPortDriverAddParameter(const char *portName,
                                   const char *idString,
                                   const char *asynTypeString,
                                   int         skipCycles) {
  printf("Error: ecmcAsynPortDriverAddParameter is an obsolete command.\n");
  printf(
    "       Use the command \"asynReport 3\" to list available parameters.\n");
  return 0;
}

/* EPICS iocsh shell command:  ecmcAsynPortDriverAddParameter*/
static const iocshArg initArg0_2 = { "port name", iocshArgString };
static const iocshArg initArg1_2 = { "id string", iocshArgString };
static const iocshArg initArg2_2 = { "asynType", iocshArgString };
static const iocshArg initArg3_2 = { "skipCycles", iocshArgInt };

static const iocshArg *const initArgs_2[] = { &initArg0_2,
                                              &initArg1_2,
                                              &initArg2_2,
                                              &initArg3_2
};

static const iocshFuncDef initFuncDef_2 =
{ "ecmcAsynPortDriverAddParameter", 4, initArgs_2 };
static void initCallFunc_2(const iocshArgBuf *args) {
  ecmcAsynPortDriverAddParameter(args[0].sval,
                                 args[1].sval,
                                 args[2].sval,
                                 args[3].ival);
}

/* EPICS iocsh shell command:  ecmcConfigOrDie*/
static ecmcOutputBufferType ecmcConfigBuffer;
int ecmcConfigOrDie(const char *ecmcCommand) {
  if (!ecmcAsynPortObj) {
    printf(
      "Error: No ecmcAsynPortDriver object found (ecmcAsynPortObj==NULL).\n");
    printf("       Use ecmcAsynPortDriverConfigure() to create object.\n");
    return asynError;
  }

  if (!ecmcCommand) {
    printf("Error: Command missing.\n");
    printf(
      "       Use \"ecmcConfigOrDie <command>\" to configure ecmc system\n");
    return asynError;
  }

  ecmcAsynPortObj->lock();

  clearBuffer(&ecmcConfigBuffer);
  int errorCode = motorHandleOneArg(ecmcCommand, &ecmcConfigBuffer);

  ecmcAsynPortObj->unlock();

  if (errorCode) {
    printf("ERROR: Command %s resulted in buffer overflow error: %s.\n",
           ecmcCommand,
           ecmcConfigBuffer.buffer);
    exit(EXIT_FAILURE);
  }

  // Check return value
  if (strcmp(ecmcConfigBuffer.buffer, "OK")) {
    int ecmcError = 0;
    int nvals     = sscanf(ecmcConfigBuffer.buffer,
                           ECMC_RETURN_ERROR_STRING "%d",
                           &ecmcError);

    if (nvals == 1) {
      printf("ECMC command \"%s\" returned error: %s (0x%x)\n",
             ecmcCommand,
             getErrorString(ecmcError),
             ecmcError);
    } else {
      printf("ECMC did not return \"OK\": %s\n", ecmcConfigBuffer.buffer);
    }
    exit(EXIT_FAILURE);
  }
  printf("%s\n", ecmcConfigBuffer.buffer);

  // Set return variable
  epicsEnvSet(ECMC_IOCSH_CFG_CMD_RETURN_VAR_NAME, ecmcConfigBuffer.buffer);

  return 0;
}

static const iocshArg initArg0_3 =
{ "Ecmc Command", iocshArgString };
static const iocshArg *const initArgs_3[]  = { &initArg0_3 };
static const iocshFuncDef    initFuncDef_3 =
{ "ecmcConfigOrDie", 1, initArgs_3 };
static void initCallFunc_3(const iocshArgBuf *args) {
  ecmcConfigOrDie(args[0].sval);
}

/* EPICS iocsh shell command:  ecmcConfig*/
int ecmcConfig(const char *ecmcCommand) {
  if (!ecmcAsynPortObj) {
    printf(
      "Error: No ecmcAsynPortDriver object found (ecmcAsynPortObj==NULL).\n");
    printf("       Use ecmcAsynPortDriverConfigure() to create object.\n");
    return asynError;
  }

  if (!ecmcCommand) {
    printf("Error: Command missing.\n");
    printf("       Use \"ecmcConfig <command>\" to configure ecmc system.\n");
    return asynError;
  }

  ecmcAsynPortObj->lock();

  clearBuffer(&ecmcConfigBuffer);
  int errorCode = motorHandleOneArg(ecmcCommand, &ecmcConfigBuffer);

  ecmcAsynPortObj->unlock();

  if (errorCode) {
    printf("ERROR: Command \"%s\" resulted in error code: %s.\n",
           ecmcCommand,
           ecmcConfigBuffer.buffer);
  }

  printf("%s\n", ecmcConfigBuffer.buffer);

  // Set return variable
  epicsEnvSet(ECMC_IOCSH_CFG_CMD_RETURN_VAR_NAME, ecmcConfigBuffer.buffer);

  return 0;
}

static const iocshArg initArg0_4 =
{ "Ecmc Command", iocshArgString };
static const iocshArg *const initArgs_4[]  = { &initArg0_4 };
static const iocshFuncDef    initFuncDef_4 = { "ecmcConfig", 1, initArgs_4 };
static void initCallFunc_4(const iocshArgBuf *args) {
  ecmcConfig(args[0].sval);
}

/* EPICS iocsh shell command: ecmcReport (same as asynReport but only ECMC)*/
int ecmcReport(int level) {
  if (!ecmcAsynPortObj) {
    printf(
      "Error: No ecmcAsynPortDriver object found (ecmcAsynPortObj==NULL).\n");
    printf("       Use ecmcAsynPortDriverConfigure() to create object.\n");
    return asynError;
  }

  ecmcAsynPortObj->report(stdout, level);

  return 0;
}

static const iocshArg initArg0_5 =
{ "Details level", iocshArgInt };
static const iocshArg *const initArgs_5[]  = { &initArg0_5 };
static const iocshFuncDef    initFuncDef_5 = { "ecmcReport", 1, initArgs_5 };
static void initCallFunc_5(const iocshArgBuf *args) {
  ecmcReport(args[0].ival);
}

/* EPICS iocsh shell command: ecmcGrepParam*/
int ecmcGrepParam(const char *pattern, int details) {
  if (!ecmcAsynPortObj) {
    printf(
      "Error: No ecmcAsynPortDriver object found (ecmcAsynPortObj==NULL).\n");
    printf("       Use ecmcAsynPortDriverConfigure() to create object.\n");
    return asynError;
  }

  if (!pattern) {
    printf("Error: Pattern missing.\n");
    printf(
      "       Use \"ecmcGrepParam <pattern>\" to list ecmc params/records.\n");
    return asynError;
  }

  ecmcAsynPortObj->grepParam(stdout, pattern, details);

  return 0;
}

static const iocshArg initArg0_6 =
{ "Pattern", iocshArgString };
static const iocshArg initArg1_6 = 
{ "Details", iocshArgString };

static const iocshArg *const initArgs_6[]  = { &initArg0_6, &initArg1_6};
static const iocshFuncDef    initFuncDef_6 =
{ "ecmcGrepParam", 2, initArgs_6 };
static void initCallFunc_6(const iocshArgBuf *args) {
  ecmcGrepParam(args[0].sval, args[1].ival);
}

/* EPICS iocsh shell command: ecmcGrepRecord*/
int ecmcGrepRecord(const char *pattern) {
  if (!ecmcAsynPortObj) {
    printf(
      "Error: No ecmcAsynPortDriver object found (ecmcAsynPortObj==NULL).\n");
    printf("       Use ecmcAsynPortDriverConfigure() to create object.\n");
    return asynError;
  }

  if (!pattern) {
    printf("Error: Pattern missing.\n");
    printf(
      "       Use \"ecmcGrepRecord <pattern>\" to list ecmc params/records.\n");
    return asynError;
  }

  ecmcAsynPortObj->grepRecord(stdout, pattern);

  return 0;
}

static const iocshArg initArg0_7 =
{ "Pattern", iocshArgString };
static const iocshArg *const initArgs_7[]  = { &initArg0_7 };
static const iocshFuncDef    initFuncDef_7 =
{ "ecmcGrepRecord", 1, initArgs_7 };
static void initCallFunc_7(const iocshArgBuf *args) {
  ecmcGrepRecord(args[0].sval);
}

static const char *allowedSpecInt[] = {
  "d", "i", "o", "u", "x", "l", "\0"
};

static const char *allowedSpecFloat[] = {
  "a", "A", "e", "E", "f", "F", "g", "G", "\0"
};

int formatIsDouble(const char *format) {
  if (!format) {
    printf("Invalid or empty format string.\n");
    return -1;
  }

  if (strlen(format) >= (ECMC_CMD_MAX_SINGLE_CMD_LENGTH - 1)) {
    printf("Format string to long (max length %d).\n",
           ECMC_CMD_MAX_SINGLE_CMD_LENGTH);
    return -1;
  }

  const char *firstProcent = strchr(format, '%');

  if (!firstProcent) {
    return -1;
  }

  char flags[ECMC_CMD_MAX_SINGLE_CMD_LENGTH];
  memset(flags, 0, sizeof(flags));
  int nvals = sscanf(firstProcent, "%%%[0-9 | +-.#hl]", flags);

  char specifiers[ECMC_CMD_MAX_SINGLE_CMD_LENGTH];
  memset(specifiers, 0, sizeof(specifiers));
  char *formatStart = (char *)firstProcent + strlen(flags) + 1;
  nvals = sscanf(formatStart, "%s", specifiers);

  if (nvals != 1) {
    printf(
      "Format string error. Could not determine specifier in string %s.\n",
      specifiers);
    return -1;
  }

  // Check specifiers for int
  size_t i            = 0;
  const char *element = allowedSpecInt[i];

  while (element[0] != 0) {
    if (strstr(specifiers, element)) {
      return 0;
    }
    i++;
    element = allowedSpecInt[i];
  }

  // Check specifiers for double
  i       = 0;
  element = allowedSpecFloat[i];

  while (element[0] != 0) {
    if (strstr(specifiers, element)) {
      return 1;
    }
    i++;
    element = allowedSpecFloat[i];
  }

  return -1; // invalid
}

asynStatus evalExprTK(const char *expression, double *result) {
  // Evaluate expression
  exprtkWrap *exprtk = new exprtkWrap();

  if (!exprtk) {
    printf("Failed allocation of exprtk expression parser.\n");
    return asynError;
  }
  double resultDouble = 0;

  exprtk->addVariable(ECMC_ENVSETCALC_RESULT_VAR, resultDouble);

  std::string exprStr = "";

  // Check if "RESULT" variable in str. If not then simple expression.. Add in beginning
  if (strstr(expression, ECMC_ENVSETCALC_RESULT_VAR)) {
    exprStr = expression;
  } else {
    exprStr  = ECMC_ENVSETCALC_RESULT_VAR;
    exprStr += ":=";
    exprStr += expression;
  }

  // Check if need to add ";" last
  if (exprStr.c_str()[strlen(exprStr.c_str()) - 1] != ';') {
    exprStr += ";";
  }

  if (exprtk->compile(exprStr)) {
    printf("Failed compile of expression with error message: %s.\n",
           exprtk->getParserError().c_str());
    return asynError;
  }
  exprtk->refresh();
  delete exprtk;  // not needed anymore (result in "resultDouble")

  *result = resultDouble;
  return asynSuccess;
}

void ecmcEpicsEnvSetCalcPrintHelp() {
  printf("\n");
  printf(
    "       Use \"ecmcEpicsEnvSetCalc(<envVarName>,  <expression>, <format>)\" to evaluate the expression and assign the variable.\n");
  printf("          <envVarName> : EPICS environment variable name.\n");
  printf(
    "          <expression> : Calculation expression (see exprTK for available functionality). Examples:\n");
  printf(
    "                         Simple expression:\"5.5+${TEST_SCALE}*sin(${TEST_ANGLE}/10)\".\n");
  printf(
    "                         Use of \"RESULT\" variable: \"if(${TEST_VAL}>5){RESULT:=100;}else{RESULT:=200;};\".\n");
  printf(
    "          <format>     : Optional format string. Example \"%%lf\", \"%%8.3lf\", \"%%x\", \"%%04d\" or \"%%d\", defaults to \"%%d\".\n");
  printf(
    "                         Can contain text like \"0x%%x\" or \"Hex value is 0x60%%x\".\n");
  printf(
    "                         Must contain one numeric value where result of expression will be written.\n");
  printf("\n");
  printf("       Restrictions:\n");
  printf(
    "         - Some flags and/or width/precision combinations might not be supported.\n");
  printf(
    "         - Hex numbers in the expression is not allowed (but hex as output by formating is OK).\n");
  printf(
    "         - Non floatingpoint values will be rounded to nearest int.\n");
  printf("\n");
}

/** EPICS iocsh shell command: ecmcEpicsEnvSetCalc
 *  Evaluates an expression and sets an EPICS environment variable
*/
int ecmcEpicsEnvSetCalc(const char *envVarName,
                        const char *expression,
                        const char *format) {
  const char *localFormat = format;

  if (!localFormat) {
    localFormat = ECMC_ENVSETCALC_DEF_FORMAT;
  }

  if (!envVarName) {
    printf("Error: Environment variable name  missing.\n");
    ecmcEpicsEnvSetCalcPrintHelp();
    return asynError;
  }

  if ((strcmp(envVarName, "-h") == 0) || (strcmp(envVarName, "--help") == 0)) {
    ecmcEpicsEnvSetCalcPrintHelp();
    return asynSuccess;
  }

  if (!expression) {
    printf("Error: Expression missing.\n");
    ecmcEpicsEnvSetCalcPrintHelp();
    return asynError;
  }

  double resultDouble = 0;

  if (evalExprTK(expression, &resultDouble) != asynSuccess) {
    return asynError;
  }

  // Convert if int in format string
  int  resultInt = round(resultDouble);
  char buffer[ECMC_CMD_MAX_SINGLE_CMD_LENGTH];
  unsigned int charCount = 0;
  memset(buffer, 0, sizeof(buffer));

  int isDouble = formatIsDouble(localFormat);

  if (isDouble < 0) {
    printf(
      "Error: Failed to determine datatype from format. Invalid format string \"%s\".\n",
      localFormat);
    return asynError;
  }

  if (isDouble) {
    charCount = snprintf(buffer,
                         sizeof(buffer),
                         localFormat,
                         resultDouble);
  } else {
    charCount = snprintf(buffer,
                         sizeof(buffer),
                         localFormat,
                         resultInt);
  }

  if (charCount >= sizeof(buffer) - 1) {
    printf("Write buffer size exceeded, format results in a to long string.\n");
    return asynError;
  }

  epicsEnvSet(envVarName, buffer);
  return asynSuccess;
}

static const iocshArg initArg0_8 =
{ "Variable name", iocshArgString };
static const iocshArg initArg1_8 =
{ "Expression", iocshArgString };
static const iocshArg initArg2_8 =
{ "Format", iocshArgString };
static const iocshArg *const initArgs_8[] =
{ &initArg0_8, &initArg1_8, &initArg2_8 };
static const iocshFuncDef initFuncDef_8 =
{ "ecmcEpicsEnvSetCalc", 3, initArgs_8 };
static void initCallFunc_8(const iocshArgBuf *args) {
  ecmcEpicsEnvSetCalc(args[0].sval, args[1].sval, args[2].sval);
}

void ecmcEpicsEnvSetCalcTernaryPrintHelp() {
  printf("\n");
  printf(
    "       Use \"ecmcEpicsEnvSetCalcTernary(<envVarName>,  <expression>, <trueString>, <falseString>)\" to evaluate the expression and assign the variable.\n");
  printf("          <envVarName>  : EPICS environment variable name.\n");
  printf(
    "          <expression>  : Calculation expression (see exprTK for available functionality). Examples:\n");
  printf(
    "                          Simple expression:\"5.5+${TEST_SCALE}*sin(${TEST_ANGLE}/10)\".\n");
  printf(
    "                          Use of \"RESULT\" variable: \"if(${TEST_VAL}>5){RESULT:=100;}else{RESULT:=200;};\".\n");
  printf(
    "          <trueString>  : String to set <envVarName> if expression (or \"RESULT\") evaluates to true.\n");
  printf(
    "          <falseString> : String to set <envVarName> if expression (or \"RESULT\") evaluates to false.\n");
  printf("\n");
}

/** EPICS iocsh shell command: ecmcEpicsEnvSetCalcTernary
 *  Evaluates an expression and sets an EPICS environment variable to:
 *   expression>0 : trueString
 *   expression<=0: falseString
*/
int ecmcEpicsEnvSetCalcTernary(const char *envVarName,
                               const char *expression,
                               const char *trueString,
                               const char *falseString) {
  if (!envVarName) {
    printf("Error: Environment variable name  missing.\n");
    ecmcEpicsEnvSetCalcTernaryPrintHelp();
    return asynError;
  }

  if ((strcmp(envVarName, "-h") == 0) || (strcmp(envVarName, "--help") == 0)) {
    ecmcEpicsEnvSetCalcTernaryPrintHelp();
    return asynSuccess;
  }

  if (!expression || !trueString || !falseString) {
    printf(
      "Error: \"expression\", \"trueString\" and/or \"falseString\" missing.\n");
    ecmcEpicsEnvSetCalcTernaryPrintHelp();
    return asynError;
  }

  double resultDouble = 0;

  if (evalExprTK(expression, &resultDouble) != asynSuccess) {
    return asynError;
  }

  if (resultDouble) {
    epicsEnvSet(envVarName, trueString);
  } else {
    epicsEnvSet(envVarName, falseString);
  }

  return asynSuccess;
}

static const iocshArg initArg0_9 =
{ "Variable name", iocshArgString };
static const iocshArg initArg1_9 =
{ "Expression", iocshArgString };
static const iocshArg initArg2_9 =
{ "True string", iocshArgString };
static const iocshArg initArg3_9 =
{ "False string", iocshArgString };

static const iocshArg *const initArgs_9[] =
{ &initArg0_9, &initArg1_9, &initArg2_9, &initArg3_9 };
static const iocshFuncDef initFuncDef_9 =
{ "ecmcEpicsEnvSetCalcTernary", 4, initArgs_9 };
static void initCallFunc_9(const iocshArgBuf *args) {
  ecmcEpicsEnvSetCalcTernary(args[0].sval,
                             args[1].sval,
                             args[2].sval,
                             args[3].sval);
}

void ecmcFileExistPrintHelp() {
  printf("\n");
  printf(
    "       Use \"ecmcFileExist(<filename>, <die>, <dirs>)\" to check if a file exists.\n");
  printf("          <filename>         : Filename to check.\n");
  printf(
    "          <die>              : Exit EPICS if file not exist. Optional, defaults to 0.\n");
  printf(
    "          <check EPICS dirs> : Look for files in EPICS_DB_INCLUDE_PATH dirs. Optional, defaults to 0.\n");
  printf(
    "          <dirs>             : List of dirs to search for file in (separated with ':'). Optional, defaults to \"\".\n");
  printf("\n");
}

#define ECMC_IS_FILE_BUFFER_SIZE 4096

int isFile(const char *filename, const char *dirs) {
  int   fileExist = 0;
  char  buffer[ECMC_IS_FILE_BUFFER_SIZE];
  char *pdirs     = (char *)dirs;
  char *pdirs_old = pdirs;

  if (dirs) {
    bool stop = false;

    while ((pdirs = strchr(pdirs, ':')) && !stop) {
      memset(buffer, 0, ECMC_IS_FILE_BUFFER_SIZE);
      int chars = (int)(pdirs - pdirs_old);
      memcpy(buffer, pdirs_old, chars);

      // strncpy(buffer, pdirs_old, chars);
      buffer[chars] = '/';
      chars++;
      memcpy(&buffer[chars], filename, strlen(filename));

      // strncpy(&buffer[chars], filename, strlen(filename));
      fileExist = access(buffer, 0) == 0;

      if (fileExist) {
        break;
      }

      if (strlen(pdirs) > 0) {
        pdirs++;
      } else {
        stop = true;
      }
      pdirs_old = pdirs;
    }

    // take the last also (if not already found)
    if ((strlen(pdirs_old) > 0) && !fileExist) {
      memset(buffer, 0, ECMC_IS_FILE_BUFFER_SIZE);
      int chars = strlen(pdirs_old);
      memcpy(buffer, pdirs_old, chars);

      // strncpy(buffer, pdirs_old, chars);
      buffer[chars] = '/';
      chars++;
      memcpy(&buffer[chars], filename, strlen(filename));

      // strncpy(&buffer[chars], filename, strlen(filename));
      fileExist = access(buffer, 0) == 0;
    }
  }

  return fileExist;
}

/** EPICS iocsh shell command: ecmcFileExist
 * Return if file exists otherwise "die"
*/
int ecmcFileExist(const char *filename,
                  int         die,
                  int         checkEpicsDirs,
                  const char *dirs) {
  if (!filename) {
    printf("Error: filename missing.\n");
    ecmcFileExistPrintHelp();
    return asynError;
  }

  if ((strcmp(filename, "-h") == 0) || (strcmp(filename, "--help") == 0)) {
    ecmcFileExistPrintHelp();
    return asynSuccess;
  }

  // Check filename directlly
  int fileExist = access(filename, 0) == 0;

  if (!fileExist && checkEpicsDirs) {
    fileExist = isFile(filename, getenv("EPICS_DB_INCLUDE_PATH"));
  }

  if (!fileExist && dirs) {
    fileExist = isFile(filename, dirs);
  }

  if (die && !fileExist) {
    printf("Error: File \"%s\" does not exist. ECMC shuts down.\n", filename);
    char *workdirname = get_current_dir_name();
    printf("       Current working directory           = %s\n",
           workdirname);
    free(workdirname);

    if (checkEpicsDirs) {
      printf("       Search path \"EPICS_DB_INCLUDE_PATH\" = %s.\n",
             getenv("EPICS_DB_INCLUDE_PATH"));
    }

    if (dirs) {
      printf("       Search path \"dirs\"                  = %s\n", dirs);
    }
    exit(EXIT_FAILURE);
  }
  epicsEnvSet(ECMC_IOCSH_FILE_EXIST_RETURN_VAR_NAME, fileExist ? "1" : "0");
  return asynSuccess;
}

static const iocshArg initArg0_10 =
{ "Filename", iocshArgString };
static const iocshArg initArg1_10 =
{ "DieIfNoFile", iocshArgInt };
static const iocshArg initArg2_10 =
{ "Check EPICS dirs", iocshArgInt };
static const iocshArg initArg3_10 =
{ "List of dirs", iocshArgString };
static const iocshArg *const initArgs_10[] =
{ &initArg0_10, &initArg1_10, &initArg2_10, &initArg3_10 };
static const iocshFuncDef initFuncDef_10 = { "ecmcFileExist", 4, initArgs_10 };
static void initCallFunc_10(const iocshArgBuf *args) {
  ecmcFileExist(args[0].sval, args[1].ival, args[2].ival, args[3].sval);
}

void ecmcForLoopPrintHelp() {
  printf("\n");
  printf(
    "       Use \"ecmcForLoop(<filename>, <macros>, <loopvar>, <from>, <to>, <step>)\" to loop execution of file with a changing loop variable.\n");
  printf("          <filename> : Filename to execute in for loop.\n");
  printf("          <macros>   : Macros to feed to execution of file.\n");
  printf(
    "          <loopvar   : Environment variable to use as index in for loop.\n");
  printf("          <from>     : <loopvar> start value.\n");
  printf("          <to>       : <loopvar> end value.\n");
  printf("          <step>     : Step to increase <loopvar> each loop cycle.\n");
  printf("\n");
}

int forLoopStep(const char *filename,
                const char *macros,
                const char *loopvar,
                int         i) {
  char buffer[256];

  memset(buffer, 0, sizeof(buffer));

  // Set loop variable
  snprintf(buffer, sizeof(buffer), "%d", i);
  epicsEnvSet(loopvar, buffer);

  // Execute file in iocsh
  iocshLoad(filename, macros);
  return 0;
}

/**
 * EPICS iocsh shell command: ecmcForLoop
*/
int ecmcForLoop(const char *filename,
                const char *macros,
                const char *loopvar,
                int         from,
                int         to,
                int         step) {
  if (!filename) {
    printf("Error: Filename missing.\n");
    ecmcForLoopPrintHelp();
    return asynError;
  }

  if ((strcmp(filename, "-h") == 0) || (strcmp(filename, "--help") == 0)) {
    ecmcForLoopPrintHelp();
    return asynSuccess;
  }

  if (!macros) {
    printf("Error: Macros missing.\n");
    ecmcForLoopPrintHelp();
    return asynError;
  }

  if (!loopvar) {
    printf("Error: Loop variable missing.\n");
    ecmcForLoopPrintHelp();
    return asynError;
  }

  // Check filename
  int fileExist = access(filename, 0) == 0;

  if (!fileExist) {
    printf("Error: File \"%s\" not found.\n", filename);
    return asynError;
  }

  if ((from > to) && (step > 0)) {
    printf("Error: from > to and step > 0.\n");
    return asynError;
  }

  if ((from < to) && (step < 0)) {
    printf("Error: from < to and step < 0.\n");
    return asynError;
  }

  // Start loop
  if (from <= to) {
    for (int i = from; i <= to; i += step) {
      forLoopStep(filename, macros, loopvar, i);
    }
  } else {
    for (int i = from; i >= to; i -= step) {
      forLoopStep(filename, macros, loopvar, i);
    }
  }
  return asynSuccess;
}

static const iocshArg initArg0_11 =
{ "Filename", iocshArgString };
static const iocshArg initArg1_11 =
{ "Macros", iocshArgString };
static const iocshArg initArg2_11 =
{ "Loop variable", iocshArgString };
static const iocshArg initArg3_11 =
{ "From", iocshArgInt };
static const iocshArg initArg4_11 =
{ "To", iocshArgInt };
static const iocshArg initArg5_11 =
{ "Step", iocshArgInt };


static const iocshArg *const initArgs_11[] = { &initArg0_11,
                                               &initArg1_11,
                                               &initArg2_11,
                                               &initArg3_11,
                                               &initArg4_11,
                                               &initArg5_11 };
static const iocshFuncDef    initFuncDef_11 =
{ "ecmcForLoop", 6, initArgs_11 };
static void initCallFunc_11(const iocshArgBuf *args) {
  ecmcForLoop(args[0].sval,
              args[1].sval,
              args[2].sval,
              args[3].ival,
              args[4].ival,
              args[5].ival);
}

/**
 * EPICS iocsh shell command: ecmcExit
*/
int ecmcExit(const char *help) {
  if ((strcmp(help, "-h") == 0) || (strcmp(help, "--help") == 0)) {
    printf("\n");
    printf("       Use \"ecmcExit\" to exit ecmc/EPICS.\n");
    printf("       Use \"ecmcExit -h | --help\" to get this help message.\n");
    printf("\n");
    return asynSuccess;
  }

  exit(EXIT_SUCCESS);
  return asynSuccess;
}

static const iocshArg initArg0_12 =
{ "help", iocshArgString };

static const iocshArg *const initArgs_12[]  = { &initArg0_12 };
static const iocshFuncDef    initFuncDef_12 = { "ecmcExit", 1, initArgs_12 };
static void initCallFunc_12(const iocshArgBuf *args) {
  ecmcExit(args[0].sval);
}

void ecmcIfPrintHelp() {
  printf("\n");
  printf(
    "       Use \"ecmcIf(<expression>)\" to evaluate the expression and set env varibales accordingly.\n");
  printf(
    "          <expression>  : Calculation expression (see exprTK for available functionality). Examples:\n");
  printf(
    "                          Simple expression:\"5.5+${TEST_SCALE}*sin(${TEST_ANGLE}/10)\".\n");
  printf(
    "                          Use of \"RESULT\" variable: \"if(${TEST_VAL}>5){RESULT:=100;}else{RESULT:=200;};\".\n");
  printf("\n");
  printf("Example:\n");
  printf("  ecmcIf(€{X}>${Y})\n");
  printf("  ${IF_TRUE} epicsEnvSet(\"RESULT\", \"X>Y\")\n");
  printf("  #-else\n");
  printf("  ${IF_FALSE} epicsEnvSet(\"RESULT\", \"Y>=X\")\n");
  printf("  ecmcEndIf()\n");
}

/** EPICS iocsh shell command: ecmcEpicsIf
 *  Evaluates an expression and sets an EPICS environment variables:
 *   IF_TRUE: to "" if expression is true otherwise "#-"
 *   IF_FALSE: to "#-" if expression is true otherwise ""
 * Inteded use (in iocsh):
 * ecmcIf(€{X}>${Y})
 * ${IF_TRUE} epicsEnvSet("RESULT", "X>Y")
 * ${IF_FALSE} epicsEnvSet("RESULT", "Y>=X")
 * ecmcEndIf()
*/

// the most current varaibles
const char *env_if   = "";
const char *env_else = "";

#define IF_TRUE_ENV_VAR "IF_TRUE"
#define IF_FALSE_ENV_VAR "IF_FALSE"

int ecmcIf(const char *expression,
           const char *env_if_str,
           const char *env_else_str) {
  if (!expression) {
    printf(
      "Error: \"expression\" missing.\n");
    ecmcIfPrintHelp();
    return asynError;
  }

  if (env_if_str) {
    env_if = env_if_str;
  } else {
    env_if = IF_TRUE_ENV_VAR;
  }

  if (env_else_str) {
    env_else = env_else_str;
  } else {
    env_else = IF_FALSE_ENV_VAR;
  }

  double resultDouble = 0;

  if (evalExprTK(expression, &resultDouble) != asynSuccess) {
    return asynError;
  }

  if (resultDouble) {
    epicsEnvSet(env_if,   "");
    epicsEnvSet(env_else, "#-");
  } else {
    epicsEnvSet(env_if,   "#-");
    epicsEnvSet(env_else, "");
  }

  return asynSuccess;
}

static const iocshArg initArg0_13 =
{ "Expression", iocshArgString };

static const iocshArg initArg1_13 =
{ "If env var name", iocshArgString };

static const iocshArg initArg2_13 =
{ "Else env var name", iocshArgString };

static const iocshArg *const initArgs_13[] = { &initArg0_13,
                                               &initArg1_13,
                                               &initArg2_13
};
static const iocshFuncDef    initFuncDef_13 =
{ "ecmcIf", 3, initArgs_13 };
static void initCallFunc_13(const iocshArgBuf *args) {
  ecmcIf(args[0].sval, args[1].sval, args[2].sval);
}

/** EPICS iocsh shell command: ecmcEndIf
 *  Resets env macros used by ecmcIf
 *   IF_TRUE: to "" if expression is true otherwise "#-"
 *   IF_FALSE: to "#-" if expression is true otherwise ""
 * Inteded use (in iocsh):
 * ecmcIf(€{X}>${Y})
 * ${IF_TRUE} epicsEnvSet("RESULT", "X>Y")
 * #-else
 * ${IF_FALSE} epicsEnvSet("RESULT", "Y>=X")
 * ecmcEndIf()
*/
int ecmcEndIf(const char *env_if_str, const char *env_else_str) {
  if (env_if_str) {
    epicsEnvUnset(env_if_str); // args
  } else if (env_if) {
    epicsEnvUnset(env_if);     // last call to ecmcIf()
  } else {
    epicsEnvUnset(IF_TRUE_ENV_VAR);  // Default
  }

  if (env_else_str) {
    epicsEnvUnset(env_else_str); // args
  } else if (env_else) {
    epicsEnvUnset(env_else);     // last call to ecmcIf()
  } else {
    epicsEnvUnset(IF_FALSE_ENV_VAR);  // Default
  }

  return asynSuccess;
}

static const iocshArg initArg0_14 =
{ "If env var name", iocshArgString };

static const iocshArg initArg1_14 =
{ "Else env var name", iocshArgString };

static const iocshArg *const initArgs_14[] = { &initArg0_14,
                                               &initArg1_14
};

static const iocshFuncDef initFuncDef_14 =
{ "ecmcEndIf", 2, initArgs_14 };
static void initCallFunc_14(const iocshArgBuf *args) {
  ecmcEndIf(args[0].sval, args[1].sval);
}


/** EPICS iocsh shell command: ecmcGetSlaveIdFromEcPath
 
 * Extracts slave id from ec<masterid>.s<slaveid>.<text>
*/

void ecmcGetSlaveIdFromEcPathHelp() {
  printf("\n");
  printf("       Use \"ecmcGetSlaveIdFromEcPath(<ec_path>,<var_result_slave_id>)\" to get the slave id from a ec-path\n");
  printf("          <ec_path>             : path to a etehrcat entry (ec<masterid>.s<slaveid>.<text>)\n");
  printf("          <var_result_slave_id> : result will be stored in this env variable.\n");
  printf("\n");
  printf("       If the slave id cannot be identified then \"<var_result_slave_id\"> will be set to \"-2\".\n");
  printf("\n");
  printf("       Example: ecmcGetSlaveIdFromEcPathHelp(ec1.s12.positionActual,RESULT)");
  printf("\n");
}

int ecmcGetSlaveIdFromEcPath(const char *ec_path, const char *env_var_str) {

  if(!ec_path || !env_var_str) {
    ecmcGetSlaveIdFromEcPathHelp();
    return asynError;
  }

  char textBuffer[ECMC_CMD_MAX_SINGLE_CMD_LENGTH];
  char resultBuffer[ECMC_CMD_MAX_SINGLE_CMD_LENGTH];
  memset(textBuffer, 0, sizeof(textBuffer));
  memset(resultBuffer, 0, sizeof(resultBuffer));
  int masterId = -1;
  int slaveId  = -2;
  epicsEnvSet(env_var_str, "-2");  // fail as default

  int nvals = sscanf(ec_path, "ec%d.s%d.%s", &masterId, &slaveId, textBuffer);
  if (nvals == 3) {
    size_t charCount = snprintf(resultBuffer,
                             sizeof(resultBuffer),
                             "%d",
                             slaveId);
    if (charCount >= sizeof(resultBuffer) - 1) {
      printf("Write buffer size exceeded, format results in a to long string.\n");
   
      return asynError;
    }
    
    // Write result
    epicsEnvSet(env_var_str, resultBuffer);
  }
  return asynSuccess;
}

static const iocshArg initArg0_15 =
{ "EtherCAT path (ec<mid>.s<sid>.<alias>)", iocshArgString };

static const iocshArg initArg1_15 =
{ "Env var name for return slave id", iocshArgString };

static const iocshArg *const initArgs_15[] = { &initArg0_15,
                                               &initArg1_15
};

static const iocshFuncDef initFuncDef_15 =
{ "ecmcGetSlaveIdFromEcPath", 2, initArgs_15 };
static void initCallFunc_15(const iocshArgBuf *args) {
  ecmcGetSlaveIdFromEcPath(args[0].sval, args[1].sval);
}

void ecmcAsynPortDriverRegister(void) {
  iocshRegister(&initFuncDef,    initCallFunc);
  iocshRegister(&initFuncDef_2,  initCallFunc_2);
  iocshRegister(&initFuncDef_3,  initCallFunc_3);
  iocshRegister(&initFuncDef_4,  initCallFunc_4);
  iocshRegister(&initFuncDef_5,  initCallFunc_5);
  iocshRegister(&initFuncDef_6,  initCallFunc_6);
  iocshRegister(&initFuncDef_7,  initCallFunc_7);
  iocshRegister(&initFuncDef_8,  initCallFunc_8);
  iocshRegister(&initFuncDef_9,  initCallFunc_9);
  iocshRegister(&initFuncDef_10, initCallFunc_10);
  iocshRegister(&initFuncDef_11, initCallFunc_11);
  iocshRegister(&initFuncDef_12, initCallFunc_12);
  iocshRegister(&initFuncDef_13, initCallFunc_13);
  iocshRegister(&initFuncDef_14, initCallFunc_14);
  iocshRegister(&initFuncDef_15, initCallFunc_15);
}

epicsExportRegistrar(ecmcAsynPortDriverRegister);
}
