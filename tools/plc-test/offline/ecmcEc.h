// Explicit in-memory backend for testing production ec_* PLC wrappers.
// This is not an EtherCAT master or a simulation of transport/device behavior.
#pragma once
#include "ecmcEcTiming.h"
#include "ecmcDefinitions.h"
#include "ecmcErrorsList.h"
#include <map>
#include <memory>
#include <vector>
#include <stdexcept>

class ecmcEcMemMap {
 public:
  explicit ecmcEcMemMap(size_t size) : data_(size, 0) {}
  int getByteSize() const { return data_.size() * sizeof(double); }
  size_t getElementCount() const { return data_.size(); }
  int getDataType() const { return ECMC_EC_F64; }
  uint8_t *getBufferPointer() { return reinterpret_cast<uint8_t *>(data_.data()); }
  int getDoubleDataAtIndex(size_t index, double *value) const {
    if (index >= data_.size()) return 0x211002; // ERROR_MEM_INDEX_OUT_OF_RANGE
    *value = data_[index];
    return 0;
  }
  int setDoubleDataAtIndex(size_t index, double value) {
    if (index >= data_.size()) return 0x211002;
    data_[index] = value;
    return 0;
  }
  int updateAsyn(bool) { throw std::logic_error("asyn publication unavailable offline"); }
 private:
  std::vector<double> data_;
};
class ecmcEcSlave {
 public:
  ecmcEcEndpointTiming timing;
  ecmcEcDcConfig dc;
  ecmcEcDcSchedule schedule;
  const ecmcEcEndpointTiming &getInputTiming() const { return timing; }
  const ecmcEcDcConfig &getDcConfig() const { return dc; }
  const ecmcEcDcSchedule &getDcSchedule() const { return schedule; }
};
class ecmcEc {
 public:
  std::map<int, std::unique_ptr<ecmcEcMemMap>> maps;
  std::map<int, ecmcEcSlave> slaves;
  std::map<int, int> domains;
  ecmcEcCycleTiming cycle;
  uint64_t sendTime = 0;
  int64_t offset = 0;
  uint64_t getTimeNs() const { return cycle.applicationTimeNs; }
  uint64_t getTimeNs(int) const { throw std::logic_error("host clock reads unavailable offline"); }
  uint64_t getLastReceiveTimeNs() const { return cycle.receiveTimeNs; }
  uint64_t getLastSendTimeNs() const { return sendTime; }
  int64_t getTimeOffsetNs() const { return offset; }
  const ecmcEcCycleTiming &getCycleTiming() const { return cycle; }
  ecmcEcSlave *findSlave(int id) {
    auto it = slaves.find(id);
    return it == slaves.end() ? nullptr : &it->second;
  }
  ecmcEcMemMap *getMemMap(int id) {
    auto it = maps.find(id);
    return it == maps.end() ? nullptr : it->second.get();
  }
  int getDomState(int id) const {
    auto it = domains.find(id);
    return it == domains.end() ? -ERROR_EC_MAIN_DOMAIN_NULL : it->second;
  }
};
