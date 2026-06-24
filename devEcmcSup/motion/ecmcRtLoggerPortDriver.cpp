/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcRtLoggerPortDriver.cpp
*
*  Created on: Apr 11, 2026
*
\*************************************************************************/

#include "ecmcRtLoggerPortDriver.h"

#include <atomic>
#include <limits.h>
#include <stdint.h>
#include <stdexcept>

#include "asynPortDriver.h"
#include "ecmcDefinitions.h"
#include "ecmcOctetIF.h"
#include "ecmcMotionDiag.h"
#include "ecmcRtLogger.h"

namespace {

const char *ECMC_RT_LOGGER_PORT_NAME = "ecmcRTLog";

const char *ECMC_RT_LOGGER_PAR_LAST_MESSAGE = "RTLOG_LAST_MESSAGE";
const char *ECMC_RT_LOGGER_PAR_LAST_LEVEL = "RTLOG_LAST_LEVEL";
const char *ECMC_RT_LOGGER_PAR_LAST_LEVEL_TEXT = "RTLOG_LAST_LEVEL_TEXT";
const char *ECMC_RT_LOGGER_PAR_MESSAGE_COUNT = "RTLOG_MESSAGE_COUNT";
const char *ECMC_RT_LOGGER_PAR_DROPPED_COUNT = "RTLOG_DROPPED_COUNT";
const char *ECMC_RT_LOGGER_PAR_CONTROL = "RTLOG_CONTROL";
const char *ECMC_RT_LOGGER_PAR_LAST_SOURCE_TYPE = "RTLOG_LAST_SOURCE_TYPE";
const char *ECMC_RT_LOGGER_PAR_LAST_SOURCE_INDEX = "RTLOG_LAST_SOURCE_INDEX";
const char *ECMC_RT_LOGGER_PAR_FILTER_MODE = "RTLOG_FILTER_MODE";
const char *ECMC_RT_LOGGER_PAR_FILTER_TYPE_MASK = "RTLOG_FILTER_TYPE_MASK";
const char *ECMC_RT_LOGGER_PAR_FILTER_INDEX = "RTLOG_FILTER_INDEX";
const char *ECMC_RT_LOGGER_PAR_DIAG_DUMP = "RTLOG_DIAG_DUMP";
const char *ECMC_RT_LOGGER_PAR_DIAG_LEVEL = "RTLOG_DIAG_LEVEL";
const char *ECMC_RT_LOGGER_PAR_DIAG_BUSY = "RTLOG_DIAG_BUSY";
const char *ECMC_RT_LOGGER_PAR_DIAG_STATUS = "RTLOG_DIAG_STATUS";
const char *ECMC_RT_LOGGER_PAR_DIAG_FILE = "RTLOG_DIAG_FILE";
const char *ECMC_RT_LOGGER_PAR_DIAG_AXIS = "RTLOG_DIAG_AXIS";
const char *ECMC_RT_LOGGER_PAR_DIAG_AXIS_REPORT = "RTLOG_DIAG_AXIS_REPORT";
const char *ECMC_RT_LOGGER_PAR_AXIS_CMD_MR_REQUEST_COUNT = "RTLOG_AXIS_CMD_MR_REQUEST_COUNT";
const char *ECMC_RT_LOGGER_PAR_AXIS_CMD_REQUEST_COUNT = "RTLOG_AXIS_CMD_REQUEST_COUNT";
const char *ECMC_RT_LOGGER_PAR_AXIS_CMD_EXECUTE_COUNT = "RTLOG_AXIS_CMD_EXECUTE_COUNT";

constexpr size_t ECMC_RT_LOGGER_DIAG_FILE_SIZE = 512;
constexpr size_t ECMC_RT_LOGGER_DIAG_AXIS_REPORT_SIZE = 1024;

std::atomic<unsigned int> axisCmdRequestCounts_[ECMC_MAX_AXES];
std::atomic<unsigned int> axisCmdExecuteCounts_[ECMC_MAX_AXES];
std::atomic<unsigned int> axisCmdMotorRecordRequestCounts_[ECMC_MAX_AXES];

enum ecmcRtLoggerPortLevel {
  ECMC_RT_LOGGER_PORT_LEVEL_INFO = 0,
  ECMC_RT_LOGGER_PORT_LEVEL_WARNING = 1,
  ECMC_RT_LOGGER_PORT_LEVEL_ERROR = 2,
  ECMC_RT_LOGGER_PORT_LEVEL_DEBUG = 3
};

class ecmcRtLoggerPortDriver : public asynPortDriver {
public:
  explicit ecmcRtLoggerPortDriver(const char *portName)
    : asynPortDriver(portName,
                     ECMC_MAX_AXES,
                     asynInt32Mask | asynOctetMask | asynDrvUserMask,
                     asynInt32Mask | asynOctetMask,
                     0,
                     1,
                     0,
                     0),
      lastMessageParam_(0),
      lastLevelParam_(0),
      lastLevelTextParam_(0),
      messageCountParam_(0),
      droppedCountParam_(0),
      controlParam_(0),
      lastSourceTypeParam_(0),
      lastSourceIndexParam_(0),
      filterModeParam_(0),
      filterTypeMaskParam_(0),
      filterIndexParam_(0),
      diagDumpParam_(0),
      diagLevelParam_(0),
      diagBusyParam_(0),
      diagStatusParam_(0),
      diagFileParam_(0),
      diagAxisParam_(0),
      diagAxisReportParam_(0),
      axisCmdMotorRecordRequestCountParam_(0),
      axisCmdRequestCountParam_(0),
      axisCmdExecuteCountParam_(0),
      messageCount_(0),
      droppedCount_(0),
      diagLevel_(1),
      diagBusy_(0),
      diagStatus_(0),
      diagAxis_(0),
      diagDumpPending_(0),
      filterMode_(ECMC_RT_LOG_FILTER_NONE),
      filterTypeMask_(INT_MAX),
      filterIndex_(-1) {
    createRequiredParam(ECMC_RT_LOGGER_PAR_LAST_MESSAGE,
                        asynParamOctet,
                        &lastMessageParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_LAST_LEVEL,
                        asynParamInt32,
                        &lastLevelParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_LAST_LEVEL_TEXT,
                        asynParamOctet,
                        &lastLevelTextParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_MESSAGE_COUNT,
                        asynParamInt32,
                        &messageCountParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_DROPPED_COUNT,
                        asynParamInt32,
                        &droppedCountParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_CONTROL,
                        asynParamInt32,
                        &controlParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_LAST_SOURCE_TYPE,
                        asynParamInt32,
                        &lastSourceTypeParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_LAST_SOURCE_INDEX,
                        asynParamInt32,
                        &lastSourceIndexParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_FILTER_MODE,
                        asynParamInt32,
                        &filterModeParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_FILTER_TYPE_MASK,
                        asynParamInt32,
                        &filterTypeMaskParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_FILTER_INDEX,
                        asynParamInt32,
                        &filterIndexParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_DIAG_DUMP,
                        asynParamInt32,
                        &diagDumpParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_DIAG_LEVEL,
                        asynParamInt32,
                        &diagLevelParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_DIAG_BUSY,
                        asynParamInt32,
                        &diagBusyParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_DIAG_STATUS,
                        asynParamInt32,
                        &diagStatusParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_DIAG_FILE,
                        asynParamOctet,
                        &diagFileParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_DIAG_AXIS,
                        asynParamInt32,
                        &diagAxisParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_DIAG_AXIS_REPORT,
                        asynParamOctet,
                        &diagAxisReportParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_AXIS_CMD_MR_REQUEST_COUNT,
                        asynParamInt32,
                        &axisCmdMotorRecordRequestCountParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_AXIS_CMD_REQUEST_COUNT,
                        asynParamInt32,
                        &axisCmdRequestCountParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_AXIS_CMD_EXECUTE_COUNT,
                        asynParamInt32,
                        &axisCmdExecuteCountParam_);

    setStringParam(lastMessageParam_, "");
    setIntegerParam(lastLevelParam_, ECMC_RT_LOGGER_PORT_LEVEL_INFO);
    setStringParam(lastLevelTextParam_, "INFO");
    setIntegerParam(messageCountParam_, 0);
    setIntegerParam(droppedCountParam_, 0);
    setIntegerParam(controlParam_, (int)ecmcRtLoggerGetControlWord());
    setIntegerParam(lastSourceTypeParam_, ECMC_RT_LOG_SOURCE_UNKNOWN);
    setIntegerParam(lastSourceIndexParam_, -1);
    setIntegerParam(filterModeParam_, ECMC_RT_LOG_FILTER_NONE);
    setIntegerParam(filterTypeMaskParam_, INT_MAX);
    setIntegerParam(filterIndexParam_, -1);
    diagFile_[0] = '\0';
    ecmcMotionDiagBuildAxisReport(diagAxis_, diagAxisReport_, sizeof(diagAxisReport_));
    setIntegerParam(diagDumpParam_, 0);
    setIntegerParam(diagLevelParam_, diagLevel_);
    setIntegerParam(diagBusyParam_, diagBusy_);
    setIntegerParam(diagStatusParam_, diagStatus_);
    setStringParam(diagFileParam_, diagFile_);
    setIntegerParam(diagAxisParam_, diagAxis_);
    setStringParam(diagAxisReportParam_, diagAxisReport_);
    for (int axisIndex = 0; axisIndex < ECMC_MAX_AXES; ++axisIndex) {
      axisCmdMotorRecordRequestCountsPublished_[axisIndex] = UINT_MAX;
      axisCmdRequestCountsPublished_[axisIndex] = UINT_MAX;
      axisCmdExecuteCountsPublished_[axisIndex] = UINT_MAX;
      setIntegerParam(axisIndex,
                      axisCmdMotorRecordRequestCountParam_,
                      saturateCount(axisCmdMotorRecordRequestCounts_[axisIndex].load(std::memory_order_acquire)));
      setIntegerParam(axisIndex,
                      axisCmdRequestCountParam_,
                      saturateCount(axisCmdRequestCounts_[axisIndex].load(std::memory_order_acquire)));
      setIntegerParam(axisIndex,
                      axisCmdExecuteCountParam_,
                      saturateCount(axisCmdExecuteCounts_[axisIndex].load(std::memory_order_acquire)));
    }
    callParamCallbacks();
  }

  asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value) override {
    if (!pasynUser) {
      return asynError;
    }

    if (pasynUser->reason == controlParam_) {
      ecmcRtLoggerSetControlWord((unsigned int)value);
      setIntegerParam(controlParam_, value);
      callParamCallbacks();
      return asynSuccess;
    }
    if (pasynUser->reason == filterModeParam_) {
      filterMode_.store((int)value, std::memory_order_release);
      setIntegerParam(filterModeParam_, value);
      callParamCallbacks();
      return asynSuccess;
    }
    if (pasynUser->reason == filterTypeMaskParam_) {
      filterTypeMask_.store((unsigned int)value, std::memory_order_release);
      setIntegerParam(filterTypeMaskParam_, value);
      callParamCallbacks();
      return asynSuccess;
    }
    if (pasynUser->reason == filterIndexParam_) {
      filterIndex_.store((int)value, std::memory_order_release);
      setIntegerParam(filterIndexParam_, value);
      callParamCallbacks();
      return asynSuccess;
    }
    if (pasynUser->reason == diagLevelParam_) {
      diagLevel_ = value;
      setIntegerParam(diagLevelParam_, value);
      callParamCallbacks();
      return asynSuccess;
    }
    if (pasynUser->reason == diagAxisParam_) {
      diagAxis_ = value;
      ecmcMotionDiagBuildAxisReport(diagAxis_,
                                    diagAxisReport_,
                                    sizeof(diagAxisReport_));
      setIntegerParam(diagAxisParam_, value);
      setStringParam(diagAxisReportParam_, diagAxisReport_);
      callParamCallbacks();
      return asynSuccess;
    }
    if (pasynUser->reason == diagDumpParam_) {
      if (!value) {
        setIntegerParam(diagDumpParam_, 0);
        callParamCallbacks();
        return asynSuccess;
      }
      if (diagBusy_) {
        diagStatus_ = 2;
        setIntegerParam(diagStatusParam_, diagStatus_);
        callParamCallbacks();
        return asynSuccess;
      }
      ecmcMotionDiagMakeDumpFileName(diagFile_, sizeof(diagFile_));
      diagBusy_ = 1;
      diagStatus_ = 1;
      setIntegerParam(diagDumpParam_, 1);
      setIntegerParam(diagBusyParam_, diagBusy_);
      setIntegerParam(diagStatusParam_, diagStatus_);
      setStringParam(diagFileParam_, diagFile_);
      diagDumpPending_.store(1, std::memory_order_release);
      callParamCallbacks();
      return asynSuccess;
    }

    return asynPortDriver::writeInt32(pasynUser, value);
  }

  void publishMessage(int level,
                      int sourceType,
                      int sourceIndex,
                      const char *message) {
    if (!filterAllows(sourceType, sourceIndex)) {
      return;
    }

    ++messageCount_;
    setStringParam(lastMessageParam_, message ? message : "");
    setIntegerParam(lastLevelParam_, level);
    setStringParam(lastLevelTextParam_,
                   level == ECMC_RT_LOGGER_PORT_LEVEL_ERROR ? "ERROR" :
                   (level == ECMC_RT_LOGGER_PORT_LEVEL_WARNING ? "WARNING" :
                    (level == ECMC_RT_LOGGER_PORT_LEVEL_DEBUG ? "DEBUG" : "INFO")));
    setIntegerParam(messageCountParam_, saturateCount(messageCount_));
    setIntegerParam(lastSourceTypeParam_, sourceType);
    setIntegerParam(lastSourceIndexParam_, sourceIndex);
    callParamCallbacks();
  }

  void publishDropped(unsigned int dropped) {
    if (!dropped) {
      return;
    }

    droppedCount_ += dropped;
    setIntegerParam(droppedCountParam_, saturateCount(droppedCount_));
    callParamCallbacks();
  }

  void service() {
    publishAxisCommandCounters();

    if (!diagDumpPending_.exchange(0, std::memory_order_acq_rel)) {
      return;
    }

    const int status = ecmcMotionDiagWriteDumpFile(diagFile_, diagLevel_);
    diagStatus_ = status;
    diagBusy_ = 0;
    setIntegerParam(diagDumpParam_, 0);
    setIntegerParam(diagBusyParam_, diagBusy_);
    setIntegerParam(diagStatusParam_, diagStatus_);
    setStringParam(diagFileParam_, diagFile_);
    ecmcMotionDiagBuildAxisReport(diagAxis_,
                                  diagAxisReport_,
                                  sizeof(diagAxisReport_));
    setStringParam(diagAxisReportParam_, diagAxisReport_);
    callParamCallbacks();
  }

private:
  void publishAxisCommandCounters() {
    for (int axisIndex = 0; axisIndex < ECMC_MAX_AXES; ++axisIndex) {
      bool changed = false;
      const unsigned int motorRecordRequestCounter =
        axisCmdMotorRecordRequestCounts_[axisIndex].load(std::memory_order_acquire);
      if (motorRecordRequestCounter != axisCmdMotorRecordRequestCountsPublished_[axisIndex]) {
        axisCmdMotorRecordRequestCountsPublished_[axisIndex] = motorRecordRequestCounter;
        setIntegerParam(axisIndex,
                        axisCmdMotorRecordRequestCountParam_,
                        saturateCount(motorRecordRequestCounter));
        changed = true;
      }

      const unsigned int requestCounter =
        axisCmdRequestCounts_[axisIndex].load(std::memory_order_acquire);
      if (requestCounter != axisCmdRequestCountsPublished_[axisIndex]) {
        axisCmdRequestCountsPublished_[axisIndex] = requestCounter;
        setIntegerParam(axisIndex,
                        axisCmdRequestCountParam_,
                        saturateCount(requestCounter));
        changed = true;
      }

      const unsigned int executeCounter =
        axisCmdExecuteCounts_[axisIndex].load(std::memory_order_acquire);
      if (executeCounter != axisCmdExecuteCountsPublished_[axisIndex]) {
        axisCmdExecuteCountsPublished_[axisIndex] = executeCounter;
        setIntegerParam(axisIndex,
                        axisCmdExecuteCountParam_,
                        saturateCount(executeCounter));
        changed = true;
      }

      if (changed) {
        callParamCallbacks(0, axisIndex);
      }
    }
  }

  bool filterAllows(int sourceType,
                    int sourceIndex) const {
    const int filterMode = filterMode_.load(std::memory_order_acquire);

    if (filterMode == ECMC_RT_LOG_FILTER_NONE) {
      return false;
    }

    if (filterMode != ECMC_RT_LOG_FILTER_SELECTED) {
      return true;
    }

    const unsigned int filterTypeMask =
      filterTypeMask_.load(std::memory_order_acquire);
    const unsigned int eventTypeMask = sourceTypeMask(sourceType);
    if (!eventTypeMask) {
      return false;
    }
    if ((filterTypeMask & eventTypeMask) == 0) {
      return false;
    }

    const int filterIndex = filterIndex_.load(std::memory_order_acquire);
    if (filterIndex >= 0 && filterIndex != sourceIndex) {
      return false;
    }

    return true;
  }

  unsigned int sourceTypeMask(int sourceType) const {
    switch (sourceType) {
    case ECMC_RT_LOG_SOURCE_UNKNOWN:
      return ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_UNKNOWN);
    case ECMC_RT_LOG_SOURCE_AXIS:
      return ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_AXIS);
    case ECMC_RT_LOG_SOURCE_AXIS_BASE:
      return ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_AXIS_BASE) |
             ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_AXIS);
    case ECMC_RT_LOG_SOURCE_AXIS_SEQ:
      return ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_AXIS_SEQ) |
             ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_AXIS);
    case ECMC_RT_LOG_SOURCE_AXIS_DATA:
      return ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_AXIS_DATA) |
             ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_AXIS);
    case ECMC_RT_LOG_SOURCE_AXIS_MON:
      return ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_AXIS_MON) |
             ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_AXIS);
    case ECMC_RT_LOG_SOURCE_AXIS_ENC:
      return ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_AXIS_ENC) |
             ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_AXIS);
    case ECMC_RT_LOG_SOURCE_AXIS_DRV:
      return ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_AXIS_DRV) |
             ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_AXIS);
    case ECMC_RT_LOG_SOURCE_AXIS_PID:
      return ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_AXIS_PID) |
             ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_AXIS);
    case ECMC_RT_LOG_SOURCE_AXIS_TRAJ:
      return ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_AXIS_TRAJ) |
             ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_AXIS);
    case ECMC_RT_LOG_SOURCE_AXIS_PVT:
      return ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_AXIS_PVT) |
             ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_AXIS);
    case ECMC_RT_LOG_SOURCE_MOTOR:
      return ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_MOTOR);
    case ECMC_RT_LOG_SOURCE_MOTOR_AXIS:
      return ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_MOTOR_AXIS) |
             ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_MOTOR);
    case ECMC_RT_LOG_SOURCE_MOTOR_CTRL:
      return ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_MOTOR_CTRL) |
             ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_MOTOR);
    case ECMC_RT_LOG_SOURCE_MASTER_SLAVE:
      return ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_MASTER_SLAVE);
    case ECMC_RT_LOG_SOURCE_ETHERCAT:
      return ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_ETHERCAT);
    case ECMC_RT_LOG_SOURCE_PLC:
      return ECMC_RT_LOG_TYPE_BIT(ECMC_RT_LOG_SOURCE_PLC);
    default:
      return 0;
    }
  }

  void createRequiredParam(const char *name,
                           asynParamType type,
                           int *paramId) {
    asynStatus status = createParam(name, type, paramId);
    if (status != asynSuccess) {
      throw std::runtime_error(name);
    }
  }

  int saturateCount(uint64_t value) const {
    return value > (uint64_t)INT_MAX ? INT_MAX : (int)value;
  }

  int lastMessageParam_;
  int lastLevelParam_;
  int lastLevelTextParam_;
  int messageCountParam_;
  int droppedCountParam_;
  int controlParam_;
  int lastSourceTypeParam_;
  int lastSourceIndexParam_;
  int filterModeParam_;
  int filterTypeMaskParam_;
  int filterIndexParam_;
  int diagDumpParam_;
  int diagLevelParam_;
  int diagBusyParam_;
  int diagStatusParam_;
  int diagFileParam_;
  int diagAxisParam_;
  int diagAxisReportParam_;
  int axisCmdMotorRecordRequestCountParam_;
  int axisCmdRequestCountParam_;
  int axisCmdExecuteCountParam_;
  uint64_t messageCount_;
  uint64_t droppedCount_;
  int diagLevel_;
  int diagBusy_;
  int diagStatus_;
  int diagAxis_;
  char diagFile_[ECMC_RT_LOGGER_DIAG_FILE_SIZE];
  char diagAxisReport_[ECMC_RT_LOGGER_DIAG_AXIS_REPORT_SIZE];
  unsigned int axisCmdMotorRecordRequestCountsPublished_[ECMC_MAX_AXES];
  unsigned int axisCmdRequestCountsPublished_[ECMC_MAX_AXES];
  unsigned int axisCmdExecuteCountsPublished_[ECMC_MAX_AXES];
  std::atomic<int> diagDumpPending_;
  std::atomic<int> filterMode_;
  std::atomic<unsigned int> filterTypeMask_;
  std::atomic<int> filterIndex_;
};

ecmcRtLoggerPortDriver *portDriver_ = NULL;

}  // namespace

int ecmcRtLoggerPortDriverStart() {
  if (portDriver_) {
    return 0;
  }

  try {
    portDriver_ = new ecmcRtLoggerPortDriver(ECMC_RT_LOGGER_PORT_NAME);
  } catch (const std::exception &ex) {
    LOGERR("%s/%s:%d: ERROR: Failed to create RT logger asyn port driver: %s.\n",
           __FILE__,
           __FUNCTION__,
           __LINE__,
           ex.what());
    portDriver_ = NULL;
    return -1;
  } catch (...) {
    LOGERR("%s/%s:%d: ERROR: Failed to create RT logger asyn port driver.\n",
           __FILE__,
           __FUNCTION__,
           __LINE__);
    portDriver_ = NULL;
    return -1;
  }

  return 0;
}

void ecmcRtLoggerPortDriverPublishMessage(int level,
                                          int sourceType,
                                          int sourceIndex,
                                          const char *message) {
  if (!portDriver_) {
    return;
  }

  portDriver_->publishMessage(level,
                              sourceType,
                              sourceIndex,
                              message);
}

void ecmcRtLoggerPortDriverPublishDropped(unsigned int dropped) {
  if (!portDriver_) {
    return;
  }

  portDriver_->publishDropped(dropped);
}

void ecmcRtLoggerPortDriverSetAxisCommandCounters(int axisIndex,
                                                  unsigned int motorRecordRequestCounter,
                                                  unsigned int requestCounter,
                                                  unsigned int executeCounter) {
  if (axisIndex < 0 || axisIndex >= ECMC_MAX_AXES) {
    return;
  }

  axisCmdMotorRecordRequestCounts_[axisIndex].store(motorRecordRequestCounter, std::memory_order_release);
  axisCmdRequestCounts_[axisIndex].store(requestCounter, std::memory_order_release);
  axisCmdExecuteCounts_[axisIndex].store(executeCounter, std::memory_order_release);
}

void ecmcRtLoggerPortDriverService() {
  if (!portDriver_) {
    return;
  }

  portDriver_->service();
}

const char *ecmcRtLoggerPortDriverGetPortName() {
  return ECMC_RT_LOGGER_PORT_NAME;
}
