/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
*
*  ecmcTaskProcessImageItemWrapper.cpp
*
*  Created on: Dec 27, 2022
*      Author: anderssandstrom
*
\*************************************************************************/

#include "ecmcTaskProcessImageItemWrapper.h"
#include "stdio.h"


/** ecmc ecmcTaskProcessImageItemWrapper class
*/
ecmcTaskProcessImageItemWrapper::ecmcTaskProcessImageItemWrapper(appDataObjType objType) {

  objType_ = objType;
  taskIndex_ = -1;
}

ecmcTaskProcessImageItemWrapper::~ecmcTaskProcessImageItemWrapper() {
  
}

appDataObjType ecmcTaskProcessImageItemWrapper::getObjectType() {
  return objType_;
}

int ecmcTaskProcessImageItemWrapper::getTaskIndex() {
  return taskIndex_;
}

int ecmcTaskProcessImageItemWrapper::setTaskIndex(int index) {
  taskIndex_ = index;
  return 0;
}
