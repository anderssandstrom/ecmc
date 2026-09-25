/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcEcTiming.h
*
*  Common timing types for relating cyclic PDO data to physical events.
\*************************************************************************/

#ifndef ECMCECTIMING_H_
#define ECMCECTIMING_H_

#include <stdint.h>
#include <limits>

enum class ecmcEcTimingSource : uint8_t {
  CYCLE_ONLY = 0,
  SYNC0_DERIVED,
  SYNC1_DERIVED,
  HARDWARE_TIMESTAMP
};

enum class ecmcEcTimingReference : uint8_t {
  RECEIVE = 0,
  APPLICATION,
  SYNC
};

/* Quality travels with a value so consumers can distinguish an exact hardware
 * event from a nominal schedule reconstruction or a cycle-bounded estimate. */
enum class ecmcEcTimeQuality : uint8_t {
  INVALID = 0,
  CYCLE_BOUNDED,
  DC_SCHEDULE,
  HARDWARE_TIMESTAMP
};

template<typename T>
struct ecmcEcTimedValue {
  T value;
  uint64_t eventTimeNs;
  uint64_t sequence;
  uint32_t uncertaintyNs;
  ecmcEcTimeQuality quality;
  bool valid;

  ecmcEcTimedValue() :
    value(),
    eventTimeNs(0),
    sequence(0),
    uncertaintyNs(0),
    quality(ecmcEcTimeQuality::INVALID),
    valid(false) {}
};

struct ecmcEcEndpointTiming {
  bool valid;
  ecmcEcTimingSource source;
  ecmcEcTimingReference reference;
  int32_t cycleOffset;
  int32_t eventOffsetNs;
  uint32_t uncertaintyNs;
  uint32_t updateDivisor;
  bool cycleOffsetKnown;
  bool eventOffsetKnown;
  bool overrideApplied;
  bool calculationCopyTimeAvailable;
  uint32_t calculationCopyTimeNs;
  bool timestampLinked;
  uint8_t timestampBits;
  int32_t timestampCorrectionNs;

  ecmcEcEndpointTiming() :
    valid(false),
    source(ecmcEcTimingSource::CYCLE_ONLY),
    reference(ecmcEcTimingReference::RECEIVE),
    cycleOffset(0),
    eventOffsetNs(0),
    uncertaintyNs(0),
    updateDivisor(1),
    cycleOffsetKnown(false),
    eventOffsetKnown(false),
    overrideApplied(false),
    calculationCopyTimeAvailable(false),
    calculationCopyTimeNs(0),
    timestampLinked(false),
    timestampBits(0),
    timestampCorrectionNs(0) {}
};

/* Optional terminal-specific correction supplied by the hardware
 * configuration. cycleOffset is relative to the most recent selected endpoint
 * event at or before application time. eventOffsetNs is relative to the SYNC
 * event selected by the terminal's 0x1C32/0x1C33 synchronization type. */
struct ecmcEcTimingOverride {
  bool configured;
  bool sourceConfigured;
  bool updateDivisorConfigured;
  ecmcEcTimingSource source;
  int32_t cycleOffset;
  int32_t eventOffsetNs;
  uint32_t uncertaintyNs;
  uint32_t updateDivisor;

  ecmcEcTimingOverride() :
    configured(false),
    sourceConfigured(false),
    updateDivisorConfigured(false),
    source(ecmcEcTimingSource::CYCLE_ONLY),
    cycleOffset(0),
    eventOffsetNs(0),
    uncertaintyNs(0),
    updateDivisor(1) {}
};

struct ecmcEcTimestampConfig {
  bool configured;
  uint8_t bits;
  int32_t correctionNs;

  ecmcEcTimestampConfig() : configured(false), bits(0), correctionNs(0) {}
};

struct ecmcEcCycleTiming {
  uint64_t sequence;
  uint64_t receiveTimeNs;
  uint64_t applicationTimeNs;
  uint64_t nominalPeriodNs;
  bool dcConfigured;
  bool domainsValid;

  ecmcEcCycleTiming() :
    sequence(0),
    receiveTimeNs(0),
    applicationTimeNs(0),
    nominalPeriodNs(0),
    dcConfigured(false),
    domainsValid(false) {}
};

struct ecmcEcDelayEstimate {
  bool valid;
  int64_t delayNs;
  uint64_t uncertaintyNs;

  ecmcEcDelayEstimate() : valid(false), delayNs(0), uncertaintyNs(0) {}
};

enum class ecmcEcDelaySource : uint8_t {
  INVALID = 0,
  CYCLE,
  DC_SYNC,
  TIMESTAMP
};

enum class ecmcEcDelayStatus : uint8_t {
  OK = 0,
  INPUT_INVALID,
  OUTPUT_INVALID,
  CYCLE_TIME_UNKNOWN,
  OFFSET_UNKNOWN,
  TIMING_DOMAIN_MISMATCH,
  TIMESTAMP_REQUIRED,
  SCHEDULE_UNKNOWN
};

/* Result of relating an encoder/input event to a drive/output application
 * event. A positive delay means that the output event occurs after the input
 * event. No value is guessed when the endpoints cannot be put on a common
 * time base. */
struct ecmcEcTimingPath {
  bool valid;
  ecmcEcDelaySource source;
  ecmcEcDelayStatus status;
  int64_t delayNs;
  uint64_t uncertaintyNs;

  ecmcEcTimingPath() :
    valid(false),
    source(ecmcEcDelaySource::INVALID),
    status(ecmcEcDelayStatus::TIMING_DOMAIN_MISMATCH),
    delayNs(0),
    uncertaintyNs(0) {}
};

struct ecmcEcDcConfig {
  bool configured;
  uint16_t assignActivate;
  uint32_t sync0CycleNs;
  int32_t sync0ShiftNs;
  uint32_t sync1OffsetNs;
  int32_t sync1ShiftNs;

  ecmcEcDcConfig() :
    configured(false),
    assignActivate(0),
    sync0CycleNs(0),
    sync0ShiftNs(0),
    sync1OffsetNs(0),
    sync1ShiftNs(0) {}
};

/* ESC cyclic-unit schedule read once after activation. These are raw
 * register values and are kept separate from the configured DC values. */
struct ecmcEcDcSchedule {
  bool discoveryAttempted;
  bool discoveryComplete;
  uint64_t startTimeNs;   // 0x0990
  uint32_t sync0CycleNs;  // 0x09A0
  uint32_t sync1CycleNs;  // 0x09A4
  int requestError;

  ecmcEcDcSchedule() :
    discoveryAttempted(false),
    discoveryComplete(false),
    startTimeNs(0),
    sync0CycleNs(0),
    sync1CycleNs(0),
    requestError(0) {}
};

/* Resolve an endpoint against the most recent occurrence of its selected
 * SYNC event at or before applicationTimeNs. cycleOffset is therefore an
 * explicit PDO generation age (negative) or output lead (positive). 0x0990
 * already includes the configured SYNC0 shift. No absolute event is guessed
 * when the actual ESC schedule was not readable. */
inline bool ecmcEcResolveScheduledEventNs(
  uint64_t applicationTimeNs,
  const ecmcEcEndpointTiming& endpoint,
  const ecmcEcDcConfig& dc,
  const ecmcEcDcSchedule& schedule,
  uint32_t periodNs,
  uint64_t *eventTimeNs) {
  if (!eventTimeNs || !applicationTimeNs ||
      applicationTimeNs >
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
      !periodNs || !endpoint.valid ||
      !endpoint.cycleOffsetKnown || !endpoint.eventOffsetKnown ||
      !schedule.discoveryComplete || !schedule.startTimeNs ||
      !schedule.sync0CycleNs ||
      (endpoint.source != ecmcEcTimingSource::SYNC0_DERIVED &&
       endpoint.source != ecmcEcTimingSource::SYNC1_DERIVED)) {
    return false;
  }

  if (schedule.startTimeNs >
      static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    return false;
  }
  int64_t firstEventNs = static_cast<int64_t>(schedule.startTimeNs);
  if (endpoint.source == ecmcEcTimingSource::SYNC1_DERIVED) {
    firstEventNs += dc.sync1OffsetNs;
  }
  firstEventNs += endpoint.eventOffsetNs;

  const int64_t anchor = static_cast<int64_t>(applicationTimeNs);
  const int64_t period = static_cast<int64_t>(periodNs);
  int64_t latestEventNs = firstEventNs;
  if (latestEventNs <= anchor) {
    latestEventNs += ((anchor - latestEventNs) / period) * period;
  } else {
    latestEventNs -= ((latestEventNs - anchor + period - 1) / period) * period;
  }
  const int64_t resolvedEventNs = latestEventNs +
    static_cast<int64_t>(endpoint.cycleOffset) * period;
  if (resolvedEventNs < 0) {
    return false;
  }
  *eventTimeNs = static_cast<uint64_t>(resolvedEventNs);
  return true;
}

struct ecmcEcSmTimingValue {
  bool available;
  uint32_t value;
  uint32_t abortCode;
  int requestError;
  uint8_t byteSize;

  ecmcEcSmTimingValue() :
    available(false),
    value(0),
    abortCode(0),
    requestError(0),
    byteSize(0) {}
};

/* Raw CoE SyncManager timing values. No assumption is made that the
 * individual delays are additive; interpretation is terminal-dependent. */
struct ecmcEcSmTiming {
  bool discoveryAttempted;
  bool discoveryComplete;
  uint16_t objectIndex;
  ecmcEcSmTimingValue syncType;             // :01
  ecmcEcSmTimingValue cycleTimeNs;           // :02
  ecmcEcSmTimingValue shiftTimeNs;           // :03
  ecmcEcSmTimingValue syncTypesSupported;    // :04
  ecmcEcSmTimingValue minimumCycleTimeNs;    // :05
  ecmcEcSmTimingValue calculationCopyTimeNs; // :06
  ecmcEcSmTimingValue minimumDelayTimeNs;    // :07
  ecmcEcSmTimingValue command;               // :08
  ecmcEcSmTimingValue maximumDelayTimeNs;    // :09
  ecmcEcSmTimingValue synchronizationError;  // :20

  explicit ecmcEcSmTiming(uint16_t index = 0) :
    discoveryAttempted(false),
    discoveryComplete(false),
    objectIndex(index) {}
};

/* Resolve a cycle-only input/output event against the exact receive or
 * application time captured by ecmc for that cycle. */
inline bool ecmcEcResolveCycleEventNs(const ecmcEcCycleTiming& cycle,
                                     const ecmcEcEndpointTiming& endpoint,
                                     uint64_t *eventTimeNs) {
  if (!eventTimeNs || !endpoint.valid || !endpoint.cycleOffsetKnown ||
      !endpoint.eventOffsetKnown || cycle.nominalPeriodNs == 0 ||
      endpoint.source != ecmcEcTimingSource::CYCLE_ONLY) {
    return false;
  }
  const uint64_t anchor = endpoint.reference ==
    ecmcEcTimingReference::APPLICATION ? cycle.applicationTimeNs :
                                         cycle.receiveTimeNs;
  const int64_t relative =
    static_cast<int64_t>(endpoint.cycleOffset) * cycle.nominalPeriodNs +
    endpoint.eventOffsetNs;
  if (relative < 0) {
    const uint64_t magnitude = static_cast<uint64_t>(-relative);
    if (magnitude > anchor) {
      return false;
    }
    *eventTimeNs = anchor - magnitude;
    return true;
  }
  if (static_cast<uint64_t>(relative) >
      std::numeric_limits<uint64_t>::max() - anchor) {
    return false;
  }
  *eventTimeNs = anchor + static_cast<uint64_t>(relative);
  return true;
}

inline bool ecmcEcSignedTimeDifferenceNs(uint64_t laterNs,
                                        uint64_t earlierNs,
                                        int64_t *differenceNs) {
  if (!differenceNs) {
    return false;
  }
  if (laterNs >= earlierNs) {
    const uint64_t difference = laterNs - earlierNs;
    if (difference > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      return false;
    }
    *differenceNs = static_cast<int64_t>(difference);
    return true;
  }
  const uint64_t difference = earlierNs - laterNs;
  if (difference > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    return false;
  }
  *differenceNs = -static_cast<int64_t>(difference);
  return true;
}

/* Extend a 32-bit DC nanosecond timestamp around a nearby 64-bit DC time.
 * The event must be within half of the 32-bit wrap interval. */
inline uint64_t ecmcEcExtendDcTimestamp32(uint32_t timestamp,
                                         uint64_t nearbyDcTimeNs) {
  const uint32_t nearby32 = static_cast<uint32_t>(nearbyDcTimeNs);
  const int32_t age = static_cast<int32_t>(nearby32 - timestamp);
  return nearbyDcTimeNs - static_cast<int64_t>(age);
}

/* Calculate the interval between endpoint events associated with one exact
 * application time. No phase is rounded or inferred from ecmc execution time. */
inline ecmcEcDelayEstimate ecmcEcCalculateEndpointDelayNs(
  uint64_t applicationTimeNs,
  const ecmcEcEndpointTiming& earlier,
  const ecmcEcDcConfig& earlierDc,
  const ecmcEcDcSchedule& earlierSchedule,
  const ecmcEcEndpointTiming& later,
  const ecmcEcDcConfig& laterDc,
  const ecmcEcDcSchedule& laterSchedule,
  uint32_t cycleTimeNs) {
  ecmcEcDelayEstimate result;
  if (!earlier.valid || !later.valid || !earlier.cycleOffsetKnown ||
      !later.cycleOffsetKnown || !earlier.eventOffsetKnown ||
      !later.eventOffsetKnown || cycleTimeNs == 0 ||
      earlier.source == ecmcEcTimingSource::CYCLE_ONLY ||
      later.source == ecmcEcTimingSource::CYCLE_ONLY ||
      earlier.source == ecmcEcTimingSource::HARDWARE_TIMESTAMP ||
      later.source == ecmcEcTimingSource::HARDWARE_TIMESTAMP) {
    return result;
  }

  uint64_t earlierEventNs = 0;
  uint64_t laterEventNs = 0;
  if (!ecmcEcResolveScheduledEventNs(applicationTimeNs, earlier, earlierDc,
                                     earlierSchedule, cycleTimeNs,
                                     &earlierEventNs) ||
      !ecmcEcResolveScheduledEventNs(applicationTimeNs, later, laterDc,
                                     laterSchedule, cycleTimeNs,
                                     &laterEventNs) ||
      !ecmcEcSignedTimeDifferenceNs(laterEventNs, earlierEventNs,
                                    &result.delayNs)) {
    return result;
  }
  result.uncertaintyNs =
    static_cast<uint64_t>(earlier.uncertaintyNs) + later.uncertaintyNs;
  result.valid = true;
  return result;
}

inline ecmcEcDelayEstimate ecmcEcCalculateCycleDelayNs(
  const ecmcEcCycleTiming& cycle,
  const ecmcEcEndpointTiming& input,
  const ecmcEcEndpointTiming& output) {
  ecmcEcDelayEstimate result;
  if (!cycle.domainsValid || input.source != ecmcEcTimingSource::CYCLE_ONLY ||
      output.source != ecmcEcTimingSource::CYCLE_ONLY) {
    return result;
  }
  uint64_t inputTime = 0;
  uint64_t outputTime = 0;
  if (!ecmcEcResolveCycleEventNs(cycle, input, &inputTime) ||
      !ecmcEcResolveCycleEventNs(cycle, output, &outputTime) ||
      !ecmcEcSignedTimeDifferenceNs(outputTime, inputTime, &result.delayNs)) {
    return result;
  }
  result.uncertaintyNs =
    static_cast<uint64_t>(input.uncertaintyNs) + output.uncertaintyNs;
  result.valid = true;
  return result;
}

/* Generic static/cyclic timing-path calculation used by encoder-to-drive and
 * input-to-output consumers. Hardware timestamp paths use the overload below
 * because their event times are dynamic rather than startup constants. */
inline ecmcEcTimingPath ecmcEcCalculateTimingPathNs(
  const ecmcEcCycleTiming& cycle,
  const ecmcEcEndpointTiming& input,
  const ecmcEcDcConfig& inputDc,
  const ecmcEcDcSchedule& inputSchedule,
  const ecmcEcEndpointTiming& output,
  const ecmcEcDcConfig& outputDc,
  const ecmcEcDcSchedule& outputSchedule) {
  ecmcEcTimingPath path;
  if (!input.valid) {
    path.status = ecmcEcDelayStatus::INPUT_INVALID;
    return path;
  }
  if (!output.valid) {
    path.status = ecmcEcDelayStatus::OUTPUT_INVALID;
    return path;
  }
  if (input.source == ecmcEcTimingSource::HARDWARE_TIMESTAMP ||
      output.source == ecmcEcTimingSource::HARDWARE_TIMESTAMP) {
    path.status = ecmcEcDelayStatus::TIMESTAMP_REQUIRED;
    return path;
  }
  if (!input.cycleOffsetKnown || !output.cycleOffsetKnown ||
      !input.eventOffsetKnown || !output.eventOffsetKnown) {
    path.status = ecmcEcDelayStatus::OFFSET_UNKNOWN;
    return path;
  }

  if (input.source == ecmcEcTimingSource::CYCLE_ONLY &&
      output.source == ecmcEcTimingSource::CYCLE_ONLY) {
    const ecmcEcDelayEstimate estimate =
      ecmcEcCalculateCycleDelayNs(cycle, input, output);
    if (!estimate.valid) {
      path.status = cycle.nominalPeriodNs == 0 ?
        ecmcEcDelayStatus::CYCLE_TIME_UNKNOWN :
        ecmcEcDelayStatus::TIMING_DOMAIN_MISMATCH;
      return path;
    }
    path.valid = true;
    path.source = ecmcEcDelaySource::CYCLE;
    path.status = ecmcEcDelayStatus::OK;
    path.delayNs = estimate.delayNs;
    path.uncertaintyNs = estimate.uncertaintyNs;
    return path;
  }

  const bool inputSync =
    input.source == ecmcEcTimingSource::SYNC0_DERIVED ||
    input.source == ecmcEcTimingSource::SYNC1_DERIVED;
  const bool outputSync =
    output.source == ecmcEcTimingSource::SYNC0_DERIVED ||
    output.source == ecmcEcTimingSource::SYNC1_DERIVED;
  if (!inputSync || !outputSync) {
    path.status = ecmcEcDelayStatus::TIMING_DOMAIN_MISMATCH;
    return path;
  }

  if (!inputSchedule.discoveryComplete || !inputSchedule.startTimeNs ||
      !inputSchedule.sync0CycleNs || !outputSchedule.discoveryComplete ||
      !outputSchedule.startTimeNs || !outputSchedule.sync0CycleNs) {
    path.status = ecmcEcDelayStatus::SCHEDULE_UNKNOWN;
    return path;
  }

  const uint32_t inputCycleNs = inputSchedule.sync0CycleNs;
  const uint32_t outputCycleNs = outputSchedule.sync0CycleNs;
  if (inputCycleNs != outputCycleNs) {
    path.status = ecmcEcDelayStatus::TIMING_DOMAIN_MISMATCH;
    return path;
  }

  const ecmcEcDelayEstimate estimate = ecmcEcCalculateEndpointDelayNs(
    cycle.applicationTimeNs, input, inputDc, inputSchedule, output, outputDc,
    outputSchedule, inputCycleNs);
  if (!estimate.valid) {
    path.status = ecmcEcDelayStatus::TIMING_DOMAIN_MISMATCH;
    return path;
  }
  path.valid = true;
  path.source = ecmcEcDelaySource::DC_SYNC;
  path.status = ecmcEcDelayStatus::OK;
  path.delayNs = estimate.delayNs;
  path.uncertaintyNs = estimate.uncertaintyNs;
  return path;
}

/* Dynamic timestamp path. Both timestamps must already be extended/corrected
 * to the same 64-bit DC time base by the endpoint owners. */
inline ecmcEcTimingPath ecmcEcCalculateTimestampPathNs(
  uint64_t inputEventTimeNs,
  uint64_t outputEventTimeNs,
  uint64_t inputUncertaintyNs,
  uint64_t outputUncertaintyNs) {
  ecmcEcTimingPath path;
  if (!ecmcEcSignedTimeDifferenceNs(outputEventTimeNs, inputEventTimeNs,
                                    &path.delayNs)) {
    return path;
  }
  path.valid = true;
  path.source = ecmcEcDelaySource::TIMESTAMP;
  path.status = ecmcEcDelayStatus::OK;
  path.uncertaintyNs = inputUncertaintyNs >
      std::numeric_limits<uint64_t>::max() - outputUncertaintyNs ?
    std::numeric_limits<uint64_t>::max() :
    inputUncertaintyNs + outputUncertaintyNs;
  return path;
}

#endif  /* ECMCECTIMING_H_ */
