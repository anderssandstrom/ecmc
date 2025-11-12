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

#ifndef LOGERR
#define LOGERR(...) std::printf(__VA_ARGS__)
#endif

enum asynStatus {
  asynSuccess = 0,
  asynError   = -1,
};

enum asynParamType {
  asynParamInt32         = 0,
  asynParamFloat64       = 1,
  asynParamUInt32Digital = 2,
};

class ecmcError {
public:
  ecmcError() : errorId_(0) {}
  virtual ~ecmcError() = default;
  virtual int setErrorID(int errorID) {
    errorId_ = errorID;
    return errorID;
  }
  virtual int setErrorID(const char*,
                         const char*,
                         int,
                         int errorID) {
    return setErrorID(errorID);
  }
  virtual void errorReset() {
    errorId_ = 0;
  }
  virtual bool getError() const {
    return errorId_ != 0;
  }
  virtual int getErrorID() const {
    return errorId_;
  }
  virtual int setWarningID(int) {
    return 0;
  }

private:
  int errorId_;
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
