#ifndef ECMC_SIGNAL_TIMING_H_
#define ECMC_SIGNAL_TIMING_H_

#include <cstdint>

// Optional provider capability. Times use the provider's event-clock epoch;
// sample and controller times must belong to the same clock. Reads perform no
// lookup or transport operations. Provider state outlives its valid descriptor.
enum class ecmcSignalTimeQuality : uint8_t { INVALID, CYCLE_BOUNDED, SCHEDULE };
struct ecmcSignalTime {
  uint64_t sampleTimeNs = 0;
  uint64_t controllerTimeNs = 0;
  uint32_t uncertaintyNs = 0;
  ecmcSignalTimeQuality quality = ecmcSignalTimeQuality::INVALID;
};
class ecmcSignalTiming {
public:
  virtual ~ecmcSignalTiming() = default;
  virtual bool read(ecmcSignalTime &time) const = 0;
};
#endif
