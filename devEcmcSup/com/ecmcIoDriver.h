#ifndef ECMC_IO_DRIVER_H_
#define ECMC_IO_DRIVER_H_

#include <time.h>

// Internal cyclic I/O contract. One caller: the existing cyclic RT thread.
// Providers expose stable signal descriptors separately through ecmcDataAccess.
// No signal lookup, allocation, transport calls or worker threads in consumers.
class ecmcIoDriver {
public:
  virtual ~ecmcIoDriver() = default;

  // The caller samples transport enablement once per cycle, separately from
  // communication health. An unhealthy active transport must still process I/O.
  virtual bool isActive() const = 0;

  // inputTime is CLOCK_MONOTONIC, sampled before the input phase. timeOffset
  // is CLOCK_REALTIME minus CLOCK_MONOTONIC, sampled at activation. Keep the
  // existing normalized timespec representation and clock conversions.
  virtual void readInputs(timespec inputTime, timespec timeOffset) = 0;
  virtual int updateIoStatus() = 0;
  virtual void writeOutputs(timespec timeOffset) = 0;
  // Timing-only update for the existing masterless path, without bus I/O.
  virtual void setTimeOffset(timespec timeOffset) = 0;

  // Startup and slow work keep their existing positions/divider in the caller.
  // Timing readiness is separate from communication health.
  virtual bool timingReady() const = 0;
  virtual void resetStartupErrors() = 0;
  virtual void updateDiagnostics() = 0;
  virtual void slowExecute() = 0;
};

#endif
