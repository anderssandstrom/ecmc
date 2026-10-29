#!/usr/bin/env python3
"""Test production encoder restore/startup code without EPICS or EtherCAT.

Run: python3 tests/test_axis_startup_restore.py
Extracts current local methods and the STARTUP state on every run. Stand-ins
provide hardware readiness, filters and notifications; this does not exercise
autosave file loading, EPICS record processing, or the complete realtime loop.
"""
# Expectation basis: INFERRED: restore consistency; COMPAT: encoder/startup defaults.
# Rationale, sources and open questions: EXPECTATIONS.md
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
encoder = (ROOT / 'devEcmcSup/motion/ecmcEncoder.cpp').read_text()
axis = (ROOT / 'devEcmcSup/motion/ecmcAxisBase.cpp').read_text()


def method(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2] + '\n'


implementation = ''.join(method(encoder, signature) for signature in (
    'double ecmcEncoder::getActPos()',
    'void ecmcEncoder::setActPos(double pos)',
    'void ecmcEncoder::setHomed(bool homed)',
    'bool ecmcEncoder::getHomed()',
    'int ecmcEncoder::setToZeroIfRelative()',
))
implementation += ''.join(method(axis, signature) for signature in (
    'int ecmcAxisBase::setPosition(double homePositionSet)',
    'int ecmcAxisBase::setAxisHomed(bool homed)',
    'void ecmcAxisBase::initEncoders()',
))
start = axis.index('  case ECMC_AXIS_STATE_STARTUP:')
startup = axis[start:axis.index('  case ECMC_AXIS_STATE_DISABLED:', start)]

stubs = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
enum { ECMC_ENCODER_TYPE_INCREMENTAL, ECMC_ENCODER_TYPE_ABSOLUTE };
enum { ECMC_AXIS_STATE_STARTUP, ECMC_AXIS_STATE_DISABLED };
enum { ERROR_AXIS_HW_NOT_READY = 1, ERROR_AXIS_HARDWARE_STATUS_NOT_OK = 2 };
bool startupHeld = false;
bool commandsHeld = false;
bool ecmcStartupMotionHeld() { return startupHeld; }
bool ecmcStartupCommandsHeld() { return commandsHeld; }
struct Filter {
  int calls = 0;
  double position = 0;
  void initFilter(double pos) { ++calls; position = pos; }
};
class ecmcEncoder {
public:
  int encType_ = ECMC_ENCODER_TYPE_INCREMENTAL, reference = -1;
  bool homed_ = false, floatingPoint = false;
  double rawPosDouble_ = 12.5, invScale_ = 2, engOffset_ = 0;
  uint64_t rawPosUint_ = 25;
  int rawTurnsOld_ = 0, rawTurns_ = 0;
  double rawPosOffset_ = 0, rawPosMultiTurn_ = 0;
  double actPosOld_ = 0, actPosLocal_ = 0, actPos_ = 0;
  Filter velocity, position;
  Filter *velocityFilter_ = &velocity, *positionFilter_ = &position;
  bool actPosEntryUsesFloatingPoint() { return floatingPoint; }
  int getRefToOtherEncAtStartup() { return reference; }
  double getActPos();
  void setActPos(double pos);
  void setHomed(bool homed);
  bool getHomed();
  int setToZeroIfRelative();
};
struct Sequence {
  bool busy = false;
  double position = 0;
  void setGlobalBusy(bool value) { busy = value; }
  void setNewPositionCtrlDrvTrajBumpless(double value) { position = value; }
};
class ecmcAxisBase {
public:
  struct Word { bool enable = false, busy = false, instartup = true; };
  struct Status {
    Word statusWord_;
    int encoderCount = 2;
    bool startupFinsished = false;
    double currentPositionActual = -999, currentTargetPosition = -999, distToStop = 5;
  };
  struct Control {
    int primaryEncIndex = 0;
    bool enableAtStartup = false;
    double positionTarget = -999;
  };
  struct Data { Status status_; Control control_; } data_;
  ecmcEncoder primary, secondary;
  ecmcEncoder *encArray_[2] = {&primary, &secondary};
  Sequence seq_;
  int axisState_ = ECMC_AXIS_STATE_STARTUP, error = 0;
  bool hwReady_ = false, hwReadyOld_ = false, mrEnable = false;
  bool startupEnablePending_ = false;
  bool getStartupCommandsHeld() const { return commandsHeld; }
  int syncs = 0, targetRefreshes = 0;
  ecmcEncoder *getPrimEnc() { return encArray_[data_.control_.primaryEncIndex]; }
  void setEnable(bool value) { data_.status_.statusWord_.enable = value; }
  void setInStartupPhase(bool value) { data_.status_.statusWord_.instartup = value; }
  void setMRSync(bool value) { if (value) ++syncs; }
  void setMRCnen(bool value) { mrEnable = value; }
  void refreshAsynTargetValue() { ++targetRefreshes; }
  void setErrorID(int value) { error = value; }
  int getErrorID() { return error; }
  void errorReset() { error = 0; }
  int setPosition(double homePositionSet);
  int setAxisHomed(bool homed);
  void initEncoders();
  // Surrounding preExecute() normally supplies the readiness samples.
  void startupStep(bool masterOK, bool ready, bool previouslyReady) {
    hwReady_ = ready;
    hwReadyOld_ = previouslyReady;
    auto &status = data_.status_;
    auto &statusWord = status.statusWord_;
    switch (axisState_) {
'''
stubs += startup + '  default: break;\n    }\n  }\n};\n'

tests = r'''
void testEncoderInitialization() {
  for (int type : {ECMC_ENCODER_TYPE_INCREMENTAL, ECMC_ENCODER_TYPE_ABSOLUTE}) {
    for (bool homed : {false, true}) {
      for (bool floating : {false, true}) {
        ecmcEncoder encoder;
        encoder.encType_ = type;
        encoder.floatingPoint = floating;
        encoder.setActPos(-123.25);
        encoder.setHomed(homed);
        const double offset = encoder.rawPosOffset_;
        const int calls = encoder.position.calls;
        assert(encoder.setToZeroIfRelative() == 0);
        bool zero = type == ECMC_ENCODER_TYPE_INCREMENTAL && !homed;
        assert(encoder.getActPos() == (zero ? 0 : -123.25));
        assert(encoder.getHomed() == homed);
        assert(encoder.position.calls == calls + (zero ? 1 : 0));
        assert(encoder.velocity.calls == encoder.position.calls);
        if (!zero) assert(encoder.rawPosOffset_ == offset);
      }
    }
  }
}

void testRestoredStartup() {
  for (double restored : {-123.25, 0.0, 9876.5}) {
    for (bool enable : {false, true}) {
      ecmcAxisBase axis;
      axis.data_.control_.enableAtStartup = enable;
      axis.secondary.setActPos(42); // Unhomed secondary still needs zeroing.
      assert(axis.setPosition(restored) == 0);
      assert(axis.primary.getHomed());
      assert(axis.seq_.position == restored);
      const double offset = axis.primary.rawPosOffset_;
      const int calls = axis.primary.position.calls;

      startupHeld = true;
      axis.startupStep(true, true, true);
      assert(!axis.data_.status_.startupFinsished && axis.syncs == 0);
      startupHeld = false;
      axis.startupStep(false, true, true); // Master unavailable.
      assert(!axis.data_.status_.startupFinsished);
      axis.startupStep(true, false, false); // Hardware not ready.
      assert(axis.error == ERROR_AXIS_HW_NOT_READY);
      assert(!axis.data_.status_.startupFinsished);
      axis.startupStep(true, true, false); // Needs two ready samples.
      assert(!axis.data_.status_.startupFinsished);
      assert(axis.secondary.getActPos() == 42);
      axis.startupStep(true, true, true);

      assert(axis.error == 0);
      assert(axis.data_.status_.startupFinsished);
      assert(!axis.data_.status_.statusWord_.instartup);
      assert(axis.axisState_ == ECMC_AXIS_STATE_DISABLED);
      assert(axis.primary.getActPos() == restored && axis.primary.getHomed());
      assert(axis.primary.rawPosOffset_ == offset);
      assert(axis.primary.position.calls == calls);
      assert(axis.secondary.getActPos() == 0 && !axis.secondary.getHomed());
      assert(axis.data_.status_.currentPositionActual == restored);
      assert(axis.data_.status_.currentTargetPosition == restored);
      assert(axis.data_.control_.positionTarget == restored);
      assert(axis.syncs == 1 && axis.targetRefreshes == 1);
      assert(axis.data_.status_.statusWord_.enable == enable);
      assert(axis.mrEnable == enable);

      // Reentering startup must not re-zero an unhomed encoder or resync.
      axis.secondary.setActPos(75);
      axis.data_.status_.currentPositionActual = restored + 5;
      axis.axisState_ = ECMC_AXIS_STATE_STARTUP;
      axis.setInStartupPhase(true);
      axis.startupStep(true, true, true);
      assert(axis.secondary.getActPos() == 75);
      assert(axis.syncs == 1);
      assert(axis.data_.control_.positionTarget == restored + 5);
      assert(axis.data_.status_.currentTargetPosition == restored + 5);
    }
  }
}

void testStartupReferences() {
  ecmcAxisBase axis;
  axis.setPosition(123.5);
  axis.secondary.reference = 0;
  axis.initEncoders();
  assert(axis.primary.getActPos() == 123.5 && axis.primary.getHomed());
  assert(axis.secondary.getActPos() == 123.5 && axis.secondary.getHomed());

  // Explicit reference configuration takes precedence over a restored value.
  axis.primary.reference = 1;
  axis.secondary.reference = -1;
  axis.secondary.encType_ = ECMC_ENCODER_TYPE_ABSOLUTE;
  axis.secondary.setActPos(456.25);
  axis.initEncoders();
  assert(axis.primary.getActPos() == 456.25 && axis.primary.getHomed());

  for (int invalid : {-1, 2, 99}) {
    axis.primary.reference = invalid;
    axis.setPosition(-25);
    axis.initEncoders();
    assert(axis.primary.getActPos() == -25 && axis.primary.getHomed());
  }
}

void testDeferredStartupEnable() {
  ecmcAxisBase axis;
  axis.setPosition(5);
  axis.data_.control_.enableAtStartup = true;
  commandsHeld = true;
  axis.startupStep(true, true, true);
  assert(axis.data_.status_.startupFinsished);
  assert(!axis.data_.status_.statusWord_.instartup);
  assert(axis.startupEnablePending_);
  assert(!axis.data_.status_.statusWord_.enable && !axis.mrEnable);
  assert(axis.primary.getActPos() == 5 && axis.primary.getHomed());
  assert(axis.data_.status_.currentTargetPosition == 5);
  commandsHeld = false;
}

int main() {
  testEncoderInitialization();
  testRestoredStartup();
  testStartupReferences();
  testDeferredStartupEnable();
  puts("Axis startup/restore: position and homed state, readiness gating, "
       "initial targets, one-time initialization, and encoder references passed.");
}
'''

with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / 'axis_startup_restore.cpp'
    exe = Path(directory) / 'axis_startup_restore'
    cpp.write_text(stubs + implementation + tests)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11', '-O2',
                    '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)],
                   check=True)
    subprocess.run([str(exe)], check=True)
