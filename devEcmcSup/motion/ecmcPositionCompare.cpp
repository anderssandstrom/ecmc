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
#include <limits>

ecmcPositionCompare::ecmcPositionCompare() :
  minLeadTimeNs_(2000000),
  maxLeadTimeNs_(100000000),
  activateIdle_(3),
  activateSchedule_(0),
  pulseWidthNs_(0),
  resetValue_(0),
  linked_(false),
  activateIdlePending_(false) {}

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
