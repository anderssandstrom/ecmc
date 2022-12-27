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

// Needed to get headers in ecmc right...
#define ECMC_IS_PLUGIN

#include <sstream>
#include "ecmcPluginClient.h"
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

/** ecmc ecmcThread class
 * This object can throw: 
 *    - bad_alloc
 *    - invalid_argument
 *    - runtime_error
*/
ecmcThread::ecmcThread(ecmcAsynPortDriver *asynPortDriver,
                       int threadIndex,
                       int threadPriority,
                       int threadAffinity,
                       int threadStacksize,
                       double threadSampleTimeMs_,
                       char* threadName) {

  // Init
  threadIndex_        = threadIndex;
  threadName_         = threadName;
  threadPriority_     = threadPrio;
  threadAffinity_     = threadAffinity;
  threadStacksize_    = threadStacksize;
  threadSampleTimeMs_ = threadSampleTimeMs_;
  asynPortDriver_     = asynPortDriver;


  doneLock_.test_and_set();   // make sure only one sdo is accessing the bus at the same time
  doneLock_.clear();

  // Create worker thread
  if(epicsThreadCreate(threadName_, threadPriority_, threadStacksize_, f_worker, this) == NULL) {
    LOGERR("ERROR: Can't create high priority thread, fallback to low priority\n");    
    throw std::runtime_error("Error: Failed create  worker thread.");    
  }

  // TODO implement affinity

  initAsyn();
}

ecmcThread::~ecmcThread() {
  
  doWorkEvent_.signal();

}

void ecmcThread::workThread() {

  while(true) {
    
    if(destructs_) {
      return;
    }

    // wait for new trigg
    doWorkEvent_.wait();

    doWork();

    // work done
    threadReady_ = true;
  }
}

void ecmcThread::doWork() {

  // Do work here
  
  // obj->execute()..

}

// Let main thread trigg work
void ecmcThread::triggWork() {
  threadReady_ = false;
  doWorkEvent_.signal();
}

// let main thread know if work is done
bool ecmcThread::getReady() {
  // hmm, this is probbaly bad..
  return threadReady_.load()
}

void ecmcThread::initAsyn() {

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
}

// only refresh from "execute" thread
void ecmcThread::refreshAsynParams() {
//  if(refreshNeeded_) {
//    connectedParam_->refreshParamRT(1); // read once into asyn param lib
//    errorParam_->refreshParamRT(1); // read once into asyn param lib
//  }
//  refreshNeeded_ = 0;
}