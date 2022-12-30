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


/** ecmc ecmcExeObjWrapper class
*/
ecmcExeObjWrapper::ecmcExeObjWrapper(appExeObjType objType) {

  objType_ = objType;
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
