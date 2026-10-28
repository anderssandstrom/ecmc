// Publication-only offline adapter, not an implementation of EPICS/asyn.
// Data storage algorithms remain in the production ecmcDataStorage.cpp.
#pragma once
#include <cstdint>
#include <memory>
#include <vector>
enum asynParamType {
  asynParamFloat64Array, asynParamInt32, asynParamUInt32Digital
};
class ecmcAsynDataItem {
 public:
  void setEcmcDataPointer(uint8_t *, size_t) {}
  void setAllowWriteToEcmc(bool) {}
  void addSupportedAsynType(asynParamType) {}
  void refreshParam(int) {}
  void refreshParamRT(int) {}
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
