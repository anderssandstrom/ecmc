/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
*
*  ECMC_EXEOBJWRAPPER_H_.h
*
*  Created on: Dec 27, 2022
*      Author: anderssandstrom
*
*
* Class that implements a generic way to execute ecmc objects
* - axis
* - plc
*
\*************************************************************************/
#ifndef ECMC_EXEOBJWRAPPER_H_
#define ECMC_EXEOBJWRAPPER_H_

#include "ecmcDefinitions.h"


class ECMC_EXEOBJWRAPPER_H_ {
 public:

  /** ecmc ecmcExeObjWrapper class
   */
  
  ecmcExeObjWrapper();
  ~ecmcExeObjWrapper();
  // trigg new execution for linked objects 
  void execute(int ecmcError, int ecOK);
};

#endif  /* ECMC_EXEOBJWRAPPER_H_ */
