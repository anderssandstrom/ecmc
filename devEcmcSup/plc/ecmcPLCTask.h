/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcPLCTask.cpp
*
*  Created on: Oct 4, 2018
*      Author: anderssandstrom
*
\*************************************************************************/

#ifndef ECMC_PLC_TASK_H_
#define ECMC_PLC_TASK_H_

#include <string>
#include <vector>
#include "ecmcAsynPortDriver.h"
#include "exprtkWrap.h"
#include "ecmcDefinitions.h"
#include "ecmcAxisBase.h"
#include "ecmcAxisGroup.h"
#include "ecmcEc.h"
#include "ecmcEcEntry.h"  // Bit macros
#include "ecmcPluginLib.h"
#include "ecmcPLCLib.h"
#include "ecmcPLCDataIF.h"
#include "ecmcLookupTable.h"

#define ECMC_MAX_PLC_VARIABLES 1024
#define ECMC_MAX_PLC_VARIABLES_NAME_LENGTH 1024

#include "ecmcPLCTaskErrors.h"


class ecmcPLCTask : public ecmcError {
public:
  explicit ecmcPLCTask(int                 plcIndex,
                       int                 skipCycles,
                       double              mcuFreq,
                       ecmcAsynPortDriver *asynPortDriver_);
  ~ecmcPLCTask();
  bool         getCompiled();
  int          validate();
  int          execute(bool ecOK);
  std::string* getExpr();
  std::string* getRawExpr();
  int          appendRawExpr(const char *exprStr);
  int          addExprLine(const char *exprStr);
  int          clearExpr();
  int          clearRawExpr();
  int          compile();
  int          addAndReisterGlobalVar(ecmcPLCDataIF *dataIF);
  int          addAndRegisterLocalVar(char *localVarStr);
  int          setPluginPointer(ecmcPluginLib *plugin,
                                int            index);
  int          parseFunctions(const char *exprStr);
  int          getFirstScanDone();
  int          readStaticPLCVar(const char *varName,
                                double     *data);
  int          writeStaticPLCVar(const char *varName,
                                 double      data);
  int          findLocalVar(const char     *varName,
                            ecmcPLCDataIF **outDataIF);
  double       getSampleTime();
  int          getNewExpr();
  int          addLib(ecmcPLCLib* lib);
  

private:
  void initVars();
  void rebuildVariableAccessLists();
  int  initAsyn(int plcIndex);
  void updateAsyn();
  int  varExist(char *varName);
  int  globalVarExist(const char *varName);
  int  localVarExist(const char *varName);
  int  getPLCErrorID();  // from PLC Code
  bool findMcFunction(const char *exprStr);
  bool findEcFunction(const char *exprStr);
  bool findDsFunction(const char *exprStr);
  bool findFileIOFunction(const char *exprStr);
  bool findPluginFunction(ecmcPluginLib *plugin,
                          const char    *exprStr);
  bool findPluginConstant(ecmcPluginLib *plugin,
                          const char    *exprStr);
  bool findMiscFunction(const char *exprStr);
  int  loadMcLib();
  int  loadEcLib();
  int  loadDsLib();
  int  loadFileIOLib();
  int  loadPluginLib(ecmcPluginLib *plugin);
  int  loadMiscLib();

  std::string exprStr_;
  std::string exprStrRaw_; // Before compile and preprocess
  bool compiled_;
  exprtkWrap *exprtk_;
  ecmcPLCDataIF *globalArray_[ECMC_MAX_PLC_VARIABLES];
  ecmcPLCDataIF *localArray_[ECMC_MAX_PLC_VARIABLES];
  ecmcPLCDataIF *readArray_[2 * ECMC_MAX_PLC_VARIABLES];
  ecmcPLCDataIF *globalWriteArray_[ECMC_MAX_PLC_VARIABLES];
  int readVariableCount_;
  int globalWriteVariableCount_;
  int globalVariableCount_;
  int localVariableCount_;
  int inStartup_;
  int firstScanDone_;
  int plcIndex_;
  int skipCycles_;
  int skipCyclesCounter_;
  double plcScanTimeInSecs_;
  int libMcLoaded_;
  int libEcLoaded_;
  int libDsLoaded_;
  int libFileIOLoaded_;
  int libMiscLoaded_;
  int libPluginsLoaded_[ECMC_MAX_PLUGINS];

  ecmcAsynPortDriver *asynPortDriver_;
  int newExpr_;
  ecmcAsynDataItem *asynParamExpr_;
  double mcuFreq_;
  ecmcPluginLib *plugins_[ECMC_MAX_PLUGINS];
  std::vector<ecmcPLCLib*> functionLibs_;

};

#endif  /* ECMC_PLC_TASK_H_ */
