/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
*
*  ecmcThread.h
*
*  Created on: Dec 27, 2022
*      Author: anderssandstrom
*
*
* Class that implements a task (thread) that can execute ecmc objects:
* - axis
* - plc
*
\*************************************************************************/
#ifndef ECMC_TASK_H_
#define ECMC_TASK_H_

#include <stdexcept>
#include "ecmcAsynPortDriver.h"
#include "ecmcDefinitions.h"
#include "inttypes.h"
#include <string>
#include <atomic>
#include <time.h>
#include <vector>
#include "../main/ecmcExeObjWrapper.h"

/**
 * TODO
 *  - Add affinity
 *  - Add validation method
 *  - Add report function that list all info about the task and the objects it handles.
 *  - Add process image handling
 *  - ....
*/

class ecmcTask {
 public:

  /** ecmc ecmcTask class
   * This object can throw: 
   *    - bad_alloc
   *    - invalid_argument
   *    - runtime_error
   *    - out_of_range
  */
  ecmcTask(ecmcAsynPortDriver *asynPortDriver,
           int                 index,
           int                 priority,
           int                 affinity,
           int                 stacksize,
           int                 offsetTimeInMasterCycles,
           int                 sampleTimeInMasterCycles,
           int                 masterSampleTimeInNanoS);
  
  ~ecmcTask();
  // trigg new execution for linked objects (in doWork())
  void execute(int ecmcError, int ecOK);
  bool isReady();
  bool isNextCycleNewExe();
  int  getErrorCode();

  // Add object to task execution vector (can throw exceptions)
  void  appendObjToExeVector(ecmcExeObjWrapper *obj);
  std::vector<ecmcExeObjWrapper*> getExeVector();
  // main of work thread (calls doWork())
  void workThread();
  
  //virtual asynStatus    writeInt32(asynUser *pasynUser, epicsInt32 value);
  //virtual asynStatus    readInt32(asynUser *pasynUser, epicsInt32 *value);
  //virtual asynStatus    readInt8Array(asynUser *pasynUser, epicsInt8 *value, 
  //                                    size_t nElements, size_t *nIn);
  //virtual asynStatus    readFloat64(asynUser *pasynUser, epicsFloat64 *value);

 private:
  static std::string    to_string(int value);
  // here all linked objectes are executed (called by workThread())
  int                  doWork();
  
  int                   index_;
  int                   priority_;
  int                   affinity_;
  int                   stacksize_;
  int                   offsetTimeInMasterCycles_;
  int                   sampleTimeInNanoS_;
  int                   masterSampleTimeInNanoS_;
  int                   sampleTimeInMasterCycles_;
  int                   triggCounter_;
  int                   errorCode_;
  int                   destructs_;
  char*                 name_;
  int                   ecmcError_;
  int                   ecOK_;
  int                   exceedCounterold_;
  int                   exceedCounter_;
  epicsEvent            doWorkEvent_;  // Need to have on for each consumer..
  std::atomic<bool>     threadReady_;
  ecmcMainThreadDiag    threadDiag_;
  std::vector<ecmcExeObjWrapper*> exeVector_;
  
  // asyn
  //void initAsyn();
  //void refreshAsynParams();
  ecmcAsynPortDriver *asynPortDriver_;
  //ecmcAsynDataItem *errorParam_;
  //ecmcAsynDataItem *connectedParam_;
  char nameBuffer_[EC_MAX_OBJECT_PATH_CHAR_LENGTH];
};

#endif  /* ECMC_TASK_H_ */
