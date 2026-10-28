#!/usr/bin/env python3
"""Compile the current startup gate and dispatch code with fake endpoints.

Run: python3 tests/test_startup_gate.py
No EPICS, EtherCAT master or hardware required. This checks cycle-by-cycle
decisions, including masterless operation, not actual scheduling or IOC hooks.
"""
# Expectation basis: DOC: startup gate contract; COMPAT: internal cycle/call boundaries.
# Rationale, sources and open questions: EXPECTATIONS.md
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'devEcmcSup/main/ecmcMainThread.cpp').read_text()


def between(start, end):
    begin = source.index(start)
    return source[begin:source.index(end, begin)]


initialization = between('  // Snapshot the configuration once per runtime entry.',
                         '  // Ownership belongs to this thread')
bus_status = between('    if (ecInitDone) {\n      ec->receive(',
                     '    // Master to master coms')
gate_and_dispatch = between('    if (!startupReleased',
                            '    if (counter) {')

stubs = r'''
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
std::atomic<bool> startupGateConfigured{true}, startupMotionHeld{true};
std::atomic<bool> startupCommandsHeld{true};
std::atomic<bool> iocRunning{false}, iocExitRequested{false};
const char *waitSetting = nullptr;
// Keep the test independent of the invoking shell's environment.
const char *testGetenv(const char *) { return waitSetting; }
#define getenv testGetenv
void ecmcRtLoggerLogInfo(const char *, ...) {}
void ecmcRtLoggerLogError(const char *, ...) {}
enum { ERROR_AXIS_HW_NOT_READY = 1, ERROR_AXIS_HARDWARE_STATUS_NOT_OK = 2 };
enum { ECMC_GLOBAL_CMD_STOP_ALL_AXES = 1, ECMC_GLOBAL_CMD_DISABLE_ALL_AXES = 2 };
enum { ECMC_MODE_CONFIG = 0 };
unsigned commands = 0;
unsigned ecmcTakeGlobalMotionCommands() { unsigned value = commands; commands = 0; return value; }
struct Axis {
  bool startup = true;
  bool motorReady = true; // No loaded motor record unless a scenario registers one.
  int error = 0, resets = 0, cycles = 0, stops = 0;
  int getErrorID() { return error; }
  void errorReset() { error = 0; ++resets; }
  bool getInStartupPhase() { return startup; }
  bool getMotorRecordStartupReady() { return motorReady; }
  void execute(int) { ++cycles; }
  void stopMotion(bool) { ++stops; }
};
struct Task {
  int calls = 0;
  void executeRT(double) { ++calls; }
  void execute() { ++calls; }
  int exeRTFunc(int) { ++calls; return 0; }
};
struct Plc {
  int axisCalls = 0, globalCalls = 0;
  void execute(int, int) { ++axisCalls; }
  void execute(int) { ++globalCalls; }
};
struct Ethercat {
  bool available = true, healthy = true, timing = true;
  int receives = 0, timingChecks = 0, resets = 0;
  void receive(int, int) { assert(available); ++receives; }
  void checkDomainsState() { assert(available); }
  int statusOK() { assert(available); return healthy; }
  bool timingStartupReady() { assert(available); ++timingChecks; return timing; }
  void resetStartupCommunicationErrors() { assert(available); ++resets; }
};
struct Rig {
  Axis axes[3];
  Axis *activeAxes[3] = {&axes[0], &axes[1], &axes[2]};
  int activeAxisCount = 3, activeAxisPlcId[3] = {0, 1, 2};
  Task sequence, pvt, masterSlave, plugin, cppLogic;
  Task *activeMotionSeqs[1] = {&sequence}, *pvtCtrl_ = &pvt;
  Task *activeMasterSlaves[1] = {&masterSlave}, *activePlugins[1] = {&plugin};
  Task *activeCppLogics[1] = {&cppLogic};
  int activeMotionSeqCount = 1, activeMasterSlaveCount = 1;
  int activePluginCount = 1, activeCppLogicCount = 1;
  Plc plc;
  Plc *plcs = &plc;
  Ethercat ethercat;
  Ethercat *ec = &ethercat;
  bool ecInitDone = true;
  int startTime = 0, masterActivationTimeOffset = 0;
  int controllerError = 0, pluginsError = 0, cppLogicError = 0, appModeCmd = 1;
  // Binary-exact period makes the 2-second boundary deterministic.
  double mcuPeriod = 250000000, ecTimeoutSeconds = 5;
  void expectDispatch(int count) {
    assert(sequence.calls == count && pvt.calls == count);
    assert(masterSlave.calls == count && plugin.calls == count && cppLogic.calls == count);
    assert(plc.globalCalls == count && plc.axisCalls == count * activeAxisCount);
  }
  void readyAxes() { for (auto &axis : axes) axis.startup = false; }
  void run(int scenario) {
'''

# Keep production locals and initialization intact by placing scenarios inside
# the same wrapper function; each call represents a new runtime entry.
body = initialization + r'''
    auto tick = [&]() {
      int i = 0, ecStat = 0;
''' + bus_status + gate_and_dispatch + r'''
    };
    auto ticks = [&](int count) { while (count--) tick(); };
    if (scenario == 0) {
      // IOC wait must not consume the EtherCAT startup timeout.
      ticks(40);
      assert(startupWaitSeconds == 0 && !startupTimedOut);
      expectDispatch(0);
      assert(axes[0].cycles == 40); // Axis initialization still runs.
      iocRunning = true;
      ticks(7);
      assert(!startupReleased && startupMotionHeld);
      ethercat.healthy = false;
      tick(); // Broken stability must restart the full two-second interval.
      ethercat.healthy = true;
      ticks(7);
      assert(!startupReleased);
      axes[0].error = ERROR_AXIS_HW_NOT_READY;
      axes[1].error = ERROR_AXIS_HARDWARE_STATUS_NOT_OK;
      axes[2].error = 99;
      tick();
      assert(startupReleased && !startupMotionHeld && !startupExecutionReady);
      assert(ethercat.resets == 1);
      assert(axes[0].resets == 1 && axes[1].resets == 1);
      assert(axes[2].error == 99 && axes[2].resets == 0);
      axes[0].startup = axes[1].startup = false;
      tick();
      expectDispatch(0); // Last axis still initializing.
      axes[2].startup = false;
      axes[1].motorReady = false;
      ticks(3);
      expectDispatch(0); // Native startup alone cannot release a pending restore.
      assert(startupCommandsHeld);
      axes[1].motorReady = true;
      tick();
      expectDispatch(1);
      assert(!startupCommandsHeld);
      tick();
      expectDispatch(2);
      assert(ethercat.resets == 1 && axes[0].resets == 1);
    } else if (scenario == 1) {
      iocRunning = true;
      ethercat.timing = false; // Bus healthy but timing discovery pending.
      ticks(19);
      assert(!startupTimedOut && !startupReleased);
      tick();
      assert(startupTimedOut && startupMotionHeld);
      ethercat.timing = true;
      readyAxes();
      ticks(12);
      assert(!startupReleased && !startupExecutionReady);
      expectDispatch(0); // Recovery requires a new runtime entry.
      assert(ethercat.resets == 0);
    } else if (scenario == 2) {
      // No master: skip every EtherCAT call, but preserve both readiness gates.
      ecInitDone = false;
      ethercat.available = false;
      ticks(20);
      assert(startupWaitSeconds == 0);
      iocRunning = true;
      ticks(7);
      assert(!startupReleased);
      tick();
      assert(startupReleased && !startupExecutionReady);
      expectDispatch(0);
      readyAxes();
      tick();
      expectDispatch(1);
      assert(ethercat.receives == 0 && ethercat.timingChecks == 0 && ethercat.resets == 0);
    } else if (scenario == 3) {
      // Explicit IOC-wait opt-out, also with no master or configured axes.
      ecInitDone = false;
      ethercat.available = false;
      activeAxisCount = 0;
      ticks(8);
      assert(!iocRunning && startupReleased && startupExecutionReady);
      expectDispatch(1);
    } else if (scenario == 4) {
      // Gate disabled retains immediate legacy dispatch.
      assert(!startupMotionHeld);
      assert(!startupCommandsHeld);
      tick();
      expectDispatch(1);
      assert(!iocRunning && startupWaitSeconds == 0);
    } else if (scenario == 5) {
      // A fresh runtime can recover following a timeout.
      assert(!startupTimedOut && startupWaitSeconds == 0 && startupMotionHeld);
      iocRunning = true;
      readyAxes();
      ticks(8);
      expectDispatch(1);
    }
  }
};
int main() {
  for (int scenario = 0; scenario < 6; ++scenario) {
    startupGateConfigured = scenario != 4;
    iocRunning = false;
    waitSetting = scenario == 3 ? "0" : nullptr;
    Rig rig;
    rig.run(scenario);
  }
  puts("Startup gate: IOC readiness, bus stability, timing timeout, axis readiness, "
       "dispatch, masterless operation, opt-outs, and runtime reentry passed.");
}
'''

with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / 'startup_gate.cpp'
    exe = Path(directory) / 'startup_gate'
    cpp.write_text(stubs + body)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11', '-O2',
                    '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)],
                   check=True)
    subprocess.run([str(exe)], check=True)
