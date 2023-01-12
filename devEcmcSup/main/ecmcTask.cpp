/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
*
*  ecmcThread.cpp
*
*  Created on: Dec 27, 2022
*      Author: anderssandstrom
*
\*************************************************************************/

#include <sstream>
#include "ecmcAsynPortDriver.h"
#include "ecmcAsynPortDriverUtils.h"
#include "ecmcTask.h"
#include "epicsThread.h"
#include "../com/ecmcOctetIF.h"  // LOG macros

// Start worker for socket read()
void f_worker(void *obj) {
  if(!obj) {
    printf("%s/%s:%d: Error: Worker read thread ecmcThread object NULL..\n",
            __FILE__, __FUNCTION__, __LINE__);
    return;
  }
  ecmcTask * taskObj = (ecmcTask*)obj;
  taskObj->workThread();
}

/** ecmc ecmcTask class
 * This object can throw: 
 *    - bad_alloc
 *    - invalid_argument
 *    - runtime_error
*/
ecmcTask::ecmcTask(ecmcAsynPortDriver *asynPortDriver,
                   int                 index,
                   int                 priority,
                   int                 affinity,
                   int                 stacksize,
                   int                 offsetTimeInMasterCycles,
                   int                 sampleTimeInMasterCycles,
                   int                 masterSampleTimeInNanoS) {

  // Init
  index_                    = index;  
  priority_                 = priority;
  affinity_                 = affinity;
  stacksize_                = stacksize;
  offsetTimeInMasterCycles_ = offsetTimeInMasterCycles;
  masterSampleTimeInNanoS_  = masterSampleTimeInNanoS;
  asynPortDriver_           = asynPortDriver;
  triggCounter_             = offsetTimeInMasterCycles_;
  sampleTimeInMasterCycles_ = sampleTimeInMasterCycles-1;
  sampleTimeInNanoS_        = sampleTimeInMasterCycles_ * masterSampleTimeInNanoS; // overflow?! Only valid for up to approx 5 seks
  exceedCounterold_         = 0;
  threadReady_              = 1;
  errorCode_                = 0;
  destructs_                = 0;
  exceedCounter_            = 0;
  ecOK_                     = 0;
  ecmcError_                = 0;
  
  threadDiag_.latency_min_ns  = 0xffffffff;
  threadDiag_.latency_max_ns  = 0;
  threadDiag_.period_min_ns   = 0xffffffff;
  threadDiag_.period_max_ns   = 0;
  threadDiag_.exec_min_ns     = 0xffffffff;
  threadDiag_.exec_max_ns     = 0;
  threadDiag_.send_min_ns     = 0xffffffff;
  threadDiag_.send_max_ns     = 0;

  if(sampleTimeInMasterCycles_ < 0) {
    sampleTimeInMasterCycles_ = 0;
  }

  if(priority_ < 0) {
    priority_= ECMC_PRIO_HIGH-1;
  }

  if(stacksize_ < 0) {
    stacksize_= ECMC_STACK_SIZE;
  }

  memset(nameBuffer_,0,sizeof(nameBuffer_));  
  unsigned int charCount = 0;  
  
  charCount = snprintf(nameBuffer_,
                       sizeof(nameBuffer_),
                       ECMC_RT_THREAD_NAME"_w%d",
                       index_);
  if (charCount >= sizeof(nameBuffer_) - 1) {
    LOGERR("ERROR: Failed to create thread name, buffer to small.\n");
    throw std::runtime_error("Error: Failed create thread name, buffer to small.");    
  }

  name_ = nameBuffer_;
  
  // Create worker thread
  if(epicsThreadCreate(name_, priority_, stacksize_, f_worker, this) == NULL) {    
    LOGERR(
      "ERROR: Can't create high priority thread, fallback to low priority.\n");
    priority_ = ECMC_PRIO_LOW;
    if(epicsThreadCreate(name_, priority_, stacksize_, f_worker, this) == NULL) {
      throw std::runtime_error("Error: Failed create  worker thread.");    
    }    
  }

  exeVector_.clear();

  // TODO implement affinity

  //initAsyn();
}

ecmcTask::~ecmcTask() {
  destructs_ = 1;
  doWorkEvent_.signal();

}

void ecmcTask::workThread() {

  LOGINFO("INFO: Task %s (%d) started. Waiting for work...\n", name_, index_);

  while(true) {
    
    if(destructs_) {
      return;
    }

    // wait for new trigg event
    threadReady_ = true;
    doWorkEvent_.wait();

    errorCode_ = doWork();

    // work done
  }
}

int ecmcTask::doWork() {
  
  for (int i = 0; i < (int)exeVector_.size(); ++i) {
    if(exeVector_[i] != NULL) {
      exeVector_[i]->execute(ecmcError_,ecOK_);
    }
  }
  
  if(exceedCounterold_ != exceedCounter_) {
    printf(" Thread %s (%d) did not finish in time, exceed counter %d\n",name_,index_,exceedCounter_);
  }

  exceedCounterold_ = exceedCounter_;
  return 0;
}

// Let main thread trigg work
// Only call method from main thread
void ecmcTask::execute(int ecmcError, int ecOK) {
  ecmcError_ = ecmcError;
  ecOK_      = ecOK;
  if(triggCounter_ == 0) {
    if(threadReady_.load()) {
      threadReady_ = false;
      doWorkEvent_.signal();  // need one event per thread since only one thread is released at every signal
      // if not ready then one cycle is skipped... This is registered by increment of exceedCounter_
    }    
    triggCounter_ = sampleTimeInMasterCycles_;
  } else if(triggCounter_ > 0) {
    triggCounter_--;
  }
}

// let main thread know if work is done. 
// Only call method from main thread
bool ecmcTask::isReady() {
  if(triggCounter_ == 0) {
    // should process again at next call to execute
    if (!threadReady_.load()) {
      exceedCounter_++;
    }
  }
  return threadReady_.load();
}

// should this thread be ready now?!
// next main thread cycle a new execution should start
bool ecmcTask::isNextCycleNewExe() {
  return triggCounter_ == 0;
}

int ecmcTask::getErrorCode() {
  return errorCode_;
}

void ecmcTask::appendObjToExeVector(ecmcExeObjWrapper *obj) {
  if(obj == NULL) {
    return;
  }

  obj->setTaskIndex(index_);
  // queue object for execution
  exeVector_.push_back(obj);

}

//void ecmcTask::initAsyn() {

  //ecmcAsynPortDriver *ecmcAsynPort = (ecmcAsynPortDriver *)getEcmcAsynPortDriver();
  //if(!ecmcAsynPort) {
  //  printf("ERROR: ecmcAsynPort NULL.");
  //  throw std::runtime_error( "ERROR: ecmcAsynPort NULL." );
  //}
 //
  //// Add resultdata "plugin.can.read.error"
  //std::string paramName = ECMC_PLUGIN_ASYN_PREFIX + std::string(".com.error");
//
  //errorParam_ = ecmcAsynPort->addNewAvailParam(
  //                                        paramName.c_str(),     // name
  //                                        asynParamInt32,        // asyn type 
  //                                        (uint8_t*)&errorCode_, // pointer to data
  //                                        sizeof(errorCode_),    // size of data
  //                                        ECMC_EC_U32,           // ecmc data type
  //                                        0);                    // die if fail
//
  //if(!errorParam_) {
  //  printf("ERROR: Failed create asyn param for data.");
  //  throw std::runtime_error( "ERROR: Failed create asyn param for: " + paramName);
  //}
  //errorParam_->setAllowWriteToEcmc(false);  // need to callback here
  //errorParam_->refreshParam(1); // read once into asyn param lib
  //ecmcAsynPort->callParamCallbacks(ECMC_ASYN_DEFAULT_LIST, ECMC_ASYN_DEFAULT_ADDR);
//
  //// Add resultdata "plugin.can.read.connected"
  //paramName = ECMC_PLUGIN_ASYN_PREFIX + std::string(".com.connected");
//
  //connectedParam_ = ecmcAsynPort->addNewAvailParam(
  //                                        paramName.c_str(),     // name
  //                                        asynParamInt32,        // asyn type 
  //                                        (uint8_t*)&connected_, // pointer to data
  //                                        sizeof(connected_),    // size of data
  //                                        ECMC_EC_U32,           // ecmc data type
  //                                        0);                    // die if fail
//
  //if(!connectedParam_) {
  //  printf("ERROR: Failed create asyn param for connected.");
  //  throw std::runtime_error( "ERROR: Failed create asyn param for: " + paramName);
  //}
  //connectedParam_->setAllowWriteToEcmc(false);  // need to callback here
  //connectedParam_->refreshParam(1); // read once into asyn param lib
  //ecmcAsynPort->callParamCallbacks(ECMC_ASYN_DEFAULT_LIST, ECMC_ASYN_DEFAULT_ADDR); 
//}

// TODO!!! Fel namn nedan
//void ecmcTask::refreshAsynParams(int force) {
//  
//  if(!asynPort->getAllowRtThreadCom()){
//    return;
//  }
//
//  int errorCode=mainAsynParams[ECMC_ASYN_MAIN_PAR_LATENCY_MIN_ID]->refreshParamRT(force);
//  if(errorCode==0){ //Reset after successfull write      
//    threadDiag.latency_min_ns  = 0xffffffff;
//  }
//  errorCode=mainAsynParams[ECMC_ASYN_MAIN_PAR_LATENCY_MAX_ID]->refreshParamRT(force);
//  if(errorCode==0){
//    threadDiag.latency_max_ns  = 0;
//  }
//  errorCode=mainAsynParams[ECMC_ASYN_MAIN_PAR_PERIOD_MIN_ID]->refreshParamRT(force);
//  if(errorCode==0){
//    threadDiag.period_min_ns  = 0xffffffff;
//  }
//  errorCode=mainAsynParams[ECMC_ASYN_MAIN_PAR_PERIOD_MAX_ID]->refreshParamRT(force);
//  if(errorCode==0){
//    threadDiag.period_max_ns  = 0;
//  }
//  errorCode=mainAsynParams[ECMC_ASYN_MAIN_PAR_EXECUTE_MIN_ID]->refreshParamRT(force);
//  if(errorCode==0){
//    threadDiag.exec_min_ns  = 0xffffffff;
//  }
//  errorCode=mainAsynParams[ECMC_ASYN_MAIN_PAR_EXECUTE_MAX_ID]->refreshParamRT(force);
//  if(errorCode==0){
//    threadDiag.exec_max_ns  = 0;
//  }
//  errorCode=mainAsynParams[ECMC_ASYN_MAIN_PAR_SEND_MIN_ID]->refreshParamRT(force);
//  if(errorCode==0){
//    threadDiag.send_min_ns  = 0xffffffff;
//  }    
//  errorCode=mainAsynParams[ECMC_ASYN_MAIN_PAR_SEND_MAX_ID]->refreshParamRT(force);
//  if(errorCode==0){
//    threadDiag.send_max_ns  = 0;    
//  }
//  
//  controllerErrorOld = controllerError;
//}
