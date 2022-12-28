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
#include "ecmcDataItem.h"
#include "ecmcAsynPortDriver.h"
#include "ecmcDefinitions.h"
#include "inttypes.h"
#include <string>
#include <atomic>

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
           int                 threadIndex,
           int                 threadPriority,
           int                 threadAffinity,
           int                 threadStacksize,
           int                 threadOffsetMasterCycles,
           int                 threadSampleTimeMicroS,
           int                 masterSampleTimeMicroS,
           char*               threadName);
  
  ~ecmcTask();
  // trigg new execution for linked objects (in doWork())
  void execute(int ecmcError, int ecOK);
  bool isReady();
  int  getErrorCode();

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

  int                   threadIndex_;
  int                   threadPriority_;
  int                   threadAffinity_;
  int                   threadStacksize_;
  int                   threadOffsetMasterCycles_;
  int                   threadSampleTimeMicroS_;
  int                   masterSampleTimeMicroS_;
  int                   exeThreadAtMasterCycles_;
  int                   triggCounter_;
  int                   errorCode_;
  int                   destructs_;
  char*                 threadName_;


  epicsEvent            doWorkEvent_;  // Need to have on for each consumer..
  std::atomic<bool>     threadReady_;
  ecmcMainThreadDiag    threadDiag_;

  // asyn
  //void initAsyn();
  //void refreshAsynParams();
  ecmcAsynPortDriver *asynPortDriver_;
  //ecmcAsynDataItem *errorParam_;
  //ecmcAsynDataItem *connectedParam_;
};

#endif  /* ECMC_TASK_H_ */
