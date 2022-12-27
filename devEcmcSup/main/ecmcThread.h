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
\*************************************************************************/
#ifndef ECMC_THREAD_H_
#define ECMC_THREAD_H_

#include <stdexcept>
#include "ecmcDataItem.h"
#include "ecmcAsynPortDriver.h"
#include "inttypes.h"
#include <string>
#include <atomic>

class ecmcThread {
 public:

  /** ecmc ecmcThread class
   * This object can throw: 
   *    - bad_alloc
   *    - invalid_argument
   *    - runtime_error
   *    - out_of_range
  */
  ecmcThread(ecmcAsynPortDriver *asynPortDriver,
             int threadIndex,
             int threadPriority,
             int threadAffinity,
             int threadStacksize,
             char* threadName,
             double exeSampelTimeMs);
  ~ecmcThread();
  // trigg new execution for linked objects (in doWork())
  void triggWork();
  
  // main of work thread (calls doWork())
  void workThread();
  
  // here all linked objectes are executed (called by workThread())
  void doWork();

  //virtual asynStatus    writeInt32(asynUser *pasynUser, epicsInt32 value);
  //virtual asynStatus    readInt32(asynUser *pasynUser, epicsInt32 *value);
  //virtual asynStatus    readInt8Array(asynUser *pasynUser, epicsInt8 *value, 
  //                                    size_t nElements, size_t *nIn);
  //virtual asynStatus    readFloat64(asynUser *pasynUser, epicsFloat64 *value);

 private:
  static std::string    to_string(int value);
  void                  doWork();

  
  int                   threadIndex_;
  int                   threadPriority_;
  int                   threadAffinity_;
  int                   threadStacksize_;
  char*                 threadName_;
  double                threadSampleTimeMs_;
  epicsEvent            doWorkEvent_;  
  std::atomic<bool>     threadReady_;
  //ASYN
  void initAsyn();
  void refreshAsynParams();
  ecmcAsynPortDriver *asynPortDriver_;
  ecmcAsynDataItem *errorParam_;
  ecmcAsynDataItem *connectedParam_;
};

#endif  /* ECMC_THREAD_H_ */
