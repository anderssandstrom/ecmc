/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcPositionCompare.h
*
*  Generic timed position compare.
\*************************************************************************/

#ifndef ECMCPOSITIONCOMPARE_H_
#define ECMCPOSITIONCOMPARE_H_

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <stdint.h>
#include "ecmcEcEntryLink.h"

#define ECMC_POS_COMPARE_ENTRY_OUTPUT      0
#define ECMC_POS_COMPARE_ENTRY_ACTIVATE    1
#define ECMC_POS_COMPARE_ENTRY_START_TIME  2

enum ecmcPositionCompareState {
  ECMC_POS_COMPARE_DISABLED = 0,
  ECMC_POS_COMPARE_ARMED    = 1,
  ECMC_POS_COMPARE_QUEUED   = 2,
  ECMC_POS_COMPARE_FIRED    = 3,
  ECMC_POS_COMPARE_MISSED   = 4,
  ECMC_POS_COMPARE_ERROR    = 5,
  ECMC_POS_COMPARE_RESET_QUEUED = 6
};

enum ecmcPositionCompareReason {
  ECMC_POS_COMPARE_REASON_NONE = 0,
  ECMC_POS_COMPARE_REASON_ARMED = 1,
  ECMC_POS_COMPARE_REASON_IDLE_CYCLE = 2,
  ECMC_POS_COMPARE_REASON_WAIT_DIRECTION = 3,
  ECMC_POS_COMPARE_REASON_WAIT_VELOCITY = 4,
  ECMC_POS_COMPARE_REASON_TOO_FAR = 5,
  ECMC_POS_COMPARE_REASON_TOO_LATE = 6,
  ECMC_POS_COMPARE_REASON_TOO_CLOSE = 7,
  ECMC_POS_COMPARE_REASON_SCHEDULED_OUTPUT = 8,
  ECMC_POS_COMPARE_REASON_SCHEDULED_RESET = 9,
  ECMC_POS_COMPARE_REASON_FIRED = 10,
  ECMC_POS_COMPARE_REASON_RESET_DONE = 11,
  ECMC_POS_COMPARE_REASON_ERROR = 12,
  ECMC_POS_COMPARE_REASON_MISSED = 13
};

struct ecmcPositionCompareStatus {
  ecmcPositionCompareState state;
  ecmcPositionCompareReason reason;
  uint64_t sequence;
  double target;
  double position;
  double velocity;
  int direction;
  uint64_t scheduledTimeNs;
  int64_t leadTimeNs;
  int64_t sampleAgeNs;
  uint64_t outputValue;
  uint64_t resetValue;
  uint64_t pulseWidthNs;
  uint64_t resetTimeNs;
  uint64_t sampleTimeNs;
  uint64_t controllerTimeNs;
  uint64_t eventTimeNs;
  uint64_t lastActivateValue;
  uint64_t lastOutputValue;

  ecmcPositionCompareStatus() :
    state(ECMC_POS_COMPARE_DISABLED),
    reason(ECMC_POS_COMPARE_REASON_NONE),
    sequence(0),
    target(0),
    position(0),
    velocity(0),
    direction(0),
    scheduledTimeNs(0),
    leadTimeNs(0),
    sampleAgeNs(0),
    outputValue(0),
    resetValue(0),
    pulseWidthNs(0),
    resetTimeNs(0),
    sampleTimeNs(0),
    controllerTimeNs(0),
    eventTimeNs(0),
    lastActivateValue(0),
    lastOutputValue(0) {}
};

class ecmcPositionCompare : public ecmcEcEntryLink {
public:
  ecmcPositionCompare();
  int configure(uint64_t minLeadTimeNs,
                uint64_t maxLeadTimeNs,
                uint64_t activateIdle,
                uint64_t activateSchedule,
                uint64_t pulseWidthNs,
                uint64_t resetValue);
  int validate();
  int arm(double target, int direction, uint64_t outputValue);
  int cancel();
  void execute(bool masterOK,
               double position,
               double velocity,
               uint64_t sampleTimeNs,
               bool sampleTimeValid,
               uint64_t controllerTimeNs);
  ecmcPositionCompareStatus getStatus() const;
  bool isLinked() const;
  bool isActive() const;

private:
  int scheduleEvent(uint64_t outputValue, uint64_t eventTimeNs);
  void writeIdleActivate();
  bool directionMatches(double distance, double velocity) const;

  uint64_t minLeadTimeNs_;
  uint64_t maxLeadTimeNs_;
  uint64_t activateIdle_;
  uint64_t activateSchedule_;
  uint64_t pulseWidthNs_;
  uint64_t resetValue_;
  ecmcPositionCompareStatus status_;
  bool linked_;
  bool activateIdlePending_;
};

#endif  /* ECMCPOSITIONCOMPARE_H_ */
