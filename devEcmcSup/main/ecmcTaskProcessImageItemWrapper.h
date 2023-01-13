/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
*
*  ecmcTaskProcessImageItemWrapper.h
*
*  Created on: Dec 27, 2022
*      Author: anderssandstrom
*
*
* Class that implements taskinformation for an data item (ec entry or plc data item)
*
\*************************************************************************/
#ifndef ECMC_TASKPROCESSIMAGEITEMWRAPPER_H
#define ECMC_TASKPROCESSIMAGEITEMWRAPPER_H

enum appDataObjType {
  ECMC_EC_ENTRY_DATA_ITEM  = 0,
  ECMC_PLC_DATA_ITEM = 1,
};

class ecmcTaskProcessImageItemWrapper {
 public:
  /** ecmc ecmcExeObjWrapper class
   */

  ecmcTaskProcessImageItemWrapper(appDataObjType objType);
  ~ecmcTaskProcessImageItemWrapper();
   
  appDataObjType getObjectType();
  virtual char* getObjectName() = 0;
  
  int getTaskIndex();
  int setTaskIndex(int index);

private:
  appDataObjType objType_;
  int taskIndex_;  // taskIndex_ == 0 is the main task  
};

#endif  /* ECMC_TASKPROCESSIMAGEITEMWRAPPER_H */
