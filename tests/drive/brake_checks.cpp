// Expectations are checked through public APIs and captured output entries.
// The brake state machine itself is compiled directly from production source.
// DOC/INFERRED/COMPAT labels and open decisions: ../EXPECTATIONS.md (B1/B2).
#include "ecmcDriveStepper.h"
#include "ecmcDriveDS402.h"
#include "ecmcAxisData.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

static size_t cycles = 0, scenarios = 0;
static std::string context;
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
  context + ": line " + std::to_string(__LINE__) + ": " #condition); } while (0)

template<class Drive = ecmcDriveStepper>
struct Bench {
  ecmcAxisData data;
  ecmcAsynPortDriver publication;
  ecmcEcEntry control, velocity, status, brake;
  std::unique_ptr<Drive> drive;
  bool requested = false;
  explicit Bench(int open = 2, int close = 2, double period = 0.001) {
    ++scenarios;
    data.status_.axisId = 1;
    data.status_.sampleTime = period;
    data.status_.statusWord_.instartup = false;
    velocity.type = ECMC_EC_S32;
    velocity.bits = 32;
    brake.type = ECMC_EC_B1;
    brake.bits = 1;
    drive = std::make_unique<Drive>(&publication, data);
    CHECK(drive->getErrorID() == 0);
    CHECK(drive->setEntryAtIndex(&control, ECMC_DRIVEBASE_ENTRY_INDEX_CONTROL_WORD, -1) == 0);
    CHECK(drive->setEntryAtIndex(&velocity, ECMC_DRIVEBASE_ENTRY_INDEX_VELOCITY_SETPOINT, -1) == 0);
    CHECK(drive->setEntryAtIndex(&status, ECMC_DRIVEBASE_ENTRY_INDEX_STATUS_WORD, -1) == 0);
    CHECK(drive->setEntryAtIndex(&brake, ECMC_DRIVEBASE_ENTRY_INDEX_BRAKE_OUTPUT, -1) == 0);
    drive->setScaleNum(1);
    CHECK(drive->setScaleDenom(1) == 0);
    CHECK(drive->setEnableBrake(true) == 0);
    CHECK(drive->setBrakeOpenDelayTime(open) == 0);
    CHECK(drive->setBrakeCloseAheadTime(close) == 0);
    CHECK(drive->validate() == 0);
  }
  void command(bool enable) {
    requested = enable;
    data.status_.statusWord_.enable = enable;
    data.control_.controlWord_.enableCmd = enable;
  }
  void tick(bool feedback, bool master = true) {
    status.value = feedback;
    tickWord(status.value, master);
  }
  void tickWord(uint64_t word, bool master = true) {
    ++cycles;
    status.value = word;
    data.interlocks_.etherCatMasterInterlock = !master;
    drive->readEntries(master);
    drive->writeEntries();
  }
  void open(int delay = 2) {
    // Ordinary starts use acknowledged OFF feedback. Delayed acknowledgement
    // and rapid reversals are exercised separately below.
    if (!drive->getEnable()) tick(false);
    command(true);
    for (int i = 0; i < delay + 3; ++i) tick(true);
    CHECK(brake.value == 1);
    CHECK(drive->getEnable());
    CHECK(drive->getEnabled());
  }
};

static void timing() {
  // DOC: ecmcMotion.h::setAxisDrvBrakeOpenDelayTime/CloseAheadTime specifies
  // sequencing and cycle units. Exact sample/pipeline offsets are COMPAT B1.
  for (double period : {0.0005, 0.001, 0.01}) {
    for (int open : {0, 1, 3, 10}) for (int close : {0, 1, 3, 10}) {
      context = "timing open=" + std::to_string(open) + " close=" + std::to_string(close);
      Bench<> b(open, close, period);
      b.tick(false);
      CHECK(!b.drive->getEnable() && b.brake.value == 0);
      b.command(true);
      // Waiting for drive feedback must never consume the opening delay.
      for (int i = 0; i < 7; ++i) {
        b.tick(false);
        CHECK(b.drive->getEnable() && b.brake.value == 0 && !b.drive->getEnabled());
      }
      // COMPAT/REVIEW B1: counter > delay releases on enabled sample N+2.
      for (int i = 0; i <= open; ++i) {
        b.tick(true);
        CHECK(b.brake.value == 0 && !b.drive->getEnabled());
      }
      b.tick(true);
      CHECK(b.brake.value == 1 && b.drive->getEnable());
      b.tick(true); // published enabled reflects the preceding brake command
      CHECK(b.drive->getEnabled());
      b.command(false);
      for (int i = 0; i <= close; ++i) {
        b.tick(true);
        CHECK(b.brake.value == 0 && b.drive->getEnable());
        CHECK(b.control.value == 1); // amplifier holds while brake applies
      }
      b.tick(true);
      CHECK(b.brake.value == 0 && !b.drive->getEnable());
      b.tick(true); // stepper sends the amplifier command on the next cycle
      CHECK(b.control.value == 0 && b.brake.value == 0);
    }
  }
}

static void feedbackLoss() {
  // INFERRED: feedback loss demands brake application in every phase.
  // INFERRED/REVIEW B2: restart requires a fresh enable, not feedback alone.
  for (int open : {0, 1, 3}) for (int close : {0, 1, 3}) {
    // Lose feedback at every enabled sample before and after brake release.
    for (int sample = 1; sample <= open + 4; ++sample) {
      context = "feedback loss while opening/open sample=" + std::to_string(sample);
      Bench<> b(open, close);
      b.command(true);
      for (int i = 0; i < sample; ++i) b.tick(true);
      b.tick(false);
      CHECK(b.brake.value == 0 && !b.drive->getEnable() && !b.drive->getEnabled());
      CHECK(!b.data.status_.statusWord_.enable);
      CHECK(b.drive->getWarningID() == WARNING_DRV_ENABLED_LOST);
      CHECK(b.drive->getErrorID() == ERROR_AXIS_AMPLIFIER_ENABLED_LOST);
      CHECK(b.data.status_.errorCode == ERROR_AXIS_AMPLIFIER_ENABLED_LOST);
      CHECK(!b.data.control_.controlWord_.enableCmd);
      // Returning feedback alone must not release the brake or restart power.
      for (int i = 0; i < open + close + 5; ++i) {
        b.tick(true);
        CHECK(b.brake.value == 0 && !b.drive->getEnable());
        CHECK(b.drive->getErrorID() == ERROR_AXIS_AMPLIFIER_ENABLED_LOST);
      }
      b.command(false); b.tick(false);
      b.drive->errorReset();
      b.tick(true);
      CHECK(b.brake.value == 0 && !b.drive->getEnable());
      b.open(open); // a fresh enable edge must recover with the complete delay
    }
    for (int sample = 0; sample <= close + 2; ++sample) {
      context = "feedback loss while closing sample=" + std::to_string(sample);
      Bench<> b(open, close);
      b.open(open); b.command(false);
      for (int i = 0; i < sample; ++i) b.tick(true);
      b.tick(false);
      CHECK(b.brake.value == 0 && !b.drive->getEnable());
      CHECK(b.drive->getErrorID() == 0); // commanded shutdown is not enable loss
      b.tick(true);
      CHECK(b.brake.value == 0 && !b.drive->getEnable());
    }
    context = "unexpected feedback while closed";
    Bench<> b(open, close);
    for (bool feedback : {true, false, true, false}) {
      b.tick(feedback);
      CHECK(b.brake.value == 0 && !b.drive->getEnable());
    }
  }
}

static void feedbackLossWithoutBrake() {
  context = "lost enable without brake remains a latched axis fault";
  Bench<> b;
  CHECK(b.drive->setEnableBrake(false) == 0);
  b.command(true); b.tick(true);
  CHECK(b.drive->getEnabled());
  b.tick(false);
  CHECK(b.drive->getErrorID() == ERROR_AXIS_AMPLIFIER_ENABLED_LOST);
  CHECK(!b.drive->getEnable() && !b.data.control_.controlWord_.enableCmd);
  for (int i = 0; i < 8; ++i) b.tick(false);
  CHECK(b.drive->getErrorID() == ERROR_AXIS_AMPLIFIER_ENABLED_LOST);
  b.drive->errorReset();
  CHECK(b.drive->getErrorID() == 0 && !b.drive->getEnable());
  b.command(true); b.tick(true);
  CHECK(b.drive->getEnabled());
  b.command(false); b.tick(false);
  CHECK(b.drive->getErrorID() == 0);

  context = "enable loss preserves an existing axis error";
  Bench<> priorError;
  priorError.open();
  priorError.data.status_.errorCode = ERROR_ENC_NOT_READY;
  priorError.tick(false);
  CHECK(priorError.data.status_.errorCode == ERROR_ENC_NOT_READY);
  CHECK(priorError.brake.value == 0 && !priorError.drive->getEnable());
}

static void cancellationAndReenable() {
  // COMPAT/REVIEW B2: rapid reversal and cancelled-opening restart policy.
  for (int sample = 0; sample < 7; ++sample) {
    context = "cancel opening at sample=" + std::to_string(sample);
    Bench<> b(4, 3);
    b.command(true);
    for (int i = 0; i < sample; ++i) b.tick(true);
    b.command(false); b.tick(true);
    CHECK(b.brake.value == 0);
    for (int i = 0; i < 6; ++i) b.tick(true);
    CHECK(b.brake.value == 0 && !b.drive->getEnable());
  }
  for (int sample = 0; sample < 6; ++sample) {
    context = "reenable during/after closing sample=" + std::to_string(sample);
    Bench<> b(3, 3);
    b.open(3); b.command(false);
    for (int i = 0; i <= sample; ++i) b.tick(true);
    CHECK(b.brake.value == 0);
    if (!b.drive->getEnable()) b.tick(false); // completed close, OFF acknowledged
    b.command(true);
    for (int i = 0; i <= 3; ++i) {
      b.tick(true);
      CHECK(b.brake.value == 0);
    }
    b.tick(true);
    CHECK(b.brake.value == 1);
  }
}

static void interlocksAndTimeout() {
  // INFERRED: interlock/timeout cannot release the brake or revive a request.
  // COMPAT: exact timeout sample/error. REVIEW B2: fresh-enable/reset policy.
  for (int phase = 0; phase < 4; ++phase) for (bool bus : {false, true}) {
    context = "interlock phase=" + std::to_string(phase);
    Bench<> b;
    if (phase == 1) { b.command(true); b.tick(true); }
    if (phase >= 2) b.open();
    if (phase == 3) { b.command(false); b.tick(true); }
    if (!bus) b.data.interlocks_.driveSummaryInterlock = true;
    b.tick(true, !bus);
    CHECK(b.brake.value == 0);
    CHECK(!b.data.status_.statusWord_.enable);
    for (int i = 0; i < 8; ++i) { b.tick(true, !bus); CHECK(b.brake.value == 0); }
    b.data.interlocks_.driveSummaryInterlock = false;
    for (int i = 0; i < 5; ++i) { b.tick(true); CHECK(b.brake.value == 0 && !b.drive->getEnable()); }
    b.command(false); b.tick(false); b.open();
  }
  context = "enable feedback timeout";
  Bench<> b;
  CHECK(b.drive->setStateMachineTimeout(0.003) == 0);
  b.command(true);
  for (int i = 0; i < 3; ++i) {
    b.tick(false);
    CHECK(b.drive->getErrorID() == 0 && b.brake.value == 0);
  }
  b.tick(false);
  CHECK(b.drive->getErrorID() == ERROR_DRV_STATE_MACHINE_TIME_OUT);
  CHECK(!b.drive->getEnable() && b.brake.value == 0);
  // A late enabled response after timeout must not release the brake or
  // resurrect the timed-out power request without an explicit new command.
  for (int i = 0; i < 8; ++i) {
    b.tick(true);
    CHECK(b.brake.value == 0 && !b.drive->getEnable());
  }
  b.drive->errorReset();
  b.tick(true);
  CHECK(b.brake.value == 0 && !b.drive->getEnable());
  b.command(false); b.tick(false);
  b.drive->errorReset();
  b.open();
}

static void delayedDisableAcknowledgement() {
  // The previous amplifier-OFF command may still be in flight when a new
  // enable request arrives. Its delayed feedback fall is not a new drive loss.
  for (bool useBrake : {false, true}) for (int delay : {0, 1, 12, 50}) {
    context = "restart before previous disable feedback, delay=" + std::to_string(delay);
    Bench<> b;
    b.drive->setEnableBrake(useBrake);
    b.command(true);
    for (int i = 0; i < 8; ++i) b.tick(true);
    CHECK(b.drive->getEnabled());
    b.command(false);
    for (int i = 0; i < 20 && (b.control.value & 1); ++i) b.tick(true);
    CHECK(!(b.control.value & 1)); // OFF has been sent, feedback is still high
    b.command(true);
    for (int i = 0; i < delay; ++i) {
      b.tick(true);
      CHECK(b.drive->getErrorID() == 0 && b.brake.value == 0);
      CHECK(!b.drive->getEnable());
    }
    b.tick(false); // acknowledgement of the previous OFF command
    CHECK(b.drive->getErrorID() == 0);
    for (int i = 0; i < 4; ++i) {
      b.tick(false);
      CHECK(b.drive->getErrorID() == 0 && b.brake.value == 0);
    }
    for (int i = 0; i < 8; ++i) b.tick(true);
    CHECK(b.drive->getErrorID() == 0 && b.drive->getEnabled());
    CHECK(!useBrake || b.brake.value == 1);
    b.tick(false); // real loss after the fresh enable must still latch a fault
    CHECK(b.drive->getErrorID() == ERROR_AXIS_AMPLIFIER_ENABLED_LOST);
  }

  context = "missing OFF acknowledgement uses the existing enable timeout";
  Bench<> timeout;
  timeout.open(); timeout.command(false);
  for (int i = 0; i < 10; ++i) timeout.tick(true);
  CHECK(timeout.drive->setStateMachineTimeout(.003) == 0);
  timeout.command(true);
  for (int i = 0; i < 4; ++i) timeout.tick(true);
  CHECK(timeout.drive->getErrorID() == ERROR_DRV_STATE_MACHINE_TIME_OUT);
  CHECK(timeout.brake.value == 0 && !timeout.drive->getEnable());

  context = "disable cancels a pending OFF acknowledgement without replay";
  Bench<> cancelled;
  cancelled.open(); cancelled.command(false);
  for (int i = 0; i < 10; ++i) cancelled.tick(true);
  cancelled.command(true); cancelled.tick(true);
  CHECK(cancelled.brake.value == 0 && !cancelled.drive->getEnable());
  cancelled.command(false); cancelled.tick(true); cancelled.tick(false);
  for (int i = 0; i < 8; ++i) cancelled.tick(true);
  CHECK(cancelled.drive->getErrorID() == 0 && cancelled.brake.value == 0 && !cancelled.drive->getEnable());
}

static void configurationAndFaults() {
  // INFERRED: reject unusable configuration and report I/O failures.
  // COMPAT: exact error codes, bypass and captured-write behavior.
  context = "negative delays preserve configured values";
  Bench<> b(3, 4);
  CHECK(b.drive->setBrakeOpenDelayTime(-1) == ERROR_DRV_BRAKE_OPEN_DELAY_TIME_INVALID);
  CHECK(b.drive->getBrakeOpenDelayTime() == 3);
  b.drive->errorReset();
  CHECK(b.drive->setBrakeCloseAheadTime(-1) == ERROR_DRV_BRAKE_CLOSE_AHEAD_TIME_INVALID);
  CHECK(b.drive->getBrakeCloseAheadTime() == 4);
  b.drive->errorReset();
  CHECK(b.drive->setEntryAtIndex(nullptr, ECMC_DRIVEBASE_ENTRY_INDEX_BRAKE_OUTPUT, -1) == 0);
  CHECK(b.drive->setEnableBrake(true) == ERROR_DRV_BRAKE_ENTRY_NULL);
  CHECK(!b.drive->getEnableBrake());

  context = "brake-disabled bypass";
  Bench<> bypass;
  CHECK(bypass.drive->setEnableBrake(false) == 0);
  bypass.command(true); bypass.tick(false);
  CHECK(bypass.drive->getEnable() && !bypass.drive->getEnabled());
  bypass.tick(true);
  CHECK(bypass.drive->getEnabled() && bypass.brake.value == 0);
  bypass.command(false); bypass.tick(true);
  CHECK(!bypass.drive->getEnable() && bypass.brake.value == 0);

  context = "startup domain unavailable must not release brake";
  Bench<> startup;
  startup.data.status_.statusWord_.instartup = true;
  startup.status.readError = ERROR_EC_ENTRY_EC_DOMAIN_ERROR;
  startup.control.writeError = ERROR_EC_ENTRY_EC_DOMAIN_ERROR;
  startup.command(true);
  for (int i = 0; i < 5; ++i) {
    startup.tick(false);
    CHECK(startup.brake.value == 0 && startup.drive->getErrorID() == 0);
  }
  startup.status.readError = 0;
  startup.control.writeError = 0;
  startup.data.status_.statusWord_.instartup = false;
  for (int i = 0; i < 3; ++i) { startup.tick(true); CHECK(startup.brake.value == 0); }
  startup.tick(true);
  CHECK(startup.brake.value == 1);

  for (int phase = 0; phase < 4; ++phase) {
    context = "unreadable status phase=" + std::to_string(phase);
    Bench<> unreadable;
    if (phase == 0) { unreadable.command(true); unreadable.tick(false); }
    if (phase == 1) { unreadable.command(true); unreadable.tick(true); }
    if (phase >= 2) unreadable.open();
    if (phase == 3) { unreadable.command(false); unreadable.tick(true); }
    unreadable.status.readError = 0x21007; // ERROR_EC_ENTRY_READ_FAIL
    unreadable.tick(true);
    CHECK(unreadable.drive->getErrorID() == 0x21007);
    CHECK(unreadable.brake.value == 0 && !unreadable.drive->getEnable());
    unreadable.status.readError = 0;
    for (int i = 0; i < 8; ++i) {
      unreadable.tick(true);
      CHECK(unreadable.brake.value == 0 && !unreadable.drive->getEnable());
    }
    unreadable.drive->errorReset();
    unreadable.tick(true);
    CHECK(unreadable.brake.value == 0 && !unreadable.drive->getEnable());
    unreadable.command(false); unreadable.tick(false); unreadable.open();
  }

  context = "brake write failure is reported, not a false successful output";
  Bench<> writeFailure;
  writeFailure.open();
  writeFailure.brake.writeError = 0x21008; // ERROR_EC_ENTRY_WRITE_FAIL
  writeFailure.command(false); writeFailure.tick(true);
  CHECK(writeFailure.drive->getErrorID() == 0x21008);
  CHECK(writeFailure.brake.value == 1); // failed write cannot change physical I/O
  writeFailure.brake.writeError = 0;
  writeFailure.tick(false);
  CHECK(writeFailure.brake.value == 0 && !writeFailure.drive->getEnable());
  writeFailure.drive->errorReset();
  writeFailure.open();

  for (int alarmIndex = 8; alarmIndex <= 10; ++alarmIndex) {
    context = "hardware alarm while brake open index=" + std::to_string(alarmIndex);
    Bench<> alarmBench;
    ecmcEcEntry alarm;
    CHECK(alarmBench.drive->setEntryAtIndex(&alarm, alarmIndex, -1) == 0);
    CHECK(alarmBench.drive->validate() == 0);
    alarmBench.open(); alarm.value = 1; alarmBench.tick(true);
    CHECK(alarmBench.brake.value == 0 && alarmBench.control.value == 0 && !alarmBench.drive->getEnable());
    CHECK(alarmBench.drive->getErrorID() != 0);
    alarm.value = 0;
    for (int i = 0; i < 8; ++i) alarmBench.tick(true);
    alarmBench.drive->errorReset();
    CHECK(alarmBench.brake.value == 0);
    alarmBench.command(false); alarmBench.tick(false); alarmBench.open();
  }
  // Slit fixture: enabled bit 1 and hardware fault bit 3 share the status word.
  // Fault recognition must not depend on whether enabled feedback also drops.
  for (uint64_t faultWord : {uint64_t(8), uint64_t(10)}) {
    context = "linked EL70xx fault bit while brake open";
    Bench<> b;
    CHECK(b.drive->setEntryAtIndex(&b.status, ECMC_DRIVEBASE_ENTRY_INDEX_STATUS_WORD, 1) == 0);
    CHECK(b.drive->setEntryAtIndex(&b.status, ECMC_DRIVEBASE_ENTRY_INDEX_ALARM_0, 3) == 0);
    CHECK(b.drive->validate() == 0);
    b.command(true);
    for (int i = 0; i < 8; ++i) b.tickWord(2);
    CHECK(b.brake.value == 1);
    b.tickWord(faultWord);
    CHECK(b.drive->getErrorID() == ERROR_DRV_HW_ALARM_0);
    CHECK(b.brake.value == 0 && b.control.value == 0 && !b.drive->getEnable());
  }
}

static void boundedSignalTraces() {
  // All enable-request/feedback sequences of length six, at three timings.
  // INFERRED: no unauthorized release; REVIEW B2: authorization/restart policy.
  // COMPAT/REVIEW B1: enabledSamples >= delay+2 preserves the current offset.
  for (int delay : {0, 1, 3}) for (unsigned int trace = 0; trace < 4096; ++trace) {
    context = "signal trace delay=" + std::to_string(delay) + " trace=" + std::to_string(trace);
    Bench<> b(delay, delay);
    bool lastRequest = false, oldFeedback = false, authorized = false;
    int enabledSamples = 0;
    unsigned int signals = trace;
    for (int i = 0; i < 6; ++i, signals >>= 2) {
      bool request = signals & 1;
      bool feedback = signals & 2;
      if (request != lastRequest) {
        b.command(request);
        authorized = request;
        enabledSamples = 0;
      }
      // A fall only revokes authorization during the same ON request while
      // the amplifier is still commanded on. A pending OFF acknowledgement
      // belongs to the preceding request and permits a fresh handshake.
      if (lastRequest && request && oldFeedback && !feedback && b.drive->getEnable()) authorized = false;
      if (!feedback) enabledSamples = 0;
      if (authorized && feedback) ++enabledSamples;
      b.tick(feedback);
      if (!request || !feedback || !authorized) CHECK(b.brake.value == 0);
      if (b.brake.value) CHECK(enabledSamples >= delay + 2);
      lastRequest = request;
      oldFeedback = feedback;
    }
  }
}

static void ds402PlantTick(Bench<ecmcDriveDS402> &b) {
    uint64_t response = 0x40;
    if (b.control.value == 6) response = 0x21;
    if (b.control.value == 7) response = 0x23;
    if (b.control.value == 15) response = 0x27;
    b.tickWord(response);
    if (response != 0x27) CHECK(b.brake.value == 0);
}

static void ds402Integration() {
  // COMPAT: production handshake/control-word sequencing against this plant;
  // this is not an independent DS402 conformance test.
  for (int phase = 0; phase < 7; ++phase) for (bool fault : {false, true}) {
    context = "DS402 feedback/fault loss enabled sample=" + std::to_string(phase);
    Bench<ecmcDriveDS402> b(3, 3);
    b.tickWord(0x40); // clear startup/reset with no fault
    b.command(true);
    for (int i = 0; i < 20 && !b.drive->getEnabledLocal(); ++i) ds402PlantTick(b);
    CHECK(b.drive->getEnabledLocal());
    for (int i = 0; i < phase; ++i) b.tickWord(0x27);
    b.tickWord(fault ? 0x08 : 0x23);
    CHECK(b.brake.value == 0 && !b.drive->getEnable());
    b.tickWord(fault ? 0x08 : 0x27);
    CHECK(b.brake.value == 0 && !b.drive->getEnable());
    CHECK(b.drive->getErrorID() == (fault ? ERROR_DRV_DS402_FAULT_STATE
                                         : ERROR_AXIS_AMPLIFIER_ENABLED_LOST));
    b.command(false); b.drive->errorReset();
    for (int i = 0; i < 3; ++i) b.tickWord(0x40);
    b.command(true);
    for (int i = 0; i < 30 && !b.brake.value; ++i) ds402PlantTick(b);
    CHECK(b.brake.value == 1);
  }
  for (int delay : {0, 1, 3}) {
    context = "DS402 normal shutdown close-ahead=" + std::to_string(delay);
    Bench<ecmcDriveDS402> b(2, delay);
    b.tickWord(0x40); b.command(true);
    for (int i = 0; i < 30 && !b.brake.value; ++i) ds402PlantTick(b);
    CHECK(b.brake.value == 1);
    b.command(false);
    for (int i = 0; i <= delay; ++i) {
      ds402PlantTick(b);
      CHECK(b.brake.value == 0 && b.drive->getEnable() && b.control.value == 15);
    }
    ds402PlantTick(b);
    CHECK(b.brake.value == 0 && !b.drive->getEnable());
    for (int i = 0; i < 3; ++i) ds402PlantTick(b);
    CHECK(b.control.value == 0 && b.brake.value == 0);
  }
  context = "DS402 restart before disable acknowledgement";
  Bench<ecmcDriveDS402> restart;
  restart.tickWord(0x40); restart.command(true);
  for (int i = 0; i < 30 && !restart.brake.value; ++i) ds402PlantTick(restart);
  CHECK(restart.brake.value == 1);
  restart.command(false);
  for (int i = 0; i < 20; ++i) restart.tickWord(0x27);
  CHECK(restart.control.value == 0);
  restart.command(true);
  for (int i = 0; i < 5; ++i) {
    restart.tickWord(0x27);
    CHECK(restart.drive->getErrorID() == 0 && restart.brake.value == 0 && !restart.drive->getEnable());
  }
  restart.tickWord(0x40);
  for (int i = 0; i < 30 && !restart.brake.value; ++i) ds402PlantTick(restart);
  CHECK(restart.drive->getErrorID() == 0 && restart.brake.value == 1);
}

int main() {
  try {
    timing(); feedbackLoss(); feedbackLossWithoutBrake(); cancellationAndReenable();
    interlocksAndTimeout(); delayedDisableAcknowledgement(); configurationAndFaults();
    boundedSignalTraces(); ds402Integration();
    std::cout << "Drive brake: " << scenarios << " scenarios, " << cycles
              << " production cycles passed.\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
