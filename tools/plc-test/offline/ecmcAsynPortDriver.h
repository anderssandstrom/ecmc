// Publication-only offline adapter, not an implementation of EPICS/asyn.
// Data storage algorithms remain in the production ecmcDataStorage.cpp.
#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include "asynDriver.h"
enum asynParamType {
  asynParamFloat64Array, asynParamInt32, asynParamUInt32Digital,
  asynParamFloat64, asynParamInt64, asynParamInt8Array
};
class ecmcAsynDataItem {
 public:
  void setEcmcDataPointer(uint8_t *, size_t) {}
  void setAllowWriteToEcmc(bool) {}
  void addSupportedAsynType(asynParamType) {}
  void refreshParam(int) {}
  void refreshParamRT(int) {}
  void refreshParam(int, uint8_t *, size_t) {}
  void refreshParamRT(int, uint8_t *, size_t) {}
  template<typename Callback> void setExeCmdFunctPtr(Callback, void *) {}
};
class ecmcAsynPortDriver {
 public:
  ecmcAsynDataItem *addNewAvailParam(const char *, asynParamType,
                                    uint8_t *, size_t, int, int) {
    items_.emplace_back(new ecmcAsynDataItem);
    return items_.back().get();
  }
  void callParamCallbacks(int, int) {}
 private:
  std::vector<std::unique_ptr<ecmcAsynDataItem>> items_;
};
