/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
*
*  ecmcExeObjWrapper.h
*
*  Created on: Dec 27, 2022
*      Author: anderssandstrom
*
*
* Class that implements a generic way to execute ecmc objects
* - axis
* - plc
* - event
* - plugin
*
\*************************************************************************/
#ifndef ECMC_EXEOBJWRAPPER_H_
#define ECMC_EXEOBJWRAPPER_H_

#include "ecmcDefinitions.h"
#include "ecmcTaskProcessImageItemWrapper.h"
#include <vector>

// Motion
enum appExeObjType {
  ECMC_AXIS  = 0,
  ECMC_PLC = 1,
  ECMC_EVENT = 2,
  ECMC_PLUGIN = 3
};

class ecmcExeObjWrapper {
 public:

  /** ecmc ecmcExeObjWrapper class
   */

  ecmcExeObjWrapper(appExeObjType objType,int index);
  ~ecmcExeObjWrapper();
  
  // The function call in derived class that executes rt code
  virtual int exeRTFunc(int arg) = 0;
  
  // Collect all ec entries and PLC data objects in an exe object
  virtual void buildProcessImage() = 0;
  
  appExeObjType getObjectType();
  //int getObjectIndex()

  // trigg new execution for linked objects 
  void execute(int ecmcError, int ecOK);
  int getTaskIndex();
  int setTaskIndex(int index);
  void printProcessImage();

  
  std::vector<ecmcTaskProcessImageItemWrapper*> getProcessImage();

protected:
  std::vector<ecmcTaskProcessImageItemWrapper*> processImage_;

private:
  appExeObjType objType_;
  int objIndex_;
  int taskIndex_;  // taskIndex_ == 0 is the main task
  char nameBuffer_[EC_MAX_OBJECT_PATH_CHAR_LENGTH];
  char *name_;
};

#endif  /* ECMC_EXEOBJWRAPPER_H_ */
