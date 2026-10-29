#ifndef ECMC_ETHERCAT_DRIVER_H_
#define ECMC_ETHERCAT_DRIVER_H_

#include "ecmcIoDriver.h"
#include "ecmcEc.h"

// Non-owning adapter. EtherCAT configuration, activation, domain filtering,
// process-image buffers and shutdown remain owned by the existing objects.
// The concrete final type is used in production so the forwarding methods can
// inline/devirtualize without adding indirect calls to the EtherCAT cycle.
class ecmcEthercatDriver final : public ecmcIoDriver {
public:
  explicit ecmcEthercatDriver(ecmcEc &master) : master_(master) {}

  bool isActive() const override { return master_.getInitDone(); }
  void readInputs(timespec inputTime, timespec timeOffset) override {
    master_.receive(inputTime, timeOffset);
  }
  int updateIoStatus() override {
    master_.checkDomainsState();
    return master_.statusOK();
  }
  void writeOutputs(timespec timeOffset) override { master_.send(timeOffset); }
  void setTimeOffset(timespec timeOffset) override { master_.setTimeOffest(timeOffset); }
  bool timingReady() const override { return master_.timingStartupReady(); }
  void resetStartupErrors() override { master_.resetStartupCommunicationErrors(); }
  void updateDiagnostics() override { master_.checkState(); }
  void slowExecute() override { master_.slowExecute(); }

private:
  ecmcEc &master_;
};

#endif
