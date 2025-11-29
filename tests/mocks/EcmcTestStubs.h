/*************************************************************************\
* Simplified stand-ins for ecmc dependencies used in unit tests.
\*************************************************************************/

#ifndef ECMC_TEST_STUBS_H_
#define ECMC_TEST_STUBS_H_

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "../../devEcmcSup/main/ecmcDefinitions.h"
#include "../../devEcmcSup/main/ecmcError.h"

#ifndef ECMC_TEST_ASYN_TYPES_DEFINED
enum asynStatus {
  asynSuccess = 0,
  asynError   = -1,
};
#define ECMC_TEST_ASYN_TYPES_DEFINED
#endif

#ifndef ASYN_PARAM_TYPE_DEFINED
#define ASYN_PARAM_TYPE_DEFINED
enum asynParamType {
  asynParamInt32         = 0,
  asynParamFloat64       = 1,
  asynParamUInt32Digital = 2,
};
#endif

#ifndef ECMC_EC_ENTRY_LINKS_MAX
#define ECMC_EC_ENTRY_LINKS_MAX 20
#endif

class ecmcEcEntry : public ecmcError {
public:
  ecmcEcEntry() : value_(0) {}
  int readValue(uint64_t *value) {
    if (!value) {
      return -1;
    }
    *value = value_;
    return 0;
  }
  int writeValue(uint64_t value) {
    value_ = value;
    return 0;
  }
  int readBits(int startBit, int bits, uint64_t *result) {
    if (!result || bits <= 0 || startBit < 0 || startBit + bits > 64) {
      return -1;
    }
    uint64_t mask = bits == 64 ? UINT64_MAX : ((1ULL << bits) - 1ULL);
    *result = (value_ >> startBit) & mask;
    return 0;
  }
  int writeBits(int startBit, int bits, uint64_t value) {
    if (bits <= 0 || startBit < 0 || startBit + bits > 64) {
      return -1;
    }
    uint64_t mask = bits == 64 ? UINT64_MAX : ((1ULL << bits) - 1ULL);
    value_ &= ~(mask << startBit);
    value_ |= (value & mask) << startBit;
    return 0;
  }

private:
  uint64_t value_;
};

class ecmcEcEntryLink : public ecmcError {
public:
  ecmcEcEntryLink() = default;
  ecmcEcEntryLink(int*, int*) {}

  int setEntryAtIndex(ecmcEcEntry *entry, int index, int) {
    if (index < 0 || index >= ECMC_EC_ENTRY_LINKS_MAX) {
      return -1;
    }
    entries_[index] = entry;
    return 0;
  }

  int validateEntry(int index) {
    if (index < 0 || index >= ECMC_EC_ENTRY_LINKS_MAX) {
      return -1;
    }
    return entries_[index] ? 0 : -1;
  }

  int readEcEntryValue(int index, uint64_t *value) {
    if (validateEntry(index)) {
      return -1;
    }
    return entries_[index]->readValue(value);
  }

  int readEcEntryValueDouble(int index, double *value) {
    uint64_t temp = 0;
    int ret       = readEcEntryValue(index, &temp);
    if (ret == 0 && value) {
      *value = static_cast<double>(temp);
    }
    return ret;
  }

  int writeEcEntryValue(int index, uint64_t value) {
    if (validateEntry(index)) {
      return -1;
    }
    return entries_[index]->writeValue(value);
  }

  int readEcEntryBits(int index, int bits, uint64_t *value) {
    if (validateEntry(index)) {
      return -1;
    }
    return entries_[index]->readBits(0, bits, value);
  }

  int writeEcEntryBits(int index, int bits, uint64_t value) {
    if (validateEntry(index)) {
      return -1;
    }
    return entries_[index]->writeBits(0, bits, value);
  }

  bool checkEntryExist(int index) {
    return index >= 0 && index < ECMC_EC_ENTRY_LINKS_MAX && entries_[index];
  }

  bool checkDomainOK(int) {
    return true;
  }

  bool checkDomainOKAllEntries() {
    return true;
  }

private:
  ecmcEcEntry *entries_[ECMC_EC_ENTRY_LINKS_MAX]{};
};

class ecmcMonitor {
public:
  void setAxisIsWithinCtrlDBExtTraj(bool within) {
    axisWithinCtrlDBExt_ = within;
  }
  bool getAxisIsWithinCtrlDB() const {
    return axisWithinCtrlDB_;
  }
  void setAxisIsWithinCtrlDB(bool within) {
    axisWithinCtrlDB_ = within;
  }

private:
  bool axisWithinCtrlDBExt_{false};
  bool axisWithinCtrlDB_{true};
};

class ecmcAxisBase;

class ecmcAxisSequencer {
public:
  explicit ecmcAxisSequencer(ecmcAxisBase *axis = nullptr)
    : axis_(axis) {}

  void attachAxis(ecmcAxisBase *axis) {
    axis_ = axis;
  }

  int setTrajDataSourceType(dataSource src);

  void setNextError(int error) {
    nextError_ = error;
  }

private:
  ecmcAxisBase *axis_;
  int nextError_{0};
};

class ecmcAxisBase : public ecmcError {
public:
  explicit ecmcAxisBase(int axisId = 0)
    : axisId_(axisId),
      seq_(this) {}

  int getAxisID() const {
    return axisId_;
  }

  int setEnable(bool enable) {
    if (nextEnableError_ != 0) {
      int err       = nextEnableError_;
      nextEnableError_ = 0;
      return err;
    }
    enableCmd_ = enable;
    enabled_   = enable && !forceDisable_;
    return 0;
  }

  bool getEnable() const {
    return enableCmd_;
  }

  bool getEnabled() const {
    return enabled_;
  }

  bool getEnabledOnly() const {
    return enabled_;
  }

  bool getBusy() const {
    return busy_;
  }

  bool getTrajBusy() const {
    return trajBusy_;
  }

  void setBusy(bool busy) {
    busy_ = busy;
  }

  void setTrajBusy(bool busy) {
    trajBusy_ = busy;
  }

  void setTrajSource(dataSource src) {
    trajSource_ = src;
  }

  dataSource getTrajDataSourceType() const {
    return trajSource_;
  }

  int setEncDataSourceType(dataSource) {
    return 0;
  }

  ecmcAxisSequencer* getSeq() {
    seq_.attachAxis(this);
    return &seq_;
  }

  ecmcMonitor* getMon() {
    return &mon_;
  }

  void setAllowSourceChangeWhenEnabled(int) {}
  void setMRSync(bool) {}
  void setMRStop(bool) {}
  void setMRCnen(bool) {}
  void setMRIgnoreDisableStatusCheck(bool) {}
  void setEnableAutoEnable(bool) {}
  void setEnableAutoDisable(bool) {}
  void stopMotion(int) {
    busy_     = false;
    trajBusy_ = false;
  }

  bool getLimitFwd() const {
    return limitFwd_;
  }

  bool getLimitBwd() const {
    return limitBwd_;
  }

  void setLimitFwd(bool state) {
    limitFwd_ = state;
  }

  void setLimitBwd(bool state) {
    limitBwd_ = state;
  }

  void setSlavedAxisInError() {
    slavedAxisError_ = true;
  }

  void setSlavedAxisInterlock() {
    slavedAxisInterlock_ = true;
  }

  void setAxisIsWithinCtrlDBExtTraj(bool within) {
    mon_.setAxisIsWithinCtrlDBExtTraj(within);
  }

  bool getAxisIsWithinCtrlDBExtTraj() const {
    return mon_.getAxisIsWithinCtrlDB();
  }

  int getSumInterlock() const {
    return sumInterlock_;
  }

  void setSumInterlock(int value) {
    sumInterlock_ = value;
  }

  void setNextEnableError(int error) {
    nextEnableError_ = error;
  }

  void setNextTrajError(int error) {
    seq_.setNextError(error);
  }

  void forceDisable(bool disable) {
    forceDisable_ = disable;
    if (disable) {
      enabled_ = false;
    }
  }

private:
  int axisId_;
  bool enableCmd_{false};
  bool enabled_{false};
  bool busy_{false};
  bool trajBusy_{false};
  bool forceDisable_{false};
  bool limitFwd_{true};
  bool limitBwd_{true};
  bool slavedAxisError_{false};
  bool slavedAxisInterlock_{false};
  int sumInterlock_{0};
  int nextEnableError_{0};
  dataSource trajSource_{ECMC_DATA_SOURCE_INTERNAL};
  ecmcAxisSequencer seq_;
  ecmcMonitor mon_;
};

inline int ecmcAxisSequencer::setTrajDataSourceType(dataSource src) {
  if (nextError_ != 0) {
    int err = nextError_;
    nextError_ = 0;
    return err;
  }
  if (axis_) {
    axis_->setTrajSource(src);
  }
  return 0;
}

class ecmcAsynDataItem {
public:
  void setAllowWriteToEcmc(bool) {}
  void addSupportedAsynType(asynParamType) {}
  void refreshParam(int) {}
  void refreshParamRT(int) {}
  void setExeCmdFunctPtr(asynStatus (*)(void*, size_t, asynParamType, void*),
                         void *) {}
};

class ecmcAsynPortDriver {
public:
  ecmcAsynPortDriver(const char * = "stub",
                     int         = 0,
                     int         = 0,
                     int         = 0,
                     double      = 0.0) {}

  ecmcAsynDataItem* addNewAvailParam(const char*,
                                     asynParamType,
                                     uint8_t*,
                                     size_t,
                                     ecmcEcDataType,
                                     int) {
    items_.emplace_back();
    return &items_.back();
  }

private:
  std::vector<ecmcAsynDataItem> items_;
};

#endif  /* ECMC_TEST_STUBS_H_ */
