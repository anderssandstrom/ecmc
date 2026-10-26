#!/usr/bin/env python3
"""Exercise production motor startup code with lightweight EPICS/asyn stand-ins."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'devEcmcSup/motor/ecmcMotorRecordAxis.cpp').read_text()


def method(signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


implementation = '\n'.join(method(signature) for signature in (
    'asynStatus ecmcMotorRecordAxis::syncEcmcSoftLimits(bool',
    'asynStatus ecmcMotorRecordAxis::syncMotorSoftLimits(bool',
    'asynStatus ecmcMotorRecordAxis::syncRestoredMotorSoftLimitsToEcmc()',
    'asynStatus ecmcMotorRecordAxis::initializeSoftLimits()',
    'asynStatus ecmcMotorRecordAxis::syncSoftLimitInterfaces(bool updateParams,',
    'asynStatus ecmcMotorRecordAxis::readEcmcAxisStatusData()',
))
# Run the real poll entry through startup initialization; subsequent motion/status
# reporting is outside this test's scope.
poll = method('asynStatus ecmcMotorRecordAxis::poll(bool *moving)')
implementation += '\n' + poll[:poll.index('  // Wait once for an RT sample')]
implementation += '\n  (void)moving; (void)timeBefore; return asynSuccess;\n}\n'

stubs = r'''
#include <cassert>
#include <cstring>
#include <map>
enum asynStatus { asynSuccess, asynError };
#define LOGINFO(...) ((void)0)
#define LOGERR(...) ((void)0)
#define motorWaitPollsBeforeReadyString 1
constexpr int ECMC_DATA_SOURCE_EXTERNAL = 1;
void *ecmcRTMutex = nullptr;
void epicsMutexLock(void *) {}
void epicsMutexUnlock(void *) {}
bool iocReady = false;
int ecmcIocRunning() { return iocReady; }
double ecmcMotorRecordgetNowTimeSecs() { return 0; }
struct ecmcAxisDataStatus { int errorCode = 0; };
struct Monitor {
  double low = -100, high = 100;
  bool lowEnabled = true, highEnabled = true;
  double getSoftLimitBwd() { return low; }
  double getSoftLimitFwd() { return high; }
  bool getEnableSoftLimitBwd() { return lowEnabled; }
  bool getEnableSoftLimitFwd() { return highEnabled; }
  bool getSafetyInterlock() { return false; }
  bool getAtTarget() { return false; }
  bool getEnableAtTargetMon() { return false; }
  bool getSumInterlock() { return false; }
};
struct Axis {
  Monitor monitor;
  bool rtStarted = false, startup = true, validStatus = true;
  ecmcAxisDataStatus status;
  Monitor *getMon() { return &monitor; }
  bool getRealTimeStarted() { return rtStarted; }
  bool getInStartupPhase() { return startup; }
  bool getBusy() { return false; }
  int getPrintDbg() { return 0; }
  bool getSoftLimitInterlock() { return false; }
  int getTrajDataSourceType() { return 0; }
  bool getMRIgnoreDisableStatusCheck() { return false; }
  ecmcAxisDataStatus *getAxisStatusDataPtr() {
    return validStatus ? &status : nullptr;
  }
};
struct Controller {
  enum { motorLowLimit_, motorHighLimit_, motorLowLimitRO_, motorHighLimitRO_,
         ecmcMotorRecordCfgDLLM_En_, ecmcMotorRecordCfgDLLM_,
         ecmcMotorRecordCfgDHLM_En_, ecmcMotorRecordCfgDHLM_,
         motorStop_, motorClosedLoop_ };
  std::map<int, double> values;
  asynStatus getDoubleParam(int, int key, double *value) {
    if (!values.count(key)) return asynError;
    *value = values[key]; return asynSuccess;
  }
  void callParamCallbacks() {}
};
struct asynMotorAxis {
  Controller *controller;
  void setDoubleParam(int key, double value) { controller->values[key] = value; }
  void setIntegerParam(int key, int value) { controller->values[key] = value; }
};
struct ecmcMotorRecordAxis : asynMotorAxis {
  Controller *pC_;
  int axisNo_ = 1, triggstop_ = 0, writes = 0;
  bool updateFirstPollDone_ = false, softLimitRestoreErrorLogged_ = false;
  bool interlockStopActive_ = false, sync = true, failWrite = false;
  bool failForwardWrite = false;
  struct {
    Axis *ecmcAxis;
    bool restoreMotorSoftlimits = true, axisInStartup = false;
    bool ecmcSafetyInterlock = false, ecmcBusy = false, ecmcAtTarget = false;
    bool ecmcAtTargetMonEnable = false, ecmcSummaryInterlock = false;
    bool ecmcSoftLimitInterlock = false, ecmcIgnoreDisableAxisStatus = false;
    int axisPrintDbg = 0, ecmcTrjSrc = 0, nErrorIdMcu = 0, axisId = 1;
    ecmcAxisDataStatus status_;
  } drvlocal;
  ecmcMotorRecordAxis(Controller &c, Axis &a) : pC_(&c) {
    controller = &c; drvlocal.ecmcAxis = &a;
  }
  bool isSoftLimitSyncEnabled() { return sync; }
  bool interlockStopRisingEdge(bool) { return false; }
  void updateError() {}
  void callParamCallbacks() {}
  int applyMotorSoftLimitChange(bool forward, double value) {
    ++writes;
    if (failWrite || (forward && failForwardWrite)) return 1;
    auto &mon = drvlocal.ecmcAxis->monitor;
    (forward ? mon.high : mon.low) = value;
    return 0;
  }
  asynStatus syncEcmcSoftLimits(bool, bool);
  asynStatus syncMotorSoftLimits(bool, bool);
  asynStatus syncRestoredMotorSoftLimitsToEcmc();
  asynStatus initializeSoftLimits();
  asynStatus syncSoftLimitInterfaces(bool update, bool seed = false);
  asynStatus readEcmcAxisStatusData();
  asynStatus poll(bool *);
};
'''

tests = r'''
int main() {
  Controller c;
  Axis a;
  ecmcMotorRecordAxis m(c, a);
  bool moving = false;
  // Constructor readback may refresh ECMC PVs, but must not seed motor limits.
  assert(m.syncSoftLimitInterfaces(true, true) == asynSuccess);
  assert(c.values[Controller::ecmcMotorRecordCfgDHLM_] == 100);
  assert(!c.values.count(Controller::motorHighLimit_));
  assert(!c.values.count(Controller::motorHighLimitRO_));

  // Before RT starts, even IOC-ready polls must not consume restoration.
  iocReady = true;
  assert(m.poll(&moving) == asynSuccess);
  assert(m.drvlocal.axisInStartup && !m.updateFirstPollDone_ && !m.writes);
  a.rtStarted = true;
  assert(m.poll(&moving) == asynSuccess);
  assert(!m.updateFirstPollDone_ && !m.writes);

  // Gate disabled/axis already started: still wait for IOC/autosave pass 1.
  a.startup = false;
  iocReady = false;
  c.values[Controller::motorLowLimit_] = -100;
  c.values[Controller::motorHighLimit_] = 100;
  assert(m.poll(&moving) == asynError);
  assert(!m.updateFirstPollDone_ && !m.writes);
  c.values[Controller::motorHighLimit_] = 150; // Actual user's saved value.
  iocReady = true;

  // Invalid status, disabled sync, and missing parameters must all retry.
  a.validStatus = false;
  assert(m.poll(&moving) == asynError);
  assert(!m.updateFirstPollDone_ && !m.writes);
  a.validStatus = true;
  m.sync = false;
  assert(m.poll(&moving) == asynError);
  assert(!m.updateFirstPollDone_ && !m.writes);
  m.sync = true;
  c.values.erase(Controller::motorHighLimit_);
  assert(m.poll(&moving) == asynError);
  assert(!m.updateFirstPollDone_ && !m.writes);
  c.values[Controller::motorHighLimit_] = 150;
  m.failWrite = true;
  assert(m.poll(&moving) == asynError);
  assert(!m.updateFirstPollDone_);
  m.syncSoftLimitInterfaces(true, true);
  assert(c.values[Controller::motorHighLimit_] == 150);
  assert(!c.values.count(Controller::motorHighLimitRO_));

  // Successful retry publishes the restored pair and enables normal sync.
  m.failWrite = false;
  m.failForwardWrite = true;
  assert(m.poll(&moving) == asynError);
  assert(!m.updateFirstPollDone_);
  assert(c.values[Controller::motorHighLimit_] == 150);
  m.failForwardWrite = false;
  assert(m.poll(&moving) == asynSuccess);
  assert(m.updateFirstPollDone_);
  assert(a.monitor.low == -100 && a.monitor.high == 150);
  assert(c.values[Controller::motorHighLimitRO_] == 150);
  int writes = m.writes;
  c.values[Controller::motorHighLimit_] = 175;
  assert(m.poll(&moving) == asynSuccess);
  assert(m.writes == writes); // Startup restore does not run again.
  a.monitor.high = 180;
  m.syncSoftLimitInterfaces(true);
  assert(c.values[Controller::motorHighLimitRO_] == 180);

  // Legacy behavior without restore still seeds YAML/controller limits.
  Controller legacyC;
  Axis legacyA;
  ecmcMotorRecordAxis legacy(legacyC, legacyA);
  legacy.drvlocal.restoreMotorSoftlimits = false;
  legacyA.rtStarted = true;
  legacyA.startup = false;
  iocReady = false;
  assert(legacy.poll(&moving) == asynSuccess);
  assert(legacy.updateFirstPollDone_ && !legacy.writes);
  assert(legacyC.values[Controller::motorHighLimit_] == 100);
  assert(legacyC.values[Controller::motorLowLimit_] == -100);
}
'''

with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / 'softlimit_restore.cpp'
    exe = Path(directory) / 'softlimit_restore'
    cpp.write_text(stubs + implementation + tests)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O2',
                    '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)],
                   check=True)
    subprocess.run([str(exe)], check=True)
print('Motor soft-limit restore: startup ordering, saved values, retries, and legacy checks passed.')
