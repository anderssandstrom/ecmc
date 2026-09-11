/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcPositionCompare.cpp
\*************************************************************************/

#include "ecmcPositionCompare.h"
#include "ecmcErrorsList.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <stdio.h>

namespace {
asynStatus asynWritePositionCompareTargetCmd(void *data,
                                             size_t bytes,
                                             asynParamType asynParType,
                                             void *userObj) {
  if (!userObj) {
    return asynError;
  }
  return static_cast<ecmcPositionCompare*>(userObj)->
         asynWriteTargetCmd(data, bytes, asynParType);
}

asynStatus asynWritePositionCompareDirectionCmd(void *data,
                                                size_t bytes,
                                                asynParamType asynParType,
                                                void *userObj) {
  if (!userObj) {
    return asynError;
  }
  return static_cast<ecmcPositionCompare*>(userObj)->
         asynWriteDirectionCmd(data, bytes, asynParType);
}

asynStatus asynWritePositionCompareOutputCmd(void *data,
                                             size_t bytes,
                                             asynParamType asynParType,
                                             void *userObj) {
  if (!userObj) {
    return asynError;
  }
  return static_cast<ecmcPositionCompare*>(userObj)->
         asynWriteOutputCmd(data, bytes, asynParType);
}

asynStatus asynWritePositionCompareArmCmd(void *data,
                                          size_t bytes,
                                          asynParamType asynParType,
                                          void *userObj) {
  if (!userObj) {
    return asynError;
  }
  return static_cast<ecmcPositionCompare*>(userObj)->
         asynWriteArmCmd(data, bytes, asynParType);
}

asynStatus asynWritePositionCompareCancelCmd(void *data,
                                             size_t bytes,
                                             asynParamType asynParType,
                                             void *userObj) {
  if (!userObj) {
    return asynError;
  }
  return static_cast<ecmcPositionCompare*>(userObj)->
         asynWriteCancelCmd(data, bytes, asynParType);
}
}

ecmcPositionCompare::ecmcPositionCompare() :
  minLeadTimeNs_(2000000),
  maxLeadTimeNs_(100000000),
  activateIdle_(3),
  activateSchedule_(0),
  pulseWidthNs_(0),
  resetValue_(0),
  linked_(false),
  activateIdlePending_(false),
  asynParamsCreated_(false),
  asynState_(ECMC_POS_COMPARE_DISABLED),
  asynReason_(ECMC_POS_COMPARE_REASON_NONE),
  asynDirection_(0),
  asynTargetCmd_(0),
  asynDirectionCmd_(0),
  asynOutputCmd_(1),
  asynArmCmd_(0),
  asynCancelCmd_(0),
  asynStateParam_(NULL),
  asynReasonParam_(NULL),
  asynSequenceParam_(NULL),
  asynTargetParam_(NULL),
  asynPositionParam_(NULL),
  asynVelocityParam_(NULL),
  asynDirectionParam_(NULL),
  asynScheduledTimeParam_(NULL),
  asynLeadTimeParam_(NULL),
  asynSampleAgeParam_(NULL),
  asynPulseWidthParam_(NULL),
  asynResetTimeParam_(NULL),
  asynLastActivateParam_(NULL),
  asynLastOutputParam_(NULL),
  asynTargetCmdParam_(NULL),
  asynDirectionCmdParam_(NULL),
  asynOutputCmdParam_(NULL),
  asynArmCmdParam_(NULL),
  asynCancelCmdParam_(NULL) {}

int ecmcPositionCompare::configure(uint64_t minLeadTimeNs,
                                   uint64_t maxLeadTimeNs,
                                   uint64_t activateIdle,
                                   uint64_t activateSchedule,
                                   uint64_t pulseWidthNs,
                                   uint64_t resetValue) {
  if (minLeadTimeNs > maxLeadTimeNs) {
    return ERROR_MAIN_ECMC_COMMAND_FORMAT_ERROR;
  }
  minLeadTimeNs_ = minLeadTimeNs;
  maxLeadTimeNs_ = maxLeadTimeNs;
  activateIdle_ = activateIdle;
  activateSchedule_ = activateSchedule;
  pulseWidthNs_ = pulseWidthNs;
  resetValue_ = resetValue;
  return 0;
}

int ecmcPositionCompare::validate() {
  if (!checkEntryExist(ECMC_POS_COMPARE_ENTRY_OUTPUT) ||
      !checkEntryExist(ECMC_POS_COMPARE_ENTRY_ACTIVATE) ||
      !checkEntryExist(ECMC_POS_COMPARE_ENTRY_START_TIME)) {
    linked_ = false;
    return ERROR_MAIN_EC_ENTRY_NULL;
  }
  int error = validateEntry(ECMC_POS_COMPARE_ENTRY_OUTPUT);
  if (error) {
    linked_ = false;
    return error;
  }
  error = validateEntry(ECMC_POS_COMPARE_ENTRY_ACTIVATE);
  if (error) {
    linked_ = false;
    return error;
  }
  error = validateEntry(ECMC_POS_COMPARE_ENTRY_START_TIME);
  linked_ = error == 0;
  return error;
}

int ecmcPositionCompare::arm(double target,
                             int direction,
                             uint64_t outputValue) {
  if (!linked_) {
    return ERROR_MAIN_EC_ENTRY_NULL;
  }
  if (direction < -1 || direction > 1) {
    return ERROR_MAIN_ECMC_COMMAND_FORMAT_ERROR;
  }
  status_.target = target;
  status_.direction = direction;
  status_.outputValue = outputValue;
  status_.scheduledTimeNs = 0;
  status_.leadTimeNs = 0;
  status_.sampleAgeNs = 0;
  status_.resetTimeNs = 0;
  status_.resetValue = resetValue_;
  status_.pulseWidthNs = pulseWidthNs_;
  status_.eventTimeNs = 0;
  status_.reason = ECMC_POS_COMPARE_REASON_ARMED;
  // Force at least one RT cycle with the terminal in idle before a new
  // schedule command is allowed. Otherwise a command-thread re-arm can write
  // idle and the next RT cycle can overwrite it with schedule before the
  // EL2252 has observed a clean idle->schedule transition.
  activateIdlePending_ = true;
  writeIdleActivate();
  status_.state = ECMC_POS_COMPARE_ARMED;
  return 0;
}

int ecmcPositionCompare::cancel() {
  status_.state = ECMC_POS_COMPARE_DISABLED;
  status_.scheduledTimeNs = 0;
  status_.resetTimeNs = 0;
  status_.reason = ECMC_POS_COMPARE_REASON_NONE;
  activateIdlePending_ = false;
  writeIdleActivate();
  return 0;
}

void ecmcPositionCompare::execute(bool masterOK,
                                  double position,
                                  double velocity,
                                  uint64_t sampleTimeNs,
                                  bool sampleTimeValid,
                                  uint64_t controllerTimeNs) {
  if (!linked_ ||
      (status_.state != ECMC_POS_COMPARE_ARMED &&
       status_.state != ECMC_POS_COMPARE_QUEUED &&
       status_.state != ECMC_POS_COMPARE_RESET_QUEUED)) {
    return;
  }

  status_.position = position;
  status_.velocity = velocity;
  status_.sampleTimeNs = sampleTimeNs;
  status_.controllerTimeNs = controllerTimeNs;

  if (!masterOK || !sampleTimeValid || !controllerTimeNs || !sampleTimeNs) {
    status_.state = ECMC_POS_COMPARE_ERROR;
    status_.reason = ECMC_POS_COMPARE_REASON_ERROR;
    return;
  }

  if (activateIdlePending_) {
    writeIdleActivate();
    activateIdlePending_ = false;
    status_.reason = ECMC_POS_COMPARE_REASON_IDLE_CYCLE;
    return;
  }

  if (status_.state == ECMC_POS_COMPARE_QUEUED) {
    if (controllerTimeNs >= status_.scheduledTimeNs) {
      if (pulseWidthNs_) {
        if (pulseWidthNs_ >
            std::numeric_limits<uint64_t>::max() - status_.scheduledTimeNs) {
          status_.state = ECMC_POS_COMPARE_ERROR;
          return;
        }
        uint64_t resetTimeNs = status_.scheduledTimeNs + pulseWidthNs_;
        const uint64_t earliestResetNs =
          controllerTimeNs > std::numeric_limits<uint64_t>::max() -
          minLeadTimeNs_ ? std::numeric_limits<uint64_t>::max() :
          controllerTimeNs + minLeadTimeNs_;
        if (resetTimeNs < earliestResetNs) {
          resetTimeNs = earliestResetNs;
        }
        if (scheduleEvent(resetValue_, resetTimeNs)) {
          status_.state = ECMC_POS_COMPARE_ERROR;
          return;
        }
        status_.resetTimeNs = resetTimeNs;
        status_.state = ECMC_POS_COMPARE_RESET_QUEUED;
        status_.reason = ECMC_POS_COMPARE_REASON_SCHEDULED_RESET;
        return;
      }
      status_.state = ECMC_POS_COMPARE_FIRED;
      status_.reason = ECMC_POS_COMPARE_REASON_FIRED;
    }
    return;
  }

  if (status_.state == ECMC_POS_COMPARE_RESET_QUEUED) {
    if (controllerTimeNs >= status_.resetTimeNs) {
      status_.state = ECMC_POS_COMPARE_FIRED;
      status_.reason = ECMC_POS_COMPARE_REASON_RESET_DONE;
    }
    return;
  }

  const double distance = status_.target - position;
  if (std::fabs(velocity) < 1E-12) {
    status_.reason = ECMC_POS_COMPARE_REASON_WAIT_VELOCITY;
    return;
  }
  if (!directionMatches(distance, velocity)) {
    status_.reason = ECMC_POS_COMPARE_REASON_WAIT_DIRECTION;
    return;
  }

  const long double dtNs =
    static_cast<long double>(distance) /
    static_cast<long double>(velocity) * 1E9L;
  if (dtNs < 0 ||
      dtNs > static_cast<long double>(std::numeric_limits<uint64_t>::max())) {
    status_.state = ECMC_POS_COMPARE_MISSED;
    status_.reason = ECMC_POS_COMPARE_REASON_MISSED;
    return;
  }
  const uint64_t dtRoundedNs = static_cast<uint64_t>(dtNs + 0.5L);
  if (dtRoundedNs > std::numeric_limits<uint64_t>::max() - sampleTimeNs) {
    status_.state = ECMC_POS_COMPARE_MISSED;
    status_.reason = ECMC_POS_COMPARE_REASON_MISSED;
    return;
  }
  const uint64_t eventTimeNs = sampleTimeNs + dtRoundedNs;
  status_.eventTimeNs = eventTimeNs;
  if (eventTimeNs < controllerTimeNs) {
    // Too late for this crossing. Keep the compare armed so a cyclic or
    // reciprocating motion can still schedule the next valid crossing.
    status_.reason = ECMC_POS_COMPARE_REASON_TOO_LATE;
    return;
  }

  const uint64_t lead = eventTimeNs - controllerTimeNs;
  status_.leadTimeNs = static_cast<int64_t>(lead);
  status_.sampleAgeNs = controllerTimeNs >= sampleTimeNs ?
    static_cast<int64_t>(controllerTimeNs - sampleTimeNs) :
    -static_cast<int64_t>(sampleTimeNs - controllerTimeNs);
  if (lead > maxLeadTimeNs_) {
    status_.reason = ECMC_POS_COMPARE_REASON_TOO_FAR;
    return;
  }
  if (lead < minLeadTimeNs_) {
    // Crossing is inside the unsafe scheduling margin. Keep armed and wait
    // for the next approach instead of making this a terminal condition.
    status_.reason = ECMC_POS_COMPARE_REASON_TOO_CLOSE;
    return;
  }

  if (scheduleEvent(status_.outputValue, eventTimeNs)) {
    status_.state = ECMC_POS_COMPARE_ERROR;
    status_.reason = ECMC_POS_COMPARE_REASON_ERROR;
    return;
  }

  status_.sequence++;
  status_.scheduledTimeNs = eventTimeNs;
  status_.state = ECMC_POS_COMPARE_QUEUED;
  status_.reason = ECMC_POS_COMPARE_REASON_SCHEDULED_OUTPUT;
}

ecmcPositionCompareStatus ecmcPositionCompare::getStatus() const {
  return status_;
}

bool ecmcPositionCompare::isLinked() const {
  return linked_;
}

bool ecmcPositionCompare::isActive() const {
  return status_.state == ECMC_POS_COMPARE_ARMED ||
         status_.state == ECMC_POS_COMPARE_QUEUED ||
         status_.state == ECMC_POS_COMPARE_RESET_QUEUED;
}

int ecmcPositionCompare::createAsynParams(ecmcAsynPortDriver *asynPortDriver,
                                          int axisId) {
  if (asynParamsCreated_) {
    return 0;
  }
  if (!asynPortDriver) {
    return ERROR_AXIS_ASYN_PORT_OBJ_NULL;
  }

  int error = 0;
  error = createAsynParam(asynPortDriver, axisId, "poscomp.state",
                          asynParamInt32, ECMC_EC_S32,
                          reinterpret_cast<uint8_t*>(&asynState_),
                          sizeof(asynState_), &asynStateParam_);
  if (error) return error;
  error = createAsynParam(asynPortDriver, axisId, "poscomp.reason",
                          asynParamInt32, ECMC_EC_S32,
                          reinterpret_cast<uint8_t*>(&asynReason_),
                          sizeof(asynReason_), &asynReasonParam_);
  if (error) return error;
  error = createAsynParam(asynPortDriver, axisId, "poscomp.sequence",
                          asynParamInt64, ECMC_EC_U64,
                          reinterpret_cast<uint8_t*>(&status_.sequence),
                          sizeof(status_.sequence), &asynSequenceParam_);
  if (error) return error;
  error = createAsynParam(asynPortDriver, axisId, "poscomp.target",
                          asynParamFloat64, ECMC_EC_F64,
                          reinterpret_cast<uint8_t*>(&status_.target),
                          sizeof(status_.target), &asynTargetParam_);
  if (error) return error;
  error = createAsynParam(asynPortDriver, axisId, "poscomp.position",
                          asynParamFloat64, ECMC_EC_F64,
                          reinterpret_cast<uint8_t*>(&status_.position),
                          sizeof(status_.position), &asynPositionParam_);
  if (error) return error;
  error = createAsynParam(asynPortDriver, axisId, "poscomp.velocity",
                          asynParamFloat64, ECMC_EC_F64,
                          reinterpret_cast<uint8_t*>(&status_.velocity),
                          sizeof(status_.velocity), &asynVelocityParam_);
  if (error) return error;
  error = createAsynParam(asynPortDriver, axisId, "poscomp.direction",
                          asynParamInt32, ECMC_EC_S32,
                          reinterpret_cast<uint8_t*>(&asynDirection_),
                          sizeof(asynDirection_), &asynDirectionParam_);
  if (error) return error;
  error = createAsynParam(asynPortDriver, axisId, "poscomp.scheduledtimens",
                          asynParamInt64, ECMC_EC_U64,
                          reinterpret_cast<uint8_t*>(&status_.scheduledTimeNs),
                          sizeof(status_.scheduledTimeNs),
                          &asynScheduledTimeParam_);
  if (error) return error;
  error = createAsynParam(asynPortDriver, axisId, "poscomp.leadtimens",
                          asynParamInt64, ECMC_EC_S64,
                          reinterpret_cast<uint8_t*>(&status_.leadTimeNs),
                          sizeof(status_.leadTimeNs), &asynLeadTimeParam_);
  if (error) return error;
  error = createAsynParam(asynPortDriver, axisId, "poscomp.sampleagens",
                          asynParamInt64, ECMC_EC_S64,
                          reinterpret_cast<uint8_t*>(&status_.sampleAgeNs),
                          sizeof(status_.sampleAgeNs), &asynSampleAgeParam_);
  if (error) return error;
  error = createAsynParam(asynPortDriver, axisId, "poscomp.pulsewidthns",
                          asynParamInt64, ECMC_EC_U64,
                          reinterpret_cast<uint8_t*>(&status_.pulseWidthNs),
                          sizeof(status_.pulseWidthNs), &asynPulseWidthParam_);
  if (error) return error;
  error = createAsynParam(asynPortDriver, axisId, "poscomp.resettimens",
                          asynParamInt64, ECMC_EC_U64,
                          reinterpret_cast<uint8_t*>(&status_.resetTimeNs),
                          sizeof(status_.resetTimeNs), &asynResetTimeParam_);
  if (error) return error;
  error = createAsynParam(asynPortDriver, axisId, "poscomp.lastactivate",
                          asynParamInt64, ECMC_EC_U64,
                          reinterpret_cast<uint8_t*>(&status_.lastActivateValue),
                          sizeof(status_.lastActivateValue),
                          &asynLastActivateParam_);
  if (error) return error;
  error = createAsynParam(asynPortDriver, axisId, "poscomp.lastoutput",
                          asynParamInt64, ECMC_EC_U64,
                          reinterpret_cast<uint8_t*>(&status_.lastOutputValue),
                          sizeof(status_.lastOutputValue),
                          &asynLastOutputParam_);
  if (error) return error;
  error = createAsynParam(asynPortDriver, axisId, "poscomp.targetcmd",
                          asynParamFloat64, ECMC_EC_F64,
                          reinterpret_cast<uint8_t*>(&asynTargetCmd_),
                          sizeof(asynTargetCmd_), &asynTargetCmdParam_);
  if (error) return error;
  asynTargetCmdParam_->setAllowWriteToEcmc(true);
  asynTargetCmdParam_->setExeCmdFunctPtr(asynWritePositionCompareTargetCmd,
                                         this);
  error = createAsynParam(asynPortDriver, axisId, "poscomp.directioncmd",
                          asynParamInt32, ECMC_EC_S32,
                          reinterpret_cast<uint8_t*>(&asynDirectionCmd_),
                          sizeof(asynDirectionCmd_), &asynDirectionCmdParam_);
  if (error) return error;
  asynDirectionCmdParam_->setAllowWriteToEcmc(true);
  asynDirectionCmdParam_->setExeCmdFunctPtr(asynWritePositionCompareDirectionCmd,
                                            this);
  error = createAsynParam(asynPortDriver, axisId, "poscomp.outputcmd",
                          asynParamInt64, ECMC_EC_U64,
                          reinterpret_cast<uint8_t*>(&asynOutputCmd_),
                          sizeof(asynOutputCmd_), &asynOutputCmdParam_);
  if (error) return error;
  asynOutputCmdParam_->setAllowWriteToEcmc(true);
  asynOutputCmdParam_->setExeCmdFunctPtr(asynWritePositionCompareOutputCmd,
                                         this);
  error = createAsynParam(asynPortDriver, axisId, "poscomp.armcmd",
                          asynParamInt32, ECMC_EC_S32,
                          reinterpret_cast<uint8_t*>(&asynArmCmd_),
                          sizeof(asynArmCmd_), &asynArmCmdParam_);
  if (error) return error;
  asynArmCmdParam_->setAllowWriteToEcmc(true);
  asynArmCmdParam_->setExeCmdFunctPtr(asynWritePositionCompareArmCmd, this);
  error = createAsynParam(asynPortDriver, axisId, "poscomp.cancelcmd",
                          asynParamInt32, ECMC_EC_S32,
                          reinterpret_cast<uint8_t*>(&asynCancelCmd_),
                          sizeof(asynCancelCmd_), &asynCancelCmdParam_);
  if (error) return error;
  asynCancelCmdParam_->setAllowWriteToEcmc(true);
  asynCancelCmdParam_->setExeCmdFunctPtr(asynWritePositionCompareCancelCmd,
                                         this);

  asynParamsCreated_ = true;
  refreshAsyn(true);
  return 0;
}

int ecmcPositionCompare::createAsynParam(
  ecmcAsynPortDriver *asynPortDriver,
  int axisId,
  const char *name,
  asynParamType asynType,
  ecmcEcDataType ecmcType,
  uint8_t *data,
  size_t bytes,
  ecmcAsynDataItem **asynParamOut) {
  char buffer[EC_MAX_OBJECT_PATH_CHAR_LENGTH];
  const int charCount = snprintf(buffer,
                                 sizeof(buffer),
                                 ECMC_AX_STR "%d.%s",
                                 axisId,
                                 name);
  if (charCount >= static_cast<int>(sizeof(buffer)) - 1) {
    return ERROR_AXIS_ASYN_PRINT_TO_BUFFER_FAIL;
  }
  ecmcAsynDataItem *param =
    asynPortDriver->addNewAvailParam(buffer, asynType, data, bytes, ecmcType, 0);
  if (!param) {
    return ERROR_MAIN_ASYN_CREATE_PARAM_FAIL;
  }
  param->setAllowWriteToEcmc(false);
  param->refreshParam(1);
  *asynParamOut = param;
  return 0;
}

asynStatus ecmcPositionCompare::asynWriteTargetCmd(void *data,
                                                   size_t bytes,
                                                   asynParamType asynParType) {
  if (bytes != sizeof(double) || asynParType != asynParamFloat64) {
    return asynError;
  }
  memcpy(&asynTargetCmd_, data, bytes);
  return asynSuccess;
}

asynStatus ecmcPositionCompare::asynWriteDirectionCmd(
  void *data,
  size_t bytes,
  asynParamType asynParType) {
  if (bytes != sizeof(int32_t) || asynParType != asynParamInt32) {
    return asynError;
  }
  int32_t direction = 0;
  memcpy(&direction, data, bytes);
  if (direction < -1 || direction > 1) {
    return asynError;
  }
  asynDirectionCmd_ = direction;
  return asynSuccess;
}

asynStatus ecmcPositionCompare::asynWriteOutputCmd(void *data,
                                                   size_t bytes,
                                                   asynParamType asynParType) {
  if (bytes != sizeof(uint64_t) || asynParType != asynParamInt64) {
    return asynError;
  }
  memcpy(&asynOutputCmd_, data, bytes);
  return asynSuccess;
}

asynStatus ecmcPositionCompare::asynWriteArmCmd(void *data,
                                                size_t bytes,
                                                asynParamType asynParType) {
  if (bytes != sizeof(int32_t) || asynParType != asynParamInt32) {
    return asynError;
  }
  memcpy(&asynArmCmd_, data, bytes);
  if (!asynArmCmd_) {
    return asynSuccess;
  }
  return arm(asynTargetCmd_, asynDirectionCmd_, asynOutputCmd_) ?
         asynError : asynSuccess;
}

asynStatus ecmcPositionCompare::asynWriteCancelCmd(void *data,
                                                   size_t bytes,
                                                   asynParamType asynParType) {
  if (bytes != sizeof(int32_t) || asynParType != asynParamInt32) {
    return asynError;
  }
  memcpy(&asynCancelCmd_, data, bytes);
  if (!asynCancelCmd_) {
    return asynSuccess;
  }
  return cancel() ? asynError : asynSuccess;
}

void ecmcPositionCompare::refreshAsyn(bool force) {
  if (!asynParamsCreated_) {
    return;
  }
  updateAsynShadow();
  asynStateParam_->refreshParamRT(force);
  asynReasonParam_->refreshParamRT(force);
  asynSequenceParam_->refreshParamRT(force);
  asynTargetParam_->refreshParamRT(force);
  asynPositionParam_->refreshParamRT(force);
  asynVelocityParam_->refreshParamRT(force);
  asynDirectionParam_->refreshParamRT(force);
  asynScheduledTimeParam_->refreshParamRT(force);
  asynLeadTimeParam_->refreshParamRT(force);
  asynSampleAgeParam_->refreshParamRT(force);
  asynPulseWidthParam_->refreshParamRT(force);
  asynResetTimeParam_->refreshParamRT(force);
  asynLastActivateParam_->refreshParamRT(force);
  asynLastOutputParam_->refreshParamRT(force);
}

void ecmcPositionCompare::updateAsynShadow() {
  asynState_ = static_cast<int32_t>(status_.state);
  asynReason_ = static_cast<int32_t>(status_.reason);
  asynDirection_ = static_cast<int32_t>(status_.direction);
}

int ecmcPositionCompare::scheduleEvent(uint64_t outputValue,
                                       uint64_t eventTimeNs) {
  if (writeEcEntryValue(ECMC_POS_COMPARE_ENTRY_OUTPUT, outputValue) ||
      writeEcEntryValue(ECMC_POS_COMPARE_ENTRY_START_TIME, eventTimeNs) ||
      writeEcEntryValue(ECMC_POS_COMPARE_ENTRY_ACTIVATE, activateSchedule_)) {
    return ERROR_EC_ENTRY_WRITE_FAIL;
  }
  status_.lastOutputValue = outputValue;
  status_.lastActivateValue = activateSchedule_;
  activateIdlePending_ = true;
  return 0;
}

void ecmcPositionCompare::writeIdleActivate() {
  if (linked_) {
    writeEcEntryValue(ECMC_POS_COMPARE_ENTRY_ACTIVATE, activateIdle_);
    status_.lastActivateValue = activateIdle_;
  }
}

bool ecmcPositionCompare::directionMatches(double distance,
                                           double velocity) const {
  if (status_.direction > 0) {
    return distance >= 0 && velocity > 0;
  }
  if (status_.direction < 0) {
    return distance <= 0 && velocity < 0;
  }
  return (distance >= 0 && velocity > 0) ||
         (distance <= 0 && velocity < 0);
}
