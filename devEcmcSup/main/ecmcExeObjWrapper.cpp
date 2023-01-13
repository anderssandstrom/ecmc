/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
*
*  ecmcExeObjWrapper.cpp
*
*  Created on: Dec 27, 2022
*      Author: anderssandstrom
*
\*************************************************************************/

#include "ecmcExeObjWrapper.h"
#include "stdio.h"
#include <cstring>
#include "../com/ecmcOctetIF.h"

/** ecmc ecmcExeObjWrapper class
*/
ecmcExeObjWrapper::ecmcExeObjWrapper(appExeObjType objType,int index) {
  objIndex_ = index;
  objType_ = objType;
  taskIndex_ = -1;
  processImage_.clear();

  // Build object name
  memset(nameBuffer_,0,sizeof(nameBuffer_));
  const char* prefix="error";
  switch(objType_) {
    case  ECMC_AXIS:
      prefix=ECMC_AX_STR;
    break;
    case ECMC_PLC:
      prefix=ECMC_PLC_DATA_STR;
    break;
    case ECMC_EVENT:
      prefix=ECMC_EVENT_STR;
    break;
    case ECMC_PLUGIN:
      prefix=ECMC_PLUGIN_STR;
    break;
  }

  snprintf(nameBuffer_,
           sizeof(nameBuffer_),
           "%s%d",
           prefix,
           objIndex_);
  name_ = nameBuffer_;  
}

ecmcExeObjWrapper::~ecmcExeObjWrapper() {
  
}

void ecmcExeObjWrapper::execute(int ecmcError, int ecOK) {

  // Most exe functions take ecOK as arg except for plugins taking ecmcError
  if(objType_==ECMC_PLUGIN) {
    exeRTFunc(ecmcError);
  } else {
    exeRTFunc(ecOK);
  }
}

appExeObjType ecmcExeObjWrapper::getObjectType() {
  return objType_;
}

int ecmcExeObjWrapper::getTaskIndex() {
  return taskIndex_;
}

int ecmcExeObjWrapper::setTaskIndex(int index) {
  taskIndex_ = index;
  return 0;
}

void ecmcExeObjWrapper::printProcessImage() {
  printf("Process image for object %s:\n",name_);
  for(int i=0; i<(int)processImage_.size();++i) {    
    printf("    %s\n",processImage_[i]->getObjectName());
  }
}

std::vector<ecmcTaskProcessImageItemWrapper*> ecmcExeObjWrapper::
            getProcessImage() {
  return processImage_;
}
