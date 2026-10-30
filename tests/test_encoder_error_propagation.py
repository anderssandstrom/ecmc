#!/usr/bin/env python3
"""Compile production encoder readiness/read ordering with controlled I/O.

REGRESSION: a primary encoder's earlier readiness/read failure must not be
published as zero (or replaced by a later sub-read error). Secondary failures
remain local. Transport and primary selection are controlled boundaries.
"""
from pathlib import Path
import os
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
sup = repo / 'devEcmcSup'
source = (sup / 'motion/ecmcEncoder.cpp').read_text()


def method(signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


code = r'''
#include "ecmcAxisData.h"
#include "ecmcDriveStepper.h"
#include "ecmcError.h"
#include "ecmcErrorsList.h"
#include "ecmcRtLogger.h"
#include <array>
#include <cassert>
#include <iostream>
struct Publication { int calls = 0; void refreshParamRT(int) { ++calls; } };
class ecmcEncoder : public ecmcError {
public:
  ecmcAxisData *data_;
  bool primary, domainOK_ = true, masterOKOld_ = true;
  bool hwReadyBitDefined_ = true, hwReadyInvert_ = false, encInitilized_ = true;
  uint64_t hwReady_ = 1, readyInput = 1;
  int readyReadError = 0, encLocalErrorId_ = 0, encLocalErrorIdOld_ = 0;
  double actPosOld_ = 0, actPos_ = 0, actPosLocal_ = 0, actVel_ = 0, actVelLocal_ = 0;
  std::array<int,4> errors{}; // warning/error, position, latch, touch-probe reads
  Publication pos, vel, err;
  Publication *encPosAct_ = &pos, *encVelAct_ = &vel, *encErrId_ = &err;
  ecmcEncoder(ecmcAxisData &data, bool selected) : data_(&data), primary(selected) {
    setExternalPtrs(&data.status_.errorCode, &data.status_.warningCode);
  }
  bool isPrimary() { return primary; }
  bool checkAllDataValid() { return domainOK_; }
  int readDataValue(int index, uint64_t *value) {
    assert(index == ECMC_ENCODER_ENTRY_INDEX_READY);
    *value = readyInput;
    return readyReadError;
  }
  int readHwWarningError(bool) { return errors[0]; }
  int readHwActPos(bool, bool) { return errors[1]; }
  int readHwLatch(bool) { return errors[2]; }
  int readHwTouchProbe(bool) { return errors[3]; }
  int readHwReady(bool);
  int hwReady();
  double readEntries(bool);
  void reset() { encLocalErrorId_ = 0; ecmcError::errorReset(); }
};
'''
code += '\n'.join(method(signature) for signature in (
    'int ecmcEncoder::readHwReady(bool domainOK)',
    'int ecmcEncoder::hwReady()',
    'double ecmcEncoder::readEntries(bool masterOK)'))
code += r'''
int main() {
  for (bool enabled : {false, true}) for (bool requested : {false, true}) {
    ecmcAxisData data;
    data.status_.statusWord_.enabled = enabled;
    data.status_.statusWord_.enable = requested;
    data.control_.controlWord_.enableCmd = requested;
    ecmcEncoder encoder(data, true);
    encoder.readyInput = 0;
    encoder.readEntries(true);
    assert(encoder.encLocalErrorId_ == ERROR_ENC_NOT_READY);
    assert(data.status_.errorCode == ERROR_ENC_NOT_READY);
    assert(!encoder.hwReady());
    assert(!data.control_.controlWord_.enableCmd);
    assert(!data.status_.statusWord_.enable);
    encoder.readyInput = 1;
    for (int cycle = 0; cycle < 3; ++cycle) {
      encoder.readEntries(true);
      assert(data.status_.errorCode == ERROR_ENC_NOT_READY);
    }
    encoder.reset(); encoder.readEntries(true);
    assert(data.status_.errorCode == 0 && encoder.hwReady());
  }
  // Successful later reads must not erase earlier failed sub-reads.
  for (int stage = 0; stage < 4; ++stage) {
    ecmcAxisData data;
    ecmcEncoder encoder(data, true);
    encoder.errors[stage] = ERROR_ENC_ENTRY_READ_FAIL;
    encoder.readEntries(true);
    assert(data.status_.errorCode == ERROR_ENC_ENTRY_READ_FAIL);
    assert(encoder.err.calls == 1);
  }
  ecmcAxisData data;
  ecmcEncoder encoder(data, true);
  encoder.readyReadError = ERROR_ENC_READY_READ_ENTRY_FAIL;
  encoder.errors[3] = ERROR_ENC_ENTRY_READ_FAIL;
  encoder.readEntries(true);
  assert(data.status_.errorCode == ERROR_ENC_READY_READ_ENTRY_FAIL);
  encoder.reset(); encoder.readyReadError = 0;
  encoder.errors[0] = ERROR_ENC_HW_ALARM_0;
  encoder.readyInput = 0;
  encoder.readEntries(true);
  assert(data.status_.errorCode == ERROR_ENC_HW_ALARM_0);

  // A secondary encoder must not overwrite an unrelated primary-axis fault
  // or remove the primary axis's enable request.
  ecmcAxisData secondaryData;
  ecmcEncoder secondary(secondaryData, false);
  secondaryData.status_.statusWord_.enabled = true;
  secondaryData.control_.controlWord_.enableCmd = true;
  secondaryData.status_.errorCode = ERROR_AXIS_SLAVED_AXIS_INTERLOCK;
  secondary.readyInput = 0;
  secondary.readEntries(true);
  assert(secondary.encLocalErrorId_ == ERROR_ENC_NOT_READY && secondary.err.calls == 1);
  assert(secondaryData.status_.errorCode == ERROR_AXIS_SLAVED_AXIS_INTERLOCK);
  assert(secondaryData.control_.controlWord_.enableCmd);

  // Integrate production readiness/error handling with the real stepper/brake
  // path. Clearing the external command alone must not leave RT enable high.
  for (bool opening : {false, true}) {
    ecmcAxisData motion;
    motion.status_.sampleTime = .001;
    ecmcAsynPortDriver publication;
    ecmcDriveStepper drive(&publication, motion);
    ecmcEncoder primary(motion, true);
    ecmcEcEntry control, velocity, feedback, brake;
    velocity.type = ECMC_EC_S32; velocity.bits = 32;
    brake.type = ECMC_EC_B1; brake.bits = 1;
    assert(drive.setEntryAtIndex(&control, ECMC_DRIVEBASE_ENTRY_INDEX_CONTROL_WORD, -1) == 0);
    assert(drive.setEntryAtIndex(&velocity, ECMC_DRIVEBASE_ENTRY_INDEX_VELOCITY_SETPOINT, -1) == 0);
    assert(drive.setEntryAtIndex(&feedback, ECMC_DRIVEBASE_ENTRY_INDEX_STATUS_WORD, -1) == 0);
    assert(drive.setEntryAtIndex(&brake, ECMC_DRIVEBASE_ENTRY_INDEX_BRAKE_OUTPUT, -1) == 0);
    drive.setScaleNum(1); assert(drive.setScaleDenom(1) == 0);
    assert(drive.setEnableBrake(true) == 0);
    assert(drive.setBrakeOpenDelayTime(opening ? 20 : 2) == 0);
    assert(drive.setBrakeCloseAheadTime(3) == 0);
    assert(drive.validate() == 0);
    motion.control_.drvMode = ECMC_DRV_MODE_CSV;
    motion.status_.statusWord_.enable = motion.control_.controlWord_.enableCmd = true;
    auto tick = [&]() {
      primary.readEntries(true);
      drive.readEntries(true);
      drive.setVelSet(motion.status_.statusWord_.enable ? 1 : 0);
      drive.writeEntries();
      feedback.value = control.value & 1;
    };
    for (int i = 0; i < (opening ? 4 : 12); ++i) tick();
    assert(drive.getEnable());
    assert(brake.value == (opening ? 0 : 1));
    primary.readyInput = 0;
    for (int i = 0; i < 10; ++i) {
      tick();
      assert(brake.value == 0);
      assert(!motion.status_.statusWord_.enable && !motion.control_.controlWord_.enableCmd);
      assert(motion.status_.errorCode == ERROR_ENC_NOT_READY);
    }
    assert(!drive.getEnabled() && !drive.getEnable());
    assert(control.value == 0 && velocity.value == 0);
  }

  // Domain-invalid samples keep their existing handling; this fix does not
  // turn a missing/invalid domain sample into a new ready-bit fault.
  ecmcAxisData invalidDomain;
  ecmcEncoder probe(invalidDomain, true);
  invalidDomain.status_.statusWord_.enable = true;
  invalidDomain.control_.controlWord_.enableCmd = true;
  probe.readyInput = 0;
  assert(probe.readHwReady(false) == 0);
  assert(invalidDomain.status_.statusWord_.enable && invalidDomain.control_.controlWord_.enableCmd);
  std::cout << "Encoder errors: primary idle/runtime readiness and read faults propagate, recovery stays latched until reset, secondary faults remain local.\n";
}
'''
with tempfile.TemporaryDirectory(prefix='ecmc-encoder-errors-') as directory:
    path = Path(directory) / 'checks.cpp'
    path.write_text(code.replace('ecmcEncoder', 'EncoderReadinessProbe'))
    binary = Path(directory) / 'checks'
    includes = [repo / 'tests/drive/offline', repo / 'tools/plc-test/offline'] + [sup / part for part in ('motion','main','com','misc','ethercat')]
    command = [os.environ.get('CXX','c++'), '-std=c++17', '-O0', '-g0', '-UNDEBUG',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all']
    command += ['-I' + str(include) for include in includes]
    subprocess.run(command + [str(path), str(repo / 'tests/drive/support.cpp'),
                              str(sup / 'motion/ecmcDriveBase.cpp'),
                              str(sup / 'motion/ecmcDriveStepper.cpp'),
                              str(sup / 'motion/ecmcAxisData.cpp'),
                              str(sup / 'main/ecmcError.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
