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

struct ecmcEcEndpointTiming {
  bool valid;
  ecmcEcTimingSource source;
  ecmcEcTimingReference reference;
  int32_t cycleOffset;
  int32_t eventOffsetNs;
  uint32_t uncertaintyNs;
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
 * configuration. The correction is relative to the SYNC event selected by
 * the terminal's 0x1C32/0x1C33 synchronization type. */
struct ecmcEcTimingOverride {
  bool configured;
  int32_t cycleOffset;
  int32_t eventOffsetNs;
  uint32_t uncertaintyNs;

  ecmcEcTimingOverride() :
    configured(false),
    cycleOffset(0),
    eventOffsetNs(0),
    uncertaintyNs(0) {}
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

/*
 * Resolve a physical event against the SYNC0 time of the reference cycle.
 * sync1OffsetNs is the configured time between SYNC0 and SYNC1 (the
 * sync1_cycle argument of ecrt_slave_config_dc), not sync1_shift.
 *
 * Hardware timestamps and cycle-only timing cannot be resolved by this
 * schedule helper. The caller must use the timestamp directly or retain a
 * bounded cycle-time estimate, respectively.
 */
inline bool ecmcEcResolveSyncEventNs(uint64_t referenceSync0Ns,
                                    uint32_t sync0CycleNs,
                                    uint32_t sync1OffsetNs,
                                    const ecmcEcEndpointTiming& endpoint,
                                    uint64_t *eventTimeNs) {
  if (!eventTimeNs || !endpoint.valid || !endpoint.cycleOffsetKnown ||
      !endpoint.eventOffsetKnown || sync0CycleNs == 0 ||
      (endpoint.source != ecmcEcTimingSource::SYNC0_DERIVED &&
       endpoint.source != ecmcEcTimingSource::SYNC1_DERIVED)) {
    return false;
  }

  int64_t relativeNs = static_cast<int64_t>(endpoint.cycleOffset) *
                       static_cast<int64_t>(sync0CycleNs);
  if (endpoint.source == ecmcEcTimingSource::SYNC1_DERIVED) {
    relativeNs += static_cast<int64_t>(sync1OffsetNs);
  }
  relativeNs += static_cast<int64_t>(endpoint.eventOffsetNs);

  if (relativeNs < 0) {
    const uint64_t magnitude = static_cast<uint64_t>(-relativeNs);
    if (magnitude > referenceSync0Ns) {
      return false;
    }
    *eventTimeNs = referenceSync0Ns - magnitude;
    return true;
  }

  const uint64_t positive = static_cast<uint64_t>(relativeNs);
  if (positive > std::numeric_limits<uint64_t>::max() - referenceSync0Ns) {
    return false;
  }
  *eventTimeNs = referenceSync0Ns + positive;
  return true;
}

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

/* Calculate the signed interval between two endpoint events expressed against
 * the same DC cycle. No cycle association is guessed: both endpoints must have
 * a known cycle offset, either discovered by an authoritative mechanism or
 * supplied explicitly by the hardware configuration. */
inline ecmcEcDelayEstimate ecmcEcCalculateEndpointDelayNs(
  const ecmcEcEndpointTiming& earlier,
  uint32_t earlierSync1OffsetNs,
  const ecmcEcEndpointTiming& later,
  uint32_t laterSync1OffsetNs,
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
  result.delayNs =
    (static_cast<int64_t>(later.cycleOffset) -
     static_cast<int64_t>(earlier.cycleOffset)) * cycleTimeNs +
    static_cast<int64_t>(later.eventOffsetNs) -
    static_cast<int64_t>(earlier.eventOffsetNs);
  if (earlier.source == ecmcEcTimingSource::SYNC1_DERIVED) {
    result.delayNs -= earlierSync1OffsetNs;
  }
  if (later.source == ecmcEcTimingSource::SYNC1_DERIVED) {
    result.delayNs += laterSync1OffsetNs;
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

#endif  /* ECMCECTIMING_H_ */
