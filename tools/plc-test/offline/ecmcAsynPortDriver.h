// Publication-only offline adapter, not an implementation of EPICS/asyn.
// Data storage algorithms remain in the production ecmcDataStorage.cpp.
#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <map>
#include <string>
#include "asynDriver.h"
enum asynParamType {
  asynParamFloat64Array, asynParamInt32, asynParamUInt32Digital,
  asynParamFloat64, asynParamInt64, asynParamInt8Array
};
class ecmcAsynDataItem {
 public:
  uint8_t *data = nullptr;
  size_t bytes = 0;
  bool writable = false;
  void setEcmcDataPointer(uint8_t *value, size_t size) { data = value; bytes = size; }
  void setAllowWriteToEcmc(bool value) { writable = value; }
  void setEcmcDataSize(size_t size) { bytes = size; }
  void setEcmcBitCount(size_t) {}
  void setEcmcMinValueInt(int64_t) {}
  void setEcmcMaxValueInt(int64_t) {}
  void addSupportedAsynType(asynParamType) {}
  void refreshParam(int) {}
  void refreshParamRT(int) {}
  void refreshParam(int, uint8_t *, size_t) {}
  void refreshParamRT(int, uint8_t *, size_t) {}
  template<typename Callback> void setExeCmdFunctPtr(Callback, void *) {}
};
class ecmcAsynPortDriver {
 public:
  ecmcAsynDataItem *addNewAvailParam(const char *name, asynParamType,
                                    uint8_t *data, size_t bytes, int, int) {
    items_.emplace_back(new ecmcAsynDataItem);
    items_.back()->setEcmcDataPointer(data, bytes);
    names_[name] = items_.back().get();
    return items_.back().get();
  }
  ecmcAsynDataItem *find(const char *name) {
    auto found = names_.find(name); return found == names_.end() ? nullptr : found->second;
  }
  void callParamCallbacks(int, int) {}
 private:
  std::vector<std::unique_ptr<ecmcAsynDataItem>> items_;
  std::map<std::string, ecmcAsynDataItem *> names_;
};
