#!/usr/bin/env python3
"""Compile ECMC startup alarm recovery and model the motor restore command guard."""
from pathlib import Path
import os
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] /
          'devEcmcSup/motor/ecmcMotorRecordController.cpp').read_text()


def method(signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


implementation = '\n'.join(method(signature) for signature in (
    'void clearStartupCommunicationAlarm(',
    'void ecmcMotorRecordController::bindStartupMotorRecord(',
    'asynStatus ecmcMotorRecordController::doCallbacksGenericPointer(',
))
stubs = r'''
#include <cassert>
#include <math.h>
#include <limits>
#include <initializer_list>
enum asynStatus { asynSuccess, asynError };
constexpr int ECMC_MAX_AXES = 4;
constexpr int NO_ALARM = 0, COMM_ALARM = 9, STATE_ALARM = 7;
constexpr int DBE_VALUE = 1, DBE_LOG = 2, DBE_ALARM = 4;
constexpr int MIP_DONE = 0, MIP_LOAD_P = 128;
struct motorRecord {
  int stat = COMM_ALARM, nsta = NO_ALARM, sevr = 3, nsev = 0;
  bool pact = false;
  double val = 0, saved = 5, actual = 0;
  int mip = MIP_DONE;
  bool dmov = true, movn = false;
};
using dbCommon = motorRecord;
bool recordLocked = false, faultOnLock = false;
int resets = 0, events = 0;
struct NativeAxis {
  bool started = true, startup = false, blocked = false;
  int error = 0;
  bool motorReady = true;
  bool getMotorRecordStartupReady() { return motorReady; }
  void setMotorRecordStartupReady(bool ready) { motorReady = ready; }
  bool getRealTimeStarted() { return started; }
  bool getInStartupPhase() { return startup; }
  bool getBlockCom() { return blocked; }
  int getErrorID() { return error; }
} native;
void dbScanLock(dbCommon *) {
  assert(!recordLocked); recordLocked = true;
  if (faultOnLock) native.error = 123;
}
void dbScanUnlock(dbCommon *) { assert(recordLocked); recordLocked = false; }
void *ecmcRTMutex = nullptr;
void epicsMutexLock(void *) { assert(recordLocked); }
void epicsMutexUnlock(void *) { assert(recordLocked); }
unsigned short recGblResetAlarms(motorRecord *r) {
  assert(recordLocked); ++resets;
  r->stat = r->nsta; r->sevr = r->nsev;
  r->nsta = r->nsev = NO_ALARM;
  return DBE_ALARM;
}
void db_post_events(motorRecord *, void *, unsigned short) {
  assert(recordLocked); ++events;
}
bool iocReady = true;
bool ecmcIocRunning() { return iocReady; }
struct MotorStatus {
  double position = 0;
  unsigned long status = 0;
  bool positionWritten = true;
};
union msta_field {
  unsigned long All;
  struct { unsigned long CNTRL_COMM_ERR : 1; unsigned long RA_PROBLEM : 1; } Bits;
};
struct ecmcMotorRecordAxis {
  bool updateFirstPollDone_ = true, waitingForStatusAfterCommand_ = false;
  struct { NativeAxis *ecmcAxis = &native; } drvlocal;
} axis;
struct asynMotorController {
  int motorStatus_ = 25, callbacks = 0, queuedPositions = 0;
  asynStatus parameterStatus = asynSuccess;
  bool readParamFails = false;
  bool deferPositionLoad = false;
  motorRecord *record = nullptr;
  asynStatus getParamStatus(int, int, asynStatus *result) {
    *result = parameterStatus; return readParamFails ? asynError : asynSuccess;
  }
  asynStatus doCallbacksGenericPointer(void *, int, int) {
    ++callbacks;
    // devMotorAsyn::build_trans rejects LOAD_POS under STAT/NSTA COMM_ALARM.
    if (record && record->stat != COMM_ALARM && record->nsta != COMM_ALARM &&
        queuedPositions == 0) {
      assert(recordLocked);
      ++queuedPositions;
      if (deferPositionLoad) { record->mip = MIP_LOAD_P; record->dmov = false; }
      else record->actual = record->saved;
    }
    return asynSuccess;
  }
};
struct ecmcMotorRecordController : asynMotorController {
  motorRecord *startupMotorRecords_[ECMC_MAX_AXES] = {};
  bool startupCommRecoveryDone_[ECMC_MAX_AXES] = {};
  ecmcMotorRecordAxis *getAxis(int) { return &axis; }
  void bindStartupMotorRecord(int, motorRecord *);
  asynStatus doCallbacksGenericPointer(void *, int, int);
};
'''
tests = r'''
int main() {
  motorRecord r;
  MotorStatus sample;
  ecmcMotorRecordController c;
  c.record = &r;
  c.deferPositionLoad = true;
  c.bindStartupMotorRecord(1, &r);
  assert(!native.getMotorRecordStartupReady());
  // The original dispatch drops the restore while the startup alarm is latched.
  c.asynMotorController::doCallbacksGenericPointer(&sample, c.motorStatus_, 1);
  assert(c.queuedPositions == 0 && r.actual == 0);
  // A healthy ECMC sample recovers the stale alarm before the motor callback.
  c.doCallbacksGenericPointer(&sample, c.motorStatus_, 1);
  assert(c.queuedPositions == 1 && r.mip == MIP_LOAD_P);
  assert(!native.getMotorRecordStartupReady()); // Queued is not completed.
  assert(c.startupCommRecoveryDone_[1] && resets == 1 && events == 3);
  assert(!recordLocked);
  r.actual = 5;
  axis.waitingForStatusAfterCommand_ = true;
  c.doCallbacksGenericPointer(&sample, c.motorStatus_, 1);
  assert(!native.getMotorRecordStartupReady());
  r.mip = MIP_DONE; r.dmov = true;
  c.doCallbacksGenericPointer(&sample, c.motorStatus_, 1);
  assert(!native.getMotorRecordStartupReady()); // Need a subsequent RT sample.
  axis.waitingForStatusAfterCommand_ = false;
  axis.updateFirstPollDone_ = false;
  c.doCallbacksGenericPointer(&sample, c.motorStatus_, 1);
  assert(!native.getMotorRecordStartupReady()); // Limits must also be ready.
  axis.updateFirstPollDone_ = true;
  c.doCallbacksGenericPointer(&sample, c.motorStatus_, 1);
  assert(native.getMotorRecordStartupReady() && r.actual == 5);
  // Later runtime communication faults are not automatically cleared.
  r.stat = COMM_ALARM;
  c.doCallbacksGenericPointer(&sample, c.motorStatus_, 1);
  assert(r.stat == COMM_ALARM && resets == 1 && c.queuedPositions == 1);

  // No recovery for an invalid packet, faulting axis, or incomplete IOC startup.
  for (int fault = 0; fault < 12; ++fault) {
    ecmcMotorRecordController bad;
    motorRecord badRecord;
    MotorStatus badSample;
    bad.record = &badRecord;
    bad.bindStartupMotorRecord(1, &badRecord);
    native = NativeAxis();
    iocReady = true; faultOnLock = false;
    switch (fault) {
      case 0: badSample.positionWritten = false; break;
      case 1: badSample.position = std::numeric_limits<double>::quiet_NaN(); break;
      case 2: badSample.status = 1; break; // incoming communication fault
      case 3: badSample.status = 2; break; // incoming axis problem
      case 4: native.started = false; break;
      case 5: native.startup = true; break;
      case 6: native.blocked = true; break;
      case 7: native.error = 123; break;
      case 8: iocReady = false; break;
      case 9: faultOnLock = true; break; // fault while waiting for record lock
      case 10: bad.parameterStatus = asynError; break;
      case 11: bad.readParamFails = true; break;
    }
    int previousResets = resets;
    bad.doCallbacksGenericPointer(&badSample, bad.motorStatus_, 1);
    assert(badRecord.stat == COMM_ALARM && resets == previousResets);
    assert(!bad.startupCommRecoveryDone_[1] && !recordLocked);
  }
  faultOnLock = false; native = NativeAxis(); iocReady = true;

  // EPICS reset promotes pending unrelated alarms, rather than discarding them.
  for (bool pendingComm : {false, true}) {
    motorRecord other;
    if (pendingComm) { other.stat = STATE_ALARM; other.nsta = COMM_ALARM; other.sevr = 2; }
    else { other.nsta = STATE_ALARM; other.nsev = 2; }
    dbScanLock(&other);
    clearStartupCommunicationAlarm(&other);
    dbScanUnlock(&other);
    assert(other.stat == STATE_ALARM && other.sevr == 2);
  }
  // An active record is not altered; recovery remains pending for a later packet.
  ecmcMotorRecordController active;
  motorRecord activeRecord;
  active.record = &activeRecord;
  active.bindStartupMotorRecord(1, &activeRecord);
  activeRecord.pact = true;
  int previousResets = resets;
  active.doCallbacksGenericPointer(&sample, active.motorStatus_, 1);
  assert(!active.startupCommRecoveryDone_[1] && resets == previousResets);
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp, exe = Path(directory) / 'startup_comm.cpp', Path(directory) / 'startup_comm'
    cpp.write_text(stubs + implementation + tests)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O2',
                    '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Startup COMM recovery: saved position, health checks, other alarms, and runtime fault checks passed.')
