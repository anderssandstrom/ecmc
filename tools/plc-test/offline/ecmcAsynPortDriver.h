// Publication-only offline adapter, not an implementation of EPICS/asyn.
// Data storage algorithms remain in the production ecmcDataStorage.cpp.
#pragma once
#include <cstdint>
#include <cstring>
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
  int rtCalls = 0, sampleCycles = 1, rtPublications = 0;
  uint64_t lastRTValue = 0;
  void setEcmcDataPointer(uint8_t *value, size_t size) { data = value; bytes = size; }
  void setAllowWriteToEcmc(bool value) { writable = value; }
  void setEcmcDataSize(size_t size) { bytes = size; }
  void setEcmcBitCount(size_t) {}
  void setEcmcMinValueInt(int64_t) {}
  void setEcmcMaxValueInt(int64_t) {}
  void addSupportedAsynType(asynParamType) {}
  void refreshParam(int) {}
  // Controlled EPICS sampling contract: sampleCycles RT calls per publication.
  void refreshParamRT(int force) {
    ++rtCalls;
    if (force || rtCalls % sampleCycles == 0) {
      ++rtPublications;
      lastRTValue = 0;
      if (data) memcpy(&lastRTValue, data, bytes < sizeof(lastRTValue) ? bytes : sizeof(lastRTValue));
    }
  }
  void refreshParam(int, uint8_t *, size_t) {}
  void refreshParamRT(int, uint8_t *, size_t) {}
  template<typename Callback> void setExeCmdFunctPtr(Callback, void *) {}
};
class ecmcAsynPortDriver {
 public:
  const char *portName = "SIM_TEST";
  int capacity = 100000;
  ecmcAsynPortDriver *createAuxiliaryPort(const char *name, int size) {
    auto found = children_.find(name);
    if (found == children_.end()) {
      children_[name] = std::unique_ptr<ecmcAsynPortDriver>(new ecmcAsynPortDriver);
      found = children_.find(name);
      found->second->portName = found->first.c_str();
    }
    found->second->capacity = size;
    return found->second.get();
  }
  size_t parameterCount() const { return items_.size(); }
  ecmcAsynPortDriver *auxiliary(const char *name) {
    auto found = children_.find(name); return found == children_.end() ? nullptr : found->second.get();
  }
  ecmcAsynDataItem *addNewAvailParam(const char *name, asynParamType,
                                    uint8_t *data, size_t bytes, int, int) {
    if (items_.size() >= static_cast<size_t>(capacity)) return nullptr;
    items_.emplace_back(new ecmcAsynDataItem);
    items_.back()->setEcmcDataPointer(data, bytes);
    names_[name] = items_.back().get();
    return items_.back().get();
  }
  ecmcAsynDataItem *find(const char *name) {
    auto found = names_.find(name); return found == names_.end() ? nullptr : found->second;
  }
  int addParamAlias(const char *name, const char *alias) {
    auto *target = find(name);
    if (!target || !alias || !*alias) return -1;
    auto *existing = find(alias);
    if (existing && existing != target) return -1;
    names_[alias] = target;
    return 0;
  }
  void callParamCallbacks(int, int) {}
 private:
  std::map<std::string, std::unique_ptr<ecmcAsynPortDriver>> children_;
  std::vector<std::unique_ptr<ecmcAsynDataItem>> items_;
  std::map<std::string, ecmcAsynDataItem *> names_;
};
