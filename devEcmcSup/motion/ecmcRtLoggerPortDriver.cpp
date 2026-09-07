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

#include <algorithm>
#include <atomic>
#include <stdio.h>
#include <string.h>
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
const char *ECMC_RT_LOGGER_PAR_AXIS_CMD_CLEAR = "RTLOG_AXIS_CMD_CLEAR";
const char *ECMC_RT_LOGGER_PAR_AXIS_CMD_COUNT_MR_STOP = "RTLOG_AXIS_CMD_COUNT_MR_STOP";
const char *ECMC_RT_LOGGER_PAR_AXIS_CMD_COUNT_ENABLE = "RTLOG_AXIS_CMD_COUNT_ENABLE";
const char *ECMC_RT_LOGGER_PAR_AXIS_MR_CMD_TYPE = "RTLOG_AXIS_MR_CMD_TYPE";
const char *ECMC_RT_LOGGER_PAR_AXIS_MR_CMD_RESULT = "RTLOG_AXIS_MR_CMD_RESULT";
const char *ECMC_RT_LOGGER_PAR_AXIS_MR_CMD_REASON = "RTLOG_AXIS_MR_CMD_REASON";
const char *ECMC_RT_LOGGER_PAR_AXIS_MR_CMD_ERROR = "RTLOG_AXIS_MR_CMD_ERROR";
const char *ECMC_RT_LOGGER_PAR_AXIS_MR_CMD_CYCLE = "RTLOG_AXIS_MR_CMD_CYCLE";
const char *ECMC_RT_LOGGER_PAR_AXIS_MR_CMD_TEXT = "RTLOG_AXIS_MR_CMD_TEXT";
const char *ECMC_RT_LOGGER_PAR_AXIS_MS_BLOCKED = "RTLOG_AXIS_MS_BLOCKED";
const char *ECMC_RT_LOGGER_PAR_AXIS_MS_BLOCK_COUNT = "RTLOG_AXIS_MS_BLOCK_COUNT";
const char *ECMC_RT_LOGGER_PAR_AXIS_MS_BLOCK_CYCLE = "RTLOG_AXIS_MS_BLOCK_CYCLE";
const char *ECMC_RT_LOGGER_PAR_AXIS_MS_BLOCK_TEXT = "RTLOG_AXIS_MS_BLOCK_TEXT";

constexpr size_t ECMC_RT_LOGGER_DIAG_FILE_SIZE = 512;
constexpr size_t ECMC_RT_LOGGER_DIAG_AXIS_REPORT_SIZE = 1024;
constexpr size_t ECMC_RT_LOGGER_AXIS_MR_CMD_TEXT_SIZE = 160;
constexpr size_t ECMC_RT_LOGGER_AXIS_MS_BLOCK_TEXT_SIZE = 160;
constexpr int ECMC_EC_TIMING_DIRECTIONS = 2;
constexpr int ECMC_EC_TIMING_FIELDS = 13;

const char *ECMC_EC_TIMING_PARAM_NAMES[2][ECMC_EC_TIMING_FIELDS] = {
  {"timing.input.status", "timing.input.source", "timing.input.reference",
   "timing.input.syncType", "timing.input.cycleOffset",
   "timing.input.updateDivisor", "timing.input.timestampBits",
   "timing.input.cycleTimeNs",
   "timing.input.shiftTimeNs", "timing.input.calculationCopyTimeNs",
   "timing.input.eventOffsetNs", "timing.input.uncertaintyNs",
   "timing.input.timestampCorrectionNs"},
  {"timing.output.status", "timing.output.source", "timing.output.reference",
   "timing.output.syncType", "timing.output.cycleOffset",
   "timing.output.updateDivisor", "timing.output.timestampBits",
   "timing.output.cycleTimeNs",
   "timing.output.shiftTimeNs", "timing.output.calculationCopyTimeNs",
   "timing.output.eventOffsetNs", "timing.output.uncertaintyNs",
   "timing.output.timestampCorrectionNs"}
};

struct atomicEcTimingDiag {
  std::atomic<int64_t> value[ECMC_EC_TIMING_FIELDS];
  std::atomic<unsigned int> version;
};

atomicEcTimingDiag ecTiming_[EC_MAX_SLAVES][ECMC_EC_TIMING_DIRECTIONS];

std::atomic<unsigned int> axisCmdRequestCounts_[ECMC_MAX_AXES];
std::atomic<unsigned int> axisCmdExecuteCounts_[ECMC_MAX_AXES];
std::atomic<unsigned int> axisCmdMotorRecordRequestCounts_[ECMC_MAX_AXES];
std::atomic<int> axisMrCmdTypes_[ECMC_MAX_AXES];
std::atomic<int> axisMrCmdResults_[ECMC_MAX_AXES];
std::atomic<int> axisMrCmdReasons_[ECMC_MAX_AXES];
std::atomic<int> axisMrCmdErrors_[ECMC_MAX_AXES];
std::atomic<int> axisMrCmdCycles_[ECMC_MAX_AXES];
std::atomic<unsigned int> axisMrCmdVersions_[ECMC_MAX_AXES];
std::atomic<int> countMotorRecordStopCommands_[ECMC_MAX_AXES];
std::atomic<int> countEnableCommands_[ECMC_MAX_AXES];
std::atomic<int> axisMsBlockStates_[ECMC_MAX_AXES];
std::atomic<int> axisMsBlockCycles_[ECMC_MAX_AXES];
std::atomic<unsigned int> axisMsBlockCounts_[ECMC_MAX_AXES];
std::atomic<unsigned int> axisMsBlockCountBases_[ECMC_MAX_AXES];
std::atomic<unsigned int> axisMsBlockVersions_[ECMC_MAX_AXES];

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
                     std::max(ECMC_MAX_AXES, EC_MAX_SLAVES),
                     asynInt32Mask | asynInt64Mask | asynOctetMask |
                       asynDrvUserMask,
                     asynInt32Mask | asynInt64Mask | asynOctetMask,
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
      axisCmdClearParam_(0),
      axisCmdCountMotorRecordStopParam_(0),
      axisCmdCountEnableParam_(0),
      axisMrCmdTypeParam_(0),
      axisMrCmdResultParam_(0),
      axisMrCmdReasonParam_(0),
      axisMrCmdErrorParam_(0),
      axisMrCmdCycleParam_(0),
      axisMrCmdTextParam_(0),
      axisMsBlockedParam_(0),
      axisMsBlockCountParam_(0),
      axisMsBlockCycleParam_(0),
      axisMsBlockTextParam_(0),
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
    createRequiredParam(ECMC_RT_LOGGER_PAR_AXIS_CMD_CLEAR,
                        asynParamInt32,
                        &axisCmdClearParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_AXIS_CMD_COUNT_MR_STOP,
                        asynParamInt32,
                        &axisCmdCountMotorRecordStopParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_AXIS_CMD_COUNT_ENABLE,
                        asynParamInt32,
                        &axisCmdCountEnableParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_AXIS_MR_CMD_TYPE,
                        asynParamInt32,
                        &axisMrCmdTypeParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_AXIS_MR_CMD_RESULT,
                        asynParamInt32,
                        &axisMrCmdResultParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_AXIS_MR_CMD_REASON,
                        asynParamInt32,
                        &axisMrCmdReasonParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_AXIS_MR_CMD_ERROR,
                        asynParamInt32,
                        &axisMrCmdErrorParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_AXIS_MR_CMD_CYCLE,
                        asynParamInt32,
                        &axisMrCmdCycleParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_AXIS_MR_CMD_TEXT,
                        asynParamOctet,
                        &axisMrCmdTextParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_AXIS_MS_BLOCKED,
                        asynParamInt32,
                        &axisMsBlockedParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_AXIS_MS_BLOCK_COUNT,
                        asynParamInt32,
                        &axisMsBlockCountParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_AXIS_MS_BLOCK_CYCLE,
                        asynParamInt32,
                        &axisMsBlockCycleParam_);
    createRequiredParam(ECMC_RT_LOGGER_PAR_AXIS_MS_BLOCK_TEXT,
                        asynParamOctet,
                        &axisMsBlockTextParam_);
    for (int direction = 0; direction < ECMC_EC_TIMING_DIRECTIONS;
         ++direction) {
      for (int field = 0; field < ECMC_EC_TIMING_FIELDS; ++field) {
        createRequiredParam(ECMC_EC_TIMING_PARAM_NAMES[direction][field],
                            field < 7 ? asynParamInt32 : asynParamInt64,
                            &ecTimingParam_[direction][field]);
      }
    }

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
      countMotorRecordStopCommands_[axisIndex].store(0, std::memory_order_relaxed);
      countEnableCommands_[axisIndex].store(0, std::memory_order_relaxed);
      axisMsBlockStates_[axisIndex].store(0, std::memory_order_relaxed);
      axisMsBlockCycles_[axisIndex].store(0, std::memory_order_relaxed);
      axisMsBlockCounts_[axisIndex].store(0, std::memory_order_relaxed);
      axisMsBlockCountBases_[axisIndex].store(0, std::memory_order_relaxed);
      axisMsBlockVersions_[axisIndex].store(0, std::memory_order_relaxed);
      axisCmdMotorRecordRequestCountsPublished_[axisIndex] = UINT_MAX;
      axisCmdRequestCountsPublished_[axisIndex] = UINT_MAX;
      axisCmdExecuteCountsPublished_[axisIndex] = UINT_MAX;
      axisCmdMotorRecordRequestCountBase_[axisIndex].store(0, std::memory_order_relaxed);
      axisCmdRequestCountBase_[axisIndex].store(0, std::memory_order_relaxed);
      axisCmdExecuteCountBase_[axisIndex].store(0, std::memory_order_relaxed);
      axisMrCmdTypesPublished_[axisIndex] = INT_MIN;
      axisMrCmdResultsPublished_[axisIndex] = INT_MIN;
      axisMrCmdReasonsPublished_[axisIndex] = INT_MIN;
      axisMrCmdErrorsPublished_[axisIndex] = INT_MIN;
      axisMrCmdCyclesPublished_[axisIndex] = INT_MIN;
      axisMrCmdVersionsPublished_[axisIndex] = UINT_MAX;
      axisMrCmdTextsPublished_[axisIndex][0] = '\0';
      axisMsBlockStatesPublished_[axisIndex] = INT_MIN;
      axisMsBlockCyclesPublished_[axisIndex] = INT_MIN;
      axisMsBlockCountsPublished_[axisIndex] = UINT_MAX;
      axisMsBlockVersionsPublished_[axisIndex] = UINT_MAX;
      axisMsBlockTextsPublished_[axisIndex][0] = '\0';
      setIntegerParam(axisIndex,
                      axisCmdMotorRecordRequestCountParam_,
                      saturateCount(
                        counterDelta(
                          axisCmdMotorRecordRequestCounts_[axisIndex].load(std::memory_order_acquire),
                          axisCmdMotorRecordRequestCountBase_[axisIndex].load(std::memory_order_acquire))));
      setIntegerParam(axisIndex,
                      axisCmdRequestCountParam_,
                      saturateCount(
                        counterDelta(
                          axisCmdRequestCounts_[axisIndex].load(std::memory_order_acquire),
                          axisCmdRequestCountBase_[axisIndex].load(std::memory_order_acquire))));
      setIntegerParam(axisIndex,
                      axisCmdExecuteCountParam_,
                      saturateCount(
                        counterDelta(
                          axisCmdExecuteCounts_[axisIndex].load(std::memory_order_acquire),
                          axisCmdExecuteCountBase_[axisIndex].load(std::memory_order_acquire))));
      setIntegerParam(axisIndex, axisCmdClearParam_, 0);
      setIntegerParam(axisIndex,
                      axisCmdCountMotorRecordStopParam_,
                      countMotorRecordStopCommands_[axisIndex].load(std::memory_order_acquire));
      setIntegerParam(axisIndex,
                      axisCmdCountEnableParam_,
                      countEnableCommands_[axisIndex].load(std::memory_order_acquire));
      setIntegerParam(axisIndex,
                      axisMrCmdTypeParam_,
                      axisMrCmdTypes_[axisIndex].load(std::memory_order_acquire));
      setIntegerParam(axisIndex,
                      axisMrCmdResultParam_,
                      axisMrCmdResults_[axisIndex].load(std::memory_order_acquire));
      setIntegerParam(axisIndex,
                      axisMrCmdReasonParam_,
                      axisMrCmdReasons_[axisIndex].load(std::memory_order_acquire));
      setIntegerParam(axisIndex,
                      axisMrCmdErrorParam_,
                      axisMrCmdErrors_[axisIndex].load(std::memory_order_acquire));
      setIntegerParam(axisIndex,
                      axisMrCmdCycleParam_,
                      axisMrCmdCycles_[axisIndex].load(std::memory_order_acquire));
      setStringParam(axisIndex, axisMrCmdTextParam_, "none");
      setIntegerParam(axisIndex, axisMsBlockedParam_, 0);
      setIntegerParam(axisIndex, axisMsBlockCountParam_, 0);
      setIntegerParam(axisIndex, axisMsBlockCycleParam_, 0);
      setStringParam(axisIndex, axisMsBlockTextParam_, "master_slave none");
    }
    for (int slave = 0; slave < EC_MAX_SLAVES; ++slave) {
      for (int direction = 0; direction < ECMC_EC_TIMING_DIRECTIONS;
           ++direction) {
        ecTimingVersionPublished_[slave][direction] = UINT_MAX;
      }
    }
    callParamCallbacks();
  }

  asynStatus readInt32(asynUser *pasynUser, epicsInt32 *value) override {
    if (!pasynUser || !value) {
      return asynError;
    }

    int addr = 0;
    if (!getAddress(pasynUser, &addr) &&
        addr >= 0 &&
        addr < ECMC_MAX_AXES) {
      if (pasynUser->reason == axisCmdCountMotorRecordStopParam_) {
        *value = countMotorRecordStopCommands_[addr].load(std::memory_order_acquire);
        return asynSuccess;
      }
      if (pasynUser->reason == axisCmdCountEnableParam_) {
        *value = countEnableCommands_[addr].load(std::memory_order_acquire);
        return asynSuccess;
      }
      if (pasynUser->reason == axisCmdMotorRecordRequestCountParam_) {
        *value = saturateCount(
          counterDelta(
            axisCmdMotorRecordRequestCounts_[addr].load(std::memory_order_acquire),
            axisCmdMotorRecordRequestCountBase_[addr].load(std::memory_order_acquire)));
        return asynSuccess;
      }
      if (pasynUser->reason == axisCmdRequestCountParam_) {
        *value = saturateCount(
          counterDelta(
            axisCmdRequestCounts_[addr].load(std::memory_order_acquire),
            axisCmdRequestCountBase_[addr].load(std::memory_order_acquire)));
        return asynSuccess;
      }
      if (pasynUser->reason == axisCmdExecuteCountParam_) {
        *value = saturateCount(
          counterDelta(
            axisCmdExecuteCounts_[addr].load(std::memory_order_acquire),
            axisCmdExecuteCountBase_[addr].load(std::memory_order_acquire)));
        return asynSuccess;
      }
      if (pasynUser->reason == axisMrCmdTypeParam_) {
        *value = axisMrCmdTypes_[addr].load(std::memory_order_acquire);
        return asynSuccess;
      }
      if (pasynUser->reason == axisMrCmdResultParam_) {
        *value = axisMrCmdResults_[addr].load(std::memory_order_acquire);
        return asynSuccess;
      }
      if (pasynUser->reason == axisMrCmdReasonParam_) {
        *value = axisMrCmdReasons_[addr].load(std::memory_order_acquire);
        return asynSuccess;
      }
      if (pasynUser->reason == axisMrCmdErrorParam_) {
        *value = axisMrCmdErrors_[addr].load(std::memory_order_acquire);
        return asynSuccess;
      }
      if (pasynUser->reason == axisMrCmdCycleParam_) {
        *value = axisMrCmdCycles_[addr].load(std::memory_order_acquire);
        return asynSuccess;
      }
      if (pasynUser->reason == axisMsBlockedParam_) {
        *value = axisMsBlockStates_[addr].load(std::memory_order_acquire);
        return asynSuccess;
      }
      if (pasynUser->reason == axisMsBlockCountParam_) {
        *value = saturateCount(
          counterDelta(
            axisMsBlockCounts_[addr].load(std::memory_order_acquire),
            axisMsBlockCountBases_[addr].load(std::memory_order_acquire)));
        return asynSuccess;
      }
      if (pasynUser->reason == axisMsBlockCycleParam_) {
        *value = axisMsBlockCycles_[addr].load(std::memory_order_acquire);
        return asynSuccess;
      }
    }

    return asynPortDriver::readInt32(pasynUser, value);
  }

  asynStatus readOctet(asynUser *pasynUser,
                       char *value,
                       size_t maxChars,
                       size_t *nActual,
                       int *eomReason) override {
    if (!pasynUser || !value || maxChars == 0) {
      return asynError;
    }

    int addr = 0;
    if (!getAddress(pasynUser, &addr) &&
        addr >= 0 &&
        addr < ECMC_MAX_AXES &&
        pasynUser->reason == axisMrCmdTextParam_) {
      char text[ECMC_RT_LOGGER_AXIS_MR_CMD_TEXT_SIZE];
      buildMrCmdText(text,
                     sizeof(text),
                     axisMrCmdTypes_[addr].load(std::memory_order_acquire),
                     axisMrCmdResults_[addr].load(std::memory_order_acquire),
                     axisMrCmdReasons_[addr].load(std::memory_order_acquire),
                     axisMrCmdErrors_[addr].load(std::memory_order_acquire),
                     axisMrCmdCycles_[addr].load(std::memory_order_acquire));
      const int written = snprintf(value, maxChars, "%s", text);
      if (nActual) {
        *nActual = written < 0 ? 0 : std::min((size_t)written, maxChars - 1);
      }
      if (eomReason) {
        *eomReason = ASYN_EOM_END;
      }
      return asynSuccess;
    }

    if (!getAddress(pasynUser, &addr) &&
        addr >= 0 &&
        addr < ECMC_MAX_AXES &&
        pasynUser->reason == axisMsBlockTextParam_) {
      char text[ECMC_RT_LOGGER_AXIS_MS_BLOCK_TEXT_SIZE];
      buildMsBlockText(text,
                       sizeof(text),
                       axisMsBlockStates_[addr].load(std::memory_order_acquire),
                       axisMsBlockCycles_[addr].load(std::memory_order_acquire),
                       counterDelta(
                         axisMsBlockCounts_[addr].load(std::memory_order_acquire),
                         axisMsBlockCountBases_[addr].load(std::memory_order_acquire)));
      const int written = snprintf(value, maxChars, "%s", text);
      if (nActual) {
        *nActual = written < 0 ? 0 : std::min((size_t)written, maxChars - 1);
      }
      if (eomReason) {
        *eomReason = ASYN_EOM_END;
      }
      return asynSuccess;
    }

    return asynPortDriver::readOctet(pasynUser,
                                     value,
                                     maxChars,
                                     nActual,
                                     eomReason);
  }

  asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value) override {
    if (!pasynUser) {
      return asynError;
    }

    int addr = 0;
    const bool validAxisAddr =
      !getAddress(pasynUser, &addr) &&
      addr >= 0 &&
      addr < ECMC_MAX_AXES;

    if (validAxisAddr && pasynUser->reason == axisCmdClearParam_) {
      if (!value) {
        setIntegerParam(addr, axisCmdClearParam_, 0);
        callParamCallbacks(addr);
        return asynSuccess;
      }

      axisCmdMotorRecordRequestCountBase_[addr].store(
        axisCmdMotorRecordRequestCounts_[addr].load(std::memory_order_acquire),
        std::memory_order_release);
      axisCmdRequestCountBase_[addr].store(
        axisCmdRequestCounts_[addr].load(std::memory_order_acquire),
        std::memory_order_release);
      axisCmdExecuteCountBase_[addr].store(
        axisCmdExecuteCounts_[addr].load(std::memory_order_acquire),
        std::memory_order_release);

      unsigned int msBlockVersionStart = 0;
      unsigned int msBlockVersionEnd = 0;
      unsigned int msBlockCount = 0;
      int msBlockState = 0;
      do {
        msBlockVersionStart =
          axisMsBlockVersions_[addr].load(std::memory_order_acquire);
        if (msBlockVersionStart & 1u) {
          continue;
        }
        msBlockState =
          axisMsBlockStates_[addr].load(std::memory_order_relaxed);
        msBlockCount =
          axisMsBlockCounts_[addr].load(std::memory_order_relaxed);
        msBlockVersionEnd =
          axisMsBlockVersions_[addr].load(std::memory_order_acquire);
      } while (msBlockVersionStart != msBlockVersionEnd ||
               (msBlockVersionEnd & 1u));

      axisMsBlockCountBases_[addr].store(msBlockCount,
                                         std::memory_order_release);
      axisMsBlockStatesPublished_[addr] = msBlockState;
      axisMsBlockCyclesPublished_[addr] = 0;
      axisMsBlockCountsPublished_[addr] = 0;
      axisMsBlockVersionsPublished_[addr] = msBlockVersionEnd;
      char msBlockText[ECMC_RT_LOGGER_AXIS_MS_BLOCK_TEXT_SIZE];
      buildMsBlockText(msBlockText,
                       sizeof(msBlockText),
                       msBlockState,
                       0,
                       0);
      snprintf(axisMsBlockTextsPublished_[addr],
               sizeof(axisMsBlockTextsPublished_[addr]),
               "%s",
               msBlockText);
      axisCmdMotorRecordRequestCountsPublished_[addr] = 0;
      axisCmdRequestCountsPublished_[addr] = 0;
      axisCmdExecuteCountsPublished_[addr] = 0;
      axisMrCmdVersions_[addr].fetch_add(1, std::memory_order_acq_rel);
      axisMrCmdTypes_[addr].store(0, std::memory_order_relaxed);
      axisMrCmdResults_[addr].store(0, std::memory_order_relaxed);
      axisMrCmdReasons_[addr].store(0, std::memory_order_relaxed);
      axisMrCmdErrors_[addr].store(0, std::memory_order_relaxed);
      axisMrCmdCycles_[addr].store(0, std::memory_order_relaxed);
      const unsigned int clearVersion =
        axisMrCmdVersions_[addr].fetch_add(1, std::memory_order_release) + 1;
      axisMrCmdTypesPublished_[addr] = 0;
      axisMrCmdResultsPublished_[addr] = 0;
      axisMrCmdReasonsPublished_[addr] = 0;
      axisMrCmdErrorsPublished_[addr] = 0;
      axisMrCmdCyclesPublished_[addr] = 0;
      axisMrCmdVersionsPublished_[addr] = clearVersion;
      char mrCmdText[ECMC_RT_LOGGER_AXIS_MR_CMD_TEXT_SIZE];
      buildMrCmdText(mrCmdText, sizeof(mrCmdText), 0, 0, 0, 0, 0);
      snprintf(axisMrCmdTextsPublished_[addr],
               sizeof(axisMrCmdTextsPublished_[addr]),
               "%s",
               mrCmdText);
      setIntegerParam(addr, axisCmdMotorRecordRequestCountParam_, 0);
      setIntegerParam(addr, axisCmdRequestCountParam_, 0);
      setIntegerParam(addr, axisCmdExecuteCountParam_, 0);
      setIntegerParam(addr, axisMrCmdTypeParam_, 0);
      setIntegerParam(addr, axisMrCmdResultParam_, 0);
      setIntegerParam(addr, axisMrCmdReasonParam_, 0);
      setIntegerParam(addr, axisMrCmdErrorParam_, 0);
      setIntegerParam(addr, axisMrCmdCycleParam_, 0);
      setStringParam(addr, axisMrCmdTextParam_, mrCmdText);
      setIntegerParam(addr, axisMsBlockedParam_, msBlockState);
      setIntegerParam(addr, axisMsBlockCountParam_, 0);
      setIntegerParam(addr, axisMsBlockCycleParam_, 0);
      setStringParam(addr, axisMsBlockTextParam_, msBlockText);
      setIntegerParam(addr, axisCmdClearParam_, 0);
      callParamCallbacks(addr);
      return asynSuccess;
    }

    if (validAxisAddr && pasynUser->reason == axisCmdCountMotorRecordStopParam_) {
      const int enabled = value ? 1 : 0;
      countMotorRecordStopCommands_[addr].store(enabled, std::memory_order_release);
      setIntegerParam(addr, axisCmdCountMotorRecordStopParam_, enabled);
      callParamCallbacks(addr);
      return asynSuccess;
    }
    if (validAxisAddr && pasynUser->reason == axisCmdCountEnableParam_) {
      const int enabled = value ? 1 : 0;
      countEnableCommands_[addr].store(enabled, std::memory_order_release);
      setIntegerParam(addr, axisCmdCountEnableParam_, enabled);
      callParamCallbacks(addr);
      return asynSuccess;
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
    publishEcTiming();
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
  void publishEcTiming() {
    for (int slave = 0; slave < EC_MAX_SLAVES; ++slave) {
      for (int direction = 0; direction < ECMC_EC_TIMING_DIRECTIONS;
           ++direction) {
        const unsigned int versionStart =
          ecTiming_[slave][direction].version.load(std::memory_order_acquire);
        if (!versionStart || (versionStart & 1u) ||
            versionStart == ecTimingVersionPublished_[slave][direction]) {
          continue;
        }
        int64_t values[ECMC_EC_TIMING_FIELDS];
        for (int field = 0; field < ECMC_EC_TIMING_FIELDS; ++field) {
          values[field] = ecTiming_[slave][direction].value[field].load(
            std::memory_order_relaxed);
        }
        const unsigned int versionEnd =
          ecTiming_[slave][direction].version.load(std::memory_order_acquire);
        if (versionStart != versionEnd || (versionEnd & 1u)) {
          continue;
        }
        for (int field = 0; field < ECMC_EC_TIMING_FIELDS; ++field) {
          if (field < 7) {
            setIntegerParam(slave, ecTimingParam_[direction][field],
                            static_cast<int>(values[field]));
          } else {
            setInteger64Param(slave, ecTimingParam_[direction][field],
                              values[field]);
          }
        }
        ecTimingVersionPublished_[slave][direction] = versionEnd;
        callParamCallbacks(slave);
      }
    }
  }

  void publishAxisCommandCounters() {
    // Do not cache the configured axes here.  The logger port can be created
    // before all axes have been added, and its lifetime spans later
    // configuration changes.  Scanning the fixed-size slots keeps interrupt
    // callbacks working regardless of startup ordering.
    for (int axisIndex = 0; axisIndex < ECMC_MAX_AXES; ++axisIndex) {
      bool changed = false;
      const unsigned int motorRecordRequestCounter =
        counterDelta(
          axisCmdMotorRecordRequestCounts_[axisIndex].load(std::memory_order_acquire),
          axisCmdMotorRecordRequestCountBase_[axisIndex].load(std::memory_order_acquire));
      if (motorRecordRequestCounter != axisCmdMotorRecordRequestCountsPublished_[axisIndex]) {
        axisCmdMotorRecordRequestCountsPublished_[axisIndex] = motorRecordRequestCounter;
        setIntegerParam(axisIndex,
                        axisCmdMotorRecordRequestCountParam_,
                        saturateCount(motorRecordRequestCounter));
        changed = true;
      }

      const unsigned int requestCounter =
        counterDelta(
          axisCmdRequestCounts_[axisIndex].load(std::memory_order_acquire),
          axisCmdRequestCountBase_[axisIndex].load(std::memory_order_acquire));
      if (requestCounter != axisCmdRequestCountsPublished_[axisIndex]) {
        axisCmdRequestCountsPublished_[axisIndex] = requestCounter;
        setIntegerParam(axisIndex,
                        axisCmdRequestCountParam_,
                        saturateCount(requestCounter));
        changed = true;
      }

      const unsigned int executeCounter =
        counterDelta(
          axisCmdExecuteCounts_[axisIndex].load(std::memory_order_acquire),
          axisCmdExecuteCountBase_[axisIndex].load(std::memory_order_acquire));
      if (executeCounter != axisCmdExecuteCountsPublished_[axisIndex]) {
        axisCmdExecuteCountsPublished_[axisIndex] = executeCounter;
        setIntegerParam(axisIndex,
                        axisCmdExecuteCountParam_,
                        saturateCount(executeCounter));
        changed = true;
      }

      const unsigned int msBlockVersionStart =
        axisMsBlockVersions_[axisIndex].load(std::memory_order_acquire);

      if (!(msBlockVersionStart & 1u) &&
          msBlockVersionStart != axisMsBlockVersionsPublished_[axisIndex]) {
        const int msBlockState =
          axisMsBlockStates_[axisIndex].load(std::memory_order_relaxed);
        const int msBlockCycle =
          axisMsBlockCycles_[axisIndex].load(std::memory_order_relaxed);
        const unsigned int msBlockCount =
          counterDelta(
            axisMsBlockCounts_[axisIndex].load(std::memory_order_relaxed),
            axisMsBlockCountBases_[axisIndex].load(std::memory_order_relaxed));
        const unsigned int msBlockVersionEnd =
          axisMsBlockVersions_[axisIndex].load(std::memory_order_acquire);

        if (msBlockVersionStart == msBlockVersionEnd &&
            !(msBlockVersionEnd & 1u)) {
          axisMsBlockVersionsPublished_[axisIndex] = msBlockVersionEnd;
          if (msBlockState != axisMsBlockStatesPublished_[axisIndex]) {
            axisMsBlockStatesPublished_[axisIndex] = msBlockState;
            setIntegerParam(axisIndex, axisMsBlockedParam_, msBlockState);
            changed = true;
          }
          if (msBlockCycle != axisMsBlockCyclesPublished_[axisIndex]) {
            axisMsBlockCyclesPublished_[axisIndex] = msBlockCycle;
            setIntegerParam(axisIndex, axisMsBlockCycleParam_, msBlockCycle);
            changed = true;
          }
          if (msBlockCount != axisMsBlockCountsPublished_[axisIndex]) {
            axisMsBlockCountsPublished_[axisIndex] = msBlockCount;
            setIntegerParam(axisIndex,
                            axisMsBlockCountParam_,
                            saturateCount(msBlockCount));
            changed = true;
          }

          char msBlockText[ECMC_RT_LOGGER_AXIS_MS_BLOCK_TEXT_SIZE];
          buildMsBlockText(msBlockText,
                           sizeof(msBlockText),
                           msBlockState,
                           msBlockCycle,
                           msBlockCount);
          if (strcmp(msBlockText, axisMsBlockTextsPublished_[axisIndex]) != 0) {
            snprintf(axisMsBlockTextsPublished_[axisIndex],
                     sizeof(axisMsBlockTextsPublished_[axisIndex]),
                     "%s",
                     msBlockText);
            setStringParam(axisIndex, axisMsBlockTextParam_, msBlockText);
            changed = true;
          }
        }
      }

      const unsigned int mrCmdVersionStart =
        axisMrCmdVersions_[axisIndex].load(std::memory_order_acquire);

      if ((mrCmdVersionStart & 1u) ||
          mrCmdVersionStart == axisMrCmdVersionsPublished_[axisIndex]) {
        if (changed) {
          callParamCallbacks(axisIndex);
        }
        continue;
      }

      const int mrCmdType =
        axisMrCmdTypes_[axisIndex].load(std::memory_order_relaxed);
      const int mrCmdResult =
        axisMrCmdResults_[axisIndex].load(std::memory_order_relaxed);
      const int mrCmdReason =
        axisMrCmdReasons_[axisIndex].load(std::memory_order_relaxed);
      const int mrCmdError =
        axisMrCmdErrors_[axisIndex].load(std::memory_order_relaxed);
      const int mrCmdCycle =
        axisMrCmdCycles_[axisIndex].load(std::memory_order_relaxed);
      const unsigned int mrCmdVersionEnd =
        axisMrCmdVersions_[axisIndex].load(std::memory_order_acquire);

      if (mrCmdVersionStart != mrCmdVersionEnd ||
          (mrCmdVersionEnd & 1u)) {
        if (changed) {
          callParamCallbacks(axisIndex);
        }
        continue;
      }
      axisMrCmdVersionsPublished_[axisIndex] = mrCmdVersionEnd;

      if (mrCmdType != axisMrCmdTypesPublished_[axisIndex]) {
        axisMrCmdTypesPublished_[axisIndex] = mrCmdType;
        setIntegerParam(axisIndex, axisMrCmdTypeParam_, mrCmdType);
        changed = true;
      }
      if (mrCmdResult != axisMrCmdResultsPublished_[axisIndex]) {
        axisMrCmdResultsPublished_[axisIndex] = mrCmdResult;
        setIntegerParam(axisIndex, axisMrCmdResultParam_, mrCmdResult);
        changed = true;
      }
      if (mrCmdReason != axisMrCmdReasonsPublished_[axisIndex]) {
        axisMrCmdReasonsPublished_[axisIndex] = mrCmdReason;
        setIntegerParam(axisIndex, axisMrCmdReasonParam_, mrCmdReason);
        changed = true;
      }
      if (mrCmdError != axisMrCmdErrorsPublished_[axisIndex]) {
        axisMrCmdErrorsPublished_[axisIndex] = mrCmdError;
        setIntegerParam(axisIndex, axisMrCmdErrorParam_, mrCmdError);
        changed = true;
      }
      if (mrCmdCycle != axisMrCmdCyclesPublished_[axisIndex]) {
        axisMrCmdCyclesPublished_[axisIndex] = mrCmdCycle;
        setIntegerParam(axisIndex, axisMrCmdCycleParam_, mrCmdCycle);
        changed = true;
      }

      char mrCmdText[ECMC_RT_LOGGER_AXIS_MR_CMD_TEXT_SIZE];
      buildMrCmdText(mrCmdText,
                     sizeof(mrCmdText),
                     mrCmdType,
                     mrCmdResult,
                     mrCmdReason,
                     mrCmdError,
                     mrCmdCycle);
      if (strcmp(mrCmdText, axisMrCmdTextsPublished_[axisIndex]) != 0) {
        snprintf(axisMrCmdTextsPublished_[axisIndex],
                 sizeof(axisMrCmdTextsPublished_[axisIndex]),
                 "%s",
                 mrCmdText);
        setStringParam(axisIndex, axisMrCmdTextParam_, mrCmdText);
        changed = true;
      }

      if (changed) {
        callParamCallbacks(axisIndex);
      }
    }
  }

  const char *mrCommandName(int command) const {
    switch (command) {
    case 1:
      return "ABS";
    case 2:
      return "REL";
    case 3:
      return "VEL";
    case 4:
      return "HOME";
    case 5:
      return "STOP";
    default:
      return "NONE";
    }
  }

  const char *mrResultName(int result) const {
    switch (result) {
    case 1:
      return "accepted";
    case 2:
      return "rejected";
    case 3:
      return "ignored";
    case 4:
      return "deferred";
    default:
      return "none";
    }
  }

  const char *mrReasonName(int reason) const {
    switch (reason) {
    case 1:
      return "ok";
    case 2:
      return "zero_velocity";
    case 3:
      return "block_com";
    case 4:
      return "axis_blocked";
    case 5:
      return "ecmc_error";
    case 6:
      return "auto_enable_pending";
    case 7:
      return "retarget_existing_move";
    case 8:
      return "no_execute_edge";
    case 9:
      return "param_error";
    default:
      return "none";
    }
  }

  void buildMrCmdText(char *buffer,
                      size_t bufferSize,
                      int command,
                      int result,
                      int reason,
                      int errorCode,
                      int cycleCounter) const {
    if (!buffer || !bufferSize) {
      return;
    }

    snprintf(buffer,
             bufferSize,
             "%s %s %s err=0x%x cycle=%d",
             mrCommandName(command),
             mrResultName(result),
             mrReasonName(reason),
             errorCode,
             cycleCounter);
  }

  void buildMsBlockText(char *buffer,
                        size_t bufferSize,
                        int blocked,
                        int cycleCounter,
                        unsigned int count) const {
    if (!buffer || !bufferSize) {
      return;
    }

    snprintf(buffer,
             bufferSize,
             "master_slave %s cycle=%d count=%u",
             blocked ? "blocked" : (count ? "unblocked" : "none"),
             cycleCounter,
             count);
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

  unsigned int counterDelta(unsigned int value, unsigned int base) const {
    return value - base;
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
  int axisCmdClearParam_;
  int axisCmdCountMotorRecordStopParam_;
  int axisCmdCountEnableParam_;
  int axisMrCmdTypeParam_;
  int axisMrCmdResultParam_;
  int axisMrCmdReasonParam_;
  int axisMrCmdErrorParam_;
  int axisMrCmdCycleParam_;
  int axisMrCmdTextParam_;
  int axisMsBlockedParam_;
  int axisMsBlockCountParam_;
  int axisMsBlockCycleParam_;
  int axisMsBlockTextParam_;
  int ecTimingParam_[ECMC_EC_TIMING_DIRECTIONS][ECMC_EC_TIMING_FIELDS];
  unsigned int ecTimingVersionPublished_[EC_MAX_SLAVES]
                                                [ECMC_EC_TIMING_DIRECTIONS];
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
  std::atomic<unsigned int> axisCmdMotorRecordRequestCountBase_[ECMC_MAX_AXES];
  std::atomic<unsigned int> axisCmdRequestCountBase_[ECMC_MAX_AXES];
  std::atomic<unsigned int> axisCmdExecuteCountBase_[ECMC_MAX_AXES];
  int axisMrCmdTypesPublished_[ECMC_MAX_AXES];
  int axisMrCmdResultsPublished_[ECMC_MAX_AXES];
  int axisMrCmdReasonsPublished_[ECMC_MAX_AXES];
  int axisMrCmdErrorsPublished_[ECMC_MAX_AXES];
  int axisMrCmdCyclesPublished_[ECMC_MAX_AXES];
  unsigned int axisMrCmdVersionsPublished_[ECMC_MAX_AXES];
  char axisMrCmdTextsPublished_[ECMC_MAX_AXES][ECMC_RT_LOGGER_AXIS_MR_CMD_TEXT_SIZE];
  int axisMsBlockStatesPublished_[ECMC_MAX_AXES];
  int axisMsBlockCyclesPublished_[ECMC_MAX_AXES];
  unsigned int axisMsBlockCountsPublished_[ECMC_MAX_AXES];
  unsigned int axisMsBlockVersionsPublished_[ECMC_MAX_AXES];
  char axisMsBlockTextsPublished_[ECMC_MAX_AXES][ECMC_RT_LOGGER_AXIS_MS_BLOCK_TEXT_SIZE];
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

void ecmcRtLoggerPortDriverSetAxisMotorRecordCommandResult(int axisIndex,
                                                           int command,
                                                           int result,
                                                           int reason,
                                                           int errorCode,
                                                           int cycleCounter) {
  if (axisIndex < 0 || axisIndex >= ECMC_MAX_AXES) {
    return;
  }

  axisMrCmdVersions_[axisIndex].fetch_add(1, std::memory_order_acq_rel);
  axisMrCmdTypes_[axisIndex].store(command, std::memory_order_relaxed);
  axisMrCmdResults_[axisIndex].store(result, std::memory_order_relaxed);
  axisMrCmdReasons_[axisIndex].store(reason, std::memory_order_relaxed);
  axisMrCmdErrors_[axisIndex].store(errorCode, std::memory_order_relaxed);
  axisMrCmdCycles_[axisIndex].store(cycleCounter, std::memory_order_relaxed);
  axisMrCmdVersions_[axisIndex].fetch_add(1, std::memory_order_release);
}

void ecmcRtLoggerPortDriverSetAxisMasterSlaveBlock(int axisIndex,
                                                   int blocked,
                                                   int cycleCounter) {
  if (axisIndex < 0 || axisIndex >= ECMC_MAX_AXES) {
    return;
  }

  axisMsBlockVersions_[axisIndex].fetch_add(1, std::memory_order_acq_rel);
  axisMsBlockStates_[axisIndex].store(blocked ? 1 : 0, std::memory_order_relaxed);
  axisMsBlockCycles_[axisIndex].store(cycleCounter, std::memory_order_relaxed);
  axisMsBlockCounts_[axisIndex].fetch_add(1, std::memory_order_relaxed);
  axisMsBlockVersions_[axisIndex].fetch_add(1, std::memory_order_release);
}

void ecmcRtLoggerPortDriverSetEcTiming(int slavePosition,
                                      int direction,
                                      const ecmcEcTimingDiag *timing) {
  if (!timing || slavePosition < 0 || slavePosition >= EC_MAX_SLAVES ||
      direction < 0 || direction >= ECMC_EC_TIMING_DIRECTIONS) {
    return;
  }
  atomicEcTimingDiag& target = ecTiming_[slavePosition][direction];
  target.version.fetch_add(1, std::memory_order_acq_rel);
  const int64_t values[ECMC_EC_TIMING_FIELDS] = {
    timing->status, timing->source, timing->reference, timing->syncType,
    timing->cycleOffset, timing->updateDivisor, timing->timestampBits,
    timing->cycleTimeNs, timing->shiftTimeNs, timing->calculationCopyTimeNs,
    timing->eventOffsetNs, timing->uncertaintyNs,
    timing->timestampCorrectionNs
  };
  for (int field = 0; field < ECMC_EC_TIMING_FIELDS; ++field) {
    target.value[field].store(values[field], std::memory_order_relaxed);
  }
  target.version.fetch_add(1, std::memory_order_release);
}

int ecmcRtLoggerPortDriverGetCountMotorRecordStopCommands(int axisIndex) {
  if (!portDriver_ || axisIndex < 0 || axisIndex >= ECMC_MAX_AXES) {
    return 0;
  }
  return countMotorRecordStopCommands_[axisIndex].load(std::memory_order_acquire);
}

int ecmcRtLoggerPortDriverGetCountEnableCommands(int axisIndex) {
  if (!portDriver_ || axisIndex < 0 || axisIndex >= ECMC_MAX_AXES) {
    return 0;
  }
  return countEnableCommands_[axisIndex].load(std::memory_order_acquire);
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
