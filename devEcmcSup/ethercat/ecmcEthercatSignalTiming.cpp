#include "ecmcEthercatSignalTiming.h"
#include "ecmcEc.h"
#include "ecmcEcSlave.h"

namespace {
class EthercatSignalTiming final : public ecmcSignalTiming {
public:
  EthercatSignalTiming(ecmcEc &master, ecmcEcSlave &slave)
    : master_(master), slave_(slave) {}
  bool read(ecmcSignalTime &time) const override {
    const auto &cycle = master_.getCycleTiming();
    const auto &timing = slave_.getInputTiming();
    time = {};
    time.controllerTimeNs = master_.getLastSendTimeNs();
    time.uncertaintyNs = timing.uncertaintyNs;
    if (timing.source == ecmcEcTimingSource::SYNC0_DERIVED ||
        timing.source == ecmcEcTimingSource::SYNC1_DERIVED) {
      time.quality = ecmcSignalTimeQuality::SCHEDULE;
      return ecmcEcResolveScheduledEventNs(cycle.applicationTimeNs, timing,
                 slave_.getDcConfig(), slave_.getDcSchedule(),
                 cycle.nominalPeriodNs, &time.sampleTimeNs);
    }
    if (timing.source == ecmcEcTimingSource::CYCLE_ONLY) {
      time.quality = ecmcSignalTimeQuality::CYCLE_BOUNDED;
      return ecmcEcResolveCycleEventNs(cycle, timing, &time.sampleTimeNs);
    }
    return false;
  }
private:
  ecmcEc &master_;
  ecmcEcSlave &slave_;
};
}

void ecmcBindEthercatTiming(ecmcEc &master, ecmcEcSlave &slave,
                           ecmcDataAccess &access) {
  if (!access.timing)
    access.timing = std::make_shared<EthercatSignalTiming>(master, slave);
}
