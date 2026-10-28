// Direct monitor samples isolate timing from trajectory/plant behavior.
// Existing virtual-axis suites cover the surrounding motion integration.
// Basis labels and unresolved decisions: ../EXPECTATIONS.md, Monitor (M1-M4).
#include "ecmcMonitor.h"
#include "ecmcErrorsList.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

static std::string context;
static size_t scenarios = 0, samples = 0;
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
  context + ": line " + std::to_string(__LINE__) + ": " #condition); } while (0)

struct Bench {
  ecmcAxisData data;
  std::array<ecmcEncoder *, ECMC_MAX_ENCODERS> encoders{};
  std::unique_ptr<ecmcMonitor> monitor;
  Bench() {
    ++scenarios;
    data.status_.sampleTime = 0.001;
    data.status_.axisId = 1;
    data.status_.command = ECMC_CMD_MOVEABS;
    data.control_.command = ECMC_CMD_MOVEABS;
    data.status_.statusWord_.instartup = false;
    monitor = std::make_unique<ecmcMonitor>(data, encoders.data());
    monitor->setLimitSwitchBwdPLCOverride(true);
    monitor->setLimitSwitchFwdPLCOverride(true);
    monitor->setLimitSwitchBwdPLCOverrideValue(true);
    monitor->setLimitSwitchFwdPLCOverrideValue(true);
    monitor->setEnableAtTargetMon(false);
    monitor->setEnableLagMon(false);
    monitor->setEnableStallMon(false);
    monitor->setEnableCheckEncsDiff(false);
    monitor->setEnable(true);
    CHECK(monitor->validate() == 0);
  }
  void tick(bool enabled = true, bool request = true) {
    ++samples;
    data.control_.controlWord_.enableCmd = request;
    data.status_.statusWord_.enable = request;
    data.status_.statusWord_.enabled = enabled;
    monitor->readEntries();
    monitor->execute();
    data.statusOld_ = data.status_;
  }
  void settle() { tick(); tick(); }
};

static void followingError() {
  // DOC: ecmcMotion.h::setAxisMonPosLagTol/Time describes excessive error.
  // COMPAT/REVIEW M1: exact N+1 trajectory / 2N drive sample counts are
  // implementation conventions, not an independently confirmed timing spec.
  for (int delay : {0, 1, 2, 5}) for (double sign : {-1., 1.}) {
    context = "lag threshold/time delay=" + std::to_string(delay);
    Bench b;
    CHECK(b.monitor->setPosLagTol(0.5) == 0);
    CHECK(b.monitor->setPosLagTime(delay) == 0);
    b.monitor->setEnableLagMon(true);
    b.settle();
    // DOC: the API says "exceeds" the tolerance; equality does not exceed it.
    b.data.status_.cntrlError = sign * 0.5;
    for (int i = 0; i < 8; ++i) {
      b.tick();
      CHECK(!b.data.interlocks_.lagTrajInterlock && !b.data.interlocks_.lagDriveInterlock);
    }
    b.data.status_.cntrlError = sign * 0.6;
    for (int i = 1; i <= 2 * delay + 5; ++i) {
      b.tick();
      CHECK(b.data.interlocks_.lagTrajInterlock == (i > delay));
      CHECK(b.data.interlocks_.lagDriveInterlock == (i >= 2 * delay));
    }
    CHECK(b.monitor->getErrorID() == ERROR_MON_MAX_POSITION_LAG_EXCEEDED);
    b.data.status_.cntrlError = 0;
    b.tick();
    CHECK(!b.data.interlocks_.lagTrajInterlock && !b.data.interlocks_.lagDriveInterlock);
    CHECK(b.monitor->getErrorID() == ERROR_MON_MAX_POSITION_LAG_EXCEEDED);
    b.monitor->errorReset(); b.tick();
    CHECK(b.monitor->getErrorID() == 0 && !b.data.interlocks_.axisErrorStateInterlock);
  }
  for (int interruption = 0; interruption < 4; ++interruption) {
    // COMPAT: countdown rearming, enabled-history guard and error-reset policy.
    context = "lag interrupted counter mode=" + std::to_string(interruption);
    Bench b;
    b.monitor->setPosLagTol(0.5); b.monitor->setPosLagTime(3);
    b.monitor->setEnableLagMon(true); b.settle();
    b.data.status_.cntrlError = 1;
    b.tick(); b.tick(); b.tick();
    CHECK(!b.data.interlocks_.lagTrajInterlock);
    if (interruption == 0) { b.data.status_.cntrlError = 0; b.tick(); }
    if (interruption == 1) { b.tick(false); b.tick(); }
    if (interruption == 2) b.tick(true, false);
    if (interruption == 3) {
      b.monitor->setEnableLagMon(false); b.tick(); b.monitor->setEnableLagMon(true);
    }
    b.data.status_.cntrlError = 1;
    for (int i = 0; i < 3; ++i) { b.tick(); CHECK(!b.data.interlocks_.lagTrajInterlock); }
    b.tick(); CHECK(b.data.interlocks_.lagTrajInterlock);
    b.monitor->errorReset();
    for (int i = 0; i < 3; ++i) { b.tick(); CHECK(!b.data.interlocks_.lagTrajInterlock); }
    b.tick(); CHECK(b.data.interlocks_.lagTrajInterlock);
  }
}

static void maximumVelocity() {
  // DOC: ecmcMotion.h::setAxisMonMaxVel/EnableMaxVel: either velocity may
  // exceed the limit, and monitoring must be enabled. Sign symmetry is INFERRED.
  for (int trajDelay : {0, 1, 3}) for (int driveDelay : {0, 1, 3})
    for (bool actual : {false, true}) for (double sign : {-1., 1.}) {
      context = "max velocity trajectory=" + std::to_string(trajDelay) +
                " drive=" + std::to_string(driveDelay);
      Bench b;
      b.monitor->setMaxVel(2);
      b.monitor->setMaxVelTrajTime(trajDelay);
      b.monitor->setMaxVelDriveTime(driveDelay);
      b.monitor->setEnableMaxVelMon(true);
      b.settle();
      auto &velocity = actual ? b.data.status_.currentVelocityActual :
                                b.data.status_.currentVelocitySetpoint;
      velocity = sign * 2;
      for (int i = 0; i < 5; ++i) {
        b.tick();
        CHECK(!b.data.interlocks_.maxVelocityTrajInterlock);
        CHECK(!b.data.interlocks_.maxVelocityDriveInterlock);
      }
      velocity = sign * 2.1;
      // COMPAT/REVIEW M2: exact boundary and cumulative delay convention.
      const int trajTrip = std::max(1, trajDelay);
      const int driveTrip = trajTrip + std::max(1, driveDelay) - 1;
      for (int i = 1; i <= driveTrip + 8; ++i) {
        b.tick();
        CHECK(b.data.interlocks_.maxVelocityTrajInterlock == (i >= trajTrip));
        // INFERRED: once tripped, the drive flag must not periodically flicker
        // while the trajectory trip remains latched. Clearing policy is M2.
        CHECK(b.data.interlocks_.maxVelocityDriveInterlock == (i >= driveTrip));
      }
      CHECK(b.monitor->getErrorID() == ERROR_MON_MAX_VELOCITY_EXCEEDED);
      // COMPAT/REVIEW M2: recovery of speed alone does not clear this latch.
      velocity = 0;
      for (int i = 0; i < 8; ++i) {
        b.tick(); CHECK(b.data.interlocks_.maxVelocityDriveInterlock);
      }
      b.monitor->errorReset(); b.tick();
      CHECK(b.monitor->getErrorID() == 0);
      CHECK(!b.data.interlocks_.maxVelocityTrajInterlock);
    }
  for (int mode = 0; mode < 4; ++mode) {
    // DOC: disabled monitor does not trip. COMPAT/REVIEW M2: the tested
    // disable/feedback/reset clearing rules are separate policy expectations.
    context = "overspeed disable/reset mode=" + std::to_string(mode);
    Bench b;
    b.monitor->setMaxVel(2); b.monitor->setMaxVelTrajTime(0);
    b.monitor->setMaxVelDriveTime(0);
    // A disabled monitor must never trip, including zero configured delays.
    b.data.status_.currentVelocityActual = 3;
    b.settle();
    CHECK(!b.data.interlocks_.maxVelocityTrajInterlock);
    b.monitor->setEnableMaxVelMon(true);
    b.tick(); CHECK(b.data.interlocks_.maxVelocityDriveInterlock);
    if (mode == 0) b.monitor->setEnableMaxVelMon(false);
    if (mode == 1) { b.tick(false); }
    if (mode == 2) { b.tick(true, false); }
    if (mode == 3) {
      b.data.status_.currentVelocityActual = 0;
      b.monitor->errorReset();
    }
    if (mode == 0 || mode == 3) b.tick();
    CHECK(!b.data.interlocks_.maxVelocityTrajInterlock);
    CHECK(!b.data.interlocks_.maxVelocityDriveInterlock);
  }
}

static void targetDetection() {
  // DOC: ecmcMotion.h::setAxisMonAtTargetTol/Time requires sustained proximity.
  // COMPAT/REVIEW M3: strict < tolerance, N+1 samples and busy suppression.
  for (int delay : {0, 1, 4}) {
    context = "at-target consecutive samples delay=" + std::to_string(delay);
    Bench b;
    b.monitor->setAtTargetTol(0.5); b.monitor->setAtTargetTime(delay);
    b.monitor->setEnableAtTargetMon(true);
    b.data.status_.cntrlError = 0.5;
    for (int i = 0; i < delay + 3; ++i) { b.tick(); CHECK(!b.monitor->getAtTarget()); }
    b.data.status_.cntrlError = 0.4;
    for (int i = 1; i <= delay + 2; ++i) {
      b.tick(); CHECK(b.monitor->getAtTarget() == (i > delay));
    }
    b.data.status_.cntrlError = 0.5; b.tick(); CHECK(!b.monitor->getAtTarget());
    b.data.status_.cntrlError = 0.4;
    for (int i = 0; i <= delay; ++i) b.tick();
    CHECK(b.monitor->getAtTarget());
    b.data.status_.statusWord_.localBusy = true;
    b.tick(); CHECK(!b.monitor->getAtTarget());
  }
}

static void directionalLimits() {
  // DOC: ecmcMotion.h::setAxisMonLatchLimit describes bounce retention.
  // COMPAT/REVIEW M4: clearing on moving-away feedback is not proof that a
  // new motion command may bypass the stop required by the public wording.
  for (bool fwd : {false, true}) for (bool latch : {false, true}) {
    context = "limit direction/latch fwd=" + std::to_string(fwd) + " latch=" + std::to_string(latch);
    Bench b; b.monitor->setLatchAtLimit(latch); b.settle();
    b.data.status_.statusWord_.moving = true;
    b.data.status_.currentVelocityActual = fwd ? 1 : -1;
    b.monitor->setLimitSwitchFwdPLCOverrideValue(!fwd);
    b.monitor->setLimitSwitchBwdPLCOverrideValue(fwd);
    b.tick();
    CHECK(b.data.interlocks_.fwdLimitInterlock == fwd);
    CHECK(b.data.interlocks_.bwdLimitInterlock == !fwd);
    CHECK(b.data.interlocks_.trajSummaryInterlockFWD == fwd);
    CHECK(b.data.interlocks_.trajSummaryInterlockBWD == !fwd);
    b.monitor->setLimitSwitchFwdPLCOverrideValue(true);
    b.monitor->setLimitSwitchBwdPLCOverrideValue(true);
    b.tick();
    CHECK((fwd ? b.data.interlocks_.fwdLimitInterlock : b.data.interlocks_.bwdLimitInterlock) == latch);
    // COMPAT/REVIEW M4: moving remains true here; no command is submitted.
    b.data.status_.currentVelocityActual *= -1;
    b.tick();
    CHECK(!b.data.interlocks_.fwdLimitInterlock && !b.data.interlocks_.bwdLimitInterlock);
    b.monitor->errorReset(); b.tick(); CHECK(b.monitor->getErrorID() == 0);
  }
  for (bool stopAtAny : {false, true}) {
    // COMPAT: exact both-limits fault and summary mapping.
    context = "both limits/stop at any limit";
    Bench b; b.monitor->setStopAtAnyLimit(stopAtAny); b.settle();
    b.monitor->setLimitSwitchFwdPLCOverrideValue(false);
    b.monitor->setLimitSwitchBwdPLCOverrideValue(stopAtAny);
    b.tick();
    CHECK(b.data.interlocks_.bothLimitsLowInterlock);
    CHECK(b.monitor->getErrorID() == ERROR_MON_BOTH_LIMIT_INTERLOCK);
    CHECK(b.data.interlocks_.driveSummaryInterlock);
    b.monitor->setLimitSwitchFwdPLCOverrideValue(true);
    b.monitor->setLimitSwitchBwdPLCOverrideValue(true);
    b.monitor->errorReset(); b.tick();
    CHECK(!b.data.interlocks_.driveSummaryInterlock && b.monitor->getErrorID() == 0);
  }
  for (auto type : {ECMC_PLC_INTERLOCK_DIR_BOTH, ECMC_PLC_INTERLOCK_DIR_BWD, ECMC_PLC_INTERLOCK_DIR_FWD}) {
    // INFERRED: direction names describe which trajectory direction to block.
    context = "PLC directional interlock";
    Bench b; b.settle();
    b.monitor->setPLCInterlock(true, type); b.tick();
    CHECK(b.data.interlocks_.trajSummaryInterlockFWD == (type != ECMC_PLC_INTERLOCK_DIR_BWD));
    CHECK(b.data.interlocks_.trajSummaryInterlockBWD == (type != ECMC_PLC_INTERLOCK_DIR_FWD));
    b.monitor->setPLCInterlock(false, type); b.tick();
    CHECK(!b.data.interlocks_.trajSummaryInterlockFWD && !b.data.interlocks_.trajSummaryInterlockBWD);
  }
}

int main() {
  try {
    followingError(); maximumVelocity(); targetDetection(); directionalLimits();
    std::cout << "Monitor: " << scenarios << " scenarios, " << samples << " production samples passed.\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
