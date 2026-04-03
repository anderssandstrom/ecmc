/*************************************************************************\
* Copyright (c) 2026 Paul Scherrer Institute
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcNativeLogicLib.h
*
\*************************************************************************/

#ifndef ECMC_NATIVE_LOGIC_LIB_H_
#define ECMC_NATIVE_LOGIC_LIB_H_

#include "ecmcError.h"

class ecmcNativeLogicLib : public ecmcError {
 public:
  struct Impl;

  explicit ecmcNativeLogicLib(int index);
  ~ecmcNativeLogicLib() override;

  int load(const char* libFilenameWP, const char* configStr);
  void unload();
  void report();

  int exeRTFunc(int controllerErrorCode);
  int exeEnterRTFunc();
  int exeExitRTFunc();

  const char* getPortName() const;

 private:
  Impl* impl_;
};

#endif
