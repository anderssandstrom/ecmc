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
#include "epicsThread.h"

// Start worker for socket read()
void f_worker(void *obj) {
  if(!obj) {
    printf("%s/%s:%d: Error: Worker read thread ecmcThread object NULL..\n",
            __FILE__, __FUNCTION__, __LINE__);
    return;
  }
  ecmcThread * threadObj = (ecmcThread*)obj;
  threadObj->workThread();
}

/** ecmc ecmcTask class
 * This object can throw: 
 *    - bad_alloc
 *    - invalid_argument
 *    - runtime_error
*/
ecmcTask::ecmcTask(ecmcAsynPortDriver *asynPortDriver,
                   int                 threadIndex,
                   int                 threadPriority,
                   int                 threadAffinity,
                   int                 threadStacksize,
                   int                 threadOffsetMasterCycles,
                   int                 threadSampleTimeMicroS,
                   int                 masterSampleTimeMicroS,
                   char*               threadName) {

  // Init
  threadIndex_              = threadIndex;
  threadName_               = threadName;
  threadPriority_           = threadPrio;
  threadAffinity_           = threadAffinity;
  threadStacksize_          = threadStacksize;
  threadOffsetMasterCycles_ = threadOffsetMasterCycles;
  threadSampleTimeMicroS_   = threadSampleTimeMicroS;
  masterSampleTimeMicroS_   = masterSampleTimeMicroS;
  asynPortDriver_           = asynPortDriver;
  triggCounter_             = threadOffsetMasterCycles_;
  exeThreadAtMasterCycles_  = threadSampleTimeMicroS/masterSampleTimeMicroS;
  threadReady_              = 1;

  if(masterSampleTimeMicroS>threadSampleTimeMicroS){
    throw std::invalid_argument("Error: thread sample rate cannot be faster than master sample rate.");    
  }

  threadDiag_.latency_min_ns  = 0xffffffff;
  threadDiag_.latency_max_ns  = 0;
  threadDiag_.period_min_ns   = 0xffffffff;
  threadDiag_.period_max_ns   = 0;
  threadDiag_.exec_min_ns     = 0xffffffff;
  threadDiag_.exec_max_ns     = 0;
  threadDiag_.send_min_ns     = 0xffffffff;
  threadDiag_.send_max_ns     = 0;
  
  // Create worker thread
  if(epicsThreadCreate(threadName_, threadPriority_, threadStacksize_, f_worker, this) == NULL) {
    LOGERR("ERROR: Can't create thread.\n");    
    throw std::runtime_error("Error: Failed create  worker thread.");    
  }

  // TODO implement affinity

  //initAsyn();
}

ecmcTask::~ecmcTask() {
  
  doWorkEvent_.signal();

}

void ecmcTask::workThread() {

  while(true) {
    
    if(destructs_) {
      return;
    }

    // wait for new trigg event
    doWorkEvent_.wait();

    doWork();

    // work done
    threadReady_ = true;
  }
}

void ecmcTask::doWork() {

  // Do work here
  
  // obj->execute()..

}

// Let main thread trigg work
void ecmcTask::triggWork() {
  if(triggCounter_ == 0) {
    threadReady_ = false;
    doWorkEvent_.signal();  // need one event per thread since only one thread is released at every signal
    triggCounter_ = exeThreadAtMasterCycles_;
  }
  triggCounter_--;
}

// let main thread know if work is done
bool ecmcTask::isReady() {
  // hmm, this is probbaly bad..
  return threadReady_.load()
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
