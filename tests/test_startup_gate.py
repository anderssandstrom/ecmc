#!/usr/bin/env python3
"""Compile the current startup gate and dispatch code with fake endpoints.

Run: python3 tests/test_startup_gate.py
No EPICS, EtherCAT master or hardware required. This checks cycle-by-cycle
decisions, including masterless operation, not actual scheduling or IOC hooks.
Dispatch traces preserve current order (COMPAT), not an independent scheduler
specification. Locks, shared memory and endpoint internals are outside this fixture.
"""
# Expectation basis: DOC: startup gate contract; COMPAT: internal cycle/call boundaries.
# Rationale, sources and open questions: EXPECTATIONS.md
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'devEcmcSup/main/ecmcMainThread.cpp').read_text()


def between(start, end):
    begin = source.index(start)
    return source[begin:source.index(end, begin)]


master_identity = between('  int masterId ',
                          '  int diagnosticsUpdateFreqHzCached')
initialization = between('  // Snapshot the configuration once per runtime entry.',
                         '  // Ownership belongs to this thread')
bus_status = between('    if (ioActive) {\n      io.readInputs(',
                     '    // Master to master coms')
gate_and_dispatch = between('    if (!startup.axisInitializationReleased',
                            '  }  // enc of RT-loop')

# Compile the real membership snapshot with fake endpoint types. Constants and
# axis-to-PLC mapping come from production headers, not fixture duplicates.
runtime_objects = between('struct RuntimeObjects {', '// Mutable startup progress')
startup_state = between('struct StartupState {', '}  // namespace')
definitions = (ROOT / 'devEcmcSup/main/ecmcDefinitions.h').read_text()
sequence_header = (ROOT / 'devEcmcSup/sequence/ecmcMotionSequence.h').read_text()
constants = '\n'.join(re.findall(
    r'^#define (?:ECMC_MAX_(?:AXES|PLCS|PLUGINS|MST_SLVS_SMS|MOTION_SEQUENCES)\b|AXIS_PLC_ID_TO_PLC_ID\().*',
    definitions + '\n' + sequence_header, re.M))

stubs = r'''
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <ctime>
#include <string>
#include <vector>
std::vector<std::string> trace;
void expectTrace(std::initializer_list<const char *> expected) {
  const std::vector<std::string> wanted(expected.begin(), expected.end());
  if (trace != wanted) {
    fputs("Unexpected cycle trace:\n", stderr);
    for (const auto &event : trace) fprintf(stderr, "  %s\n", event.c_str());
    assert(trace == wanted);
  }
}
void printStatus() { trace.push_back("status"); }
void updateAsynParams(int) { trace.push_back("publish"); }
struct AsynPort { int getEpicsState() { return 14; } };
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
  const std::string name;
  explicit Axis(const char *id) : name(id) {}
  bool startup = true;
  bool motorReady = true; // No loaded motor record unless a scenario registers one.
  int error = 0, resets = 0, cycles = 0, stops = 0;
  int getErrorID() { return error; }
  void errorReset() { error = 0; ++resets; }
  bool getInStartupPhase() { return startup; }
  bool getMotorRecordStartupReady() { return motorReady; }
  void execute(int) { ++cycles; trace.push_back(name); }
  void stopMotion(bool disable) {
    ++stops;
    trace.push_back((disable ? "disable-" : "stop-") + name);
  }
  void slowExecute() { trace.push_back("slow-" + name); }
};
struct Task {
  const std::string name;
  int result;
  explicit Task(const char *id, int error = 0) : name(id), result(error) {}
  int calls = 0;
  void executeRT(double) { ++calls; trace.push_back(name); }
  void execute() { ++calls; trace.push_back(name); }
  int exeRTFunc(int) { ++calls; trace.push_back(name); return result; }
};
struct Plc {
  bool dataInputs = false;
  bool hasDataInputProducers() const { return dataInputs; }
  void updateDataInputHealth(int id) { trace.push_back("input-health-" + std::to_string(id)); }
  int axisCalls = 0, globalCalls = 0;
  void execute(int id, int) { ++axisCalls; trace.push_back("axis-plc-" + std::to_string(id)); }
  void execute(int) { ++globalCalls; trace.push_back("global-plc"); }
};
struct Ethercat {
  int masterIndex = 0;
  int getMasterIndex() { return masterIndex; }
  bool available = true, healthy = true, timing = true;
  int receives = 0, timingChecks = 0, resets = 0, activeChecks = 0;
  bool getInitDone() { ++activeChecks; return available; }
  void receive(timespec, timespec) { assert(available); ++receives; trace.push_back("receive"); }
  void checkDomainsState() { assert(available); }
  int statusOK() { assert(available); return healthy; }
  bool timingStartupReady() { assert(available); ++timingChecks; return timing; }
  void resetStartupCommunicationErrors() { assert(available); ++resets; }
  void checkState() { assert(available); trace.push_back("bus-state"); }
  void slowExecute() { assert(available); trace.push_back("slow-bus"); }
  void send(timespec) { assert(available); trace.push_back("send"); }
  void setTimeOffest(timespec) { trace.push_back("time-offset"); }
};
struct Simulation {
  bool healthy = true;
  bool isActive() const { return true; }
  void readInputs(timespec, timespec) { trace.push_back("sim-input"); }
  int updateIoStatus() { return healthy; }
  bool timingReady() const { return healthy; }
  void slowExecute() { trace.push_back("sim-slow"); }
  void writeOutputs(timespec) { trace.push_back("sim-output"); }
};
std::vector<Simulation *> configuredSimulations;
const std::vector<Simulation *> &ecmcSimulationDrivers() { return configuredSimulations; }
using ecmcEc = Ethercat;
#define ECMCEC_H_
#include "ecmcEthercatDriver.h"
''' + constants + r'''
using ecmcAxisBase = Axis;
using ecmcMasterSlaveStateMachine = Task;
using ecmcPluginLib = Task;
using ecmcCppLogicLib = Task;
using ecmcMotionSequence = Task;
''' + runtime_objects + startup_state + r'''
struct Rig {
  Axis axes[3] = {Axis("axis-2"), Axis("axis-7"), Axis("axis-11")};
  Task sequence{"sequence"}, pvt{"pvt"}, masterSlave{"master-slave"};
  Task plugin{"plugin", 17}, cppLogic{"cpp-logic", 23}, safety{"safety", 31};
  Task *safetyplugin = &safety;
  Task *pvtCtrl_ = &pvt;
  Axis *axisObjects[ECMC_MAX_AXES] = {};
  Task *masterSlaveObjects[ECMC_MAX_MST_SLVS_SMS] = {};
  Task *pluginObjects[ECMC_MAX_PLUGINS] = {};
  Task *cppLogicObjects[ECMC_MAX_PLUGINS] = {};
  Task *motionSequenceObjects[ECMC_MAX_MOTION_SEQUENCES] = {};
  RuntimeObjects runtime{axisObjects, masterSlaveObjects, pluginObjects,
                         cppLogicObjects, motionSequenceObjects};
  explicit Rig(int masterId = 0) {
    ethercat.masterIndex = masterId;
    ethercat.available = masterId >= 0;
    axisObjects[2] = &axes[0];
    axisObjects[7] = &axes[1];
    axisObjects[11] = &axes[2];
    masterSlaveObjects[3] = &masterSlave;
    pluginObjects[5] = &plugin;
    cppLogicObjects[7] = &cppLogic;
    motionSequenceObjects[9] = &sequence;
    runtime = RuntimeObjects(axisObjects, masterSlaveObjects, pluginObjects,
                             cppLogicObjects, motionSequenceObjects);
  }
  Simulation simulated;
  Plc plc;
  Plc *plcs = &plc;
  Ethercat ethercat;
  Ethercat *ec = &ethercat;
  timespec startTime = {}, sendTime = {}, endTime = {};
  timespec masterActivationTimeOffset = {};
  int counter = 0, diagnosticsUpdateFreqHz = 0;
  int diagnosticsUpdateFreqHzCached = -1, slowCycleInterval = 1;
  double mcuFrequency = 4;
  AsynPort port;
  AsynPort *localAsynPort = &port;
  int safetypluginError = 0;
  int controllerError = 0, pluginsError = 0, cppLogicError = 0, appModeCmd = 1;
  // Binary-exact period makes the 2-second boundary deterministic.
  double mcuPeriod = 250000000, ecTimeoutSeconds = 5;
  void expectDispatch(int count) {
    assert(sequence.calls == count && pvt.calls == count);
    assert(masterSlave.calls == count && plugin.calls == count && cppLogic.calls == count);
    assert(plc.globalCalls == count && plc.axisCalls == count * runtime.activeAxisCount);
  }
  void readyAxes() { for (auto &axis : axes) axis.startup = false; }
  void run(int scenario) {
'''

# Keep production locals and initialization intact by placing scenarios inside
# the same wrapper function; each call represents a new runtime entry.
body = master_identity + initialization + r'''
    // COMPAT: each runtime entry starts with fresh progress, including after
    // a timed-out invocation; disabling the gate releases both stages at once.
    assert(!startup.timedOut && startup.waitSeconds == 0 &&
           startup.stableSeconds == 0 && startup.motorWaitSeconds == 0);
    assert(startup.axisInitializationReleased == !startupGateEnabled &&
           startup.commandsReleased == !startupGateEnabled);
    auto tick = [&]() {
      trace.clear();
      int i = 0, ecStat = 0;
      const int previousActiveChecks = ethercat.activeChecks;
      const bool ioActive = io.isActive();
''' + bus_status + gate_and_dispatch + r'''
      assert(ethercat.activeChecks == previousActiveChecks + 1);
    };
    auto ticks = [&](int count) { while (count--) tick(); };
    if (scenario == 0) {
      // IOC wait must not consume the EtherCAT startup timeout.
      ticks(40);
      assert(startup.waitSeconds == 0 && !startup.timedOut);
      expectDispatch(0);
      assert(axes[0].cycles == 40); // Axis initialization still runs.
      iocRunning = true;
      ticks(7);
      assert(!startup.axisInitializationReleased && startupMotionHeld);
      ethercat.healthy = false;
      tick(); // Broken stability must restart the full two-second interval.
      ethercat.healthy = true;
      ticks(7);
      assert(!startup.axisInitializationReleased);
      axes[0].error = ERROR_AXIS_HW_NOT_READY;
      axes[1].error = ERROR_AXIS_HARDWARE_STATUS_NOT_OK;
      axes[2].error = 99;
      tick();
      assert(startup.axisInitializationReleased && !startupMotionHeld && !startup.commandsReleased);
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
      assert(!startup.timedOut && !startup.axisInitializationReleased);
      tick();
      assert(startup.timedOut && startupMotionHeld);
      ethercat.timing = true;
      readyAxes();
      ticks(12);
      assert(!startup.axisInitializationReleased && !startup.commandsReleased);
      expectDispatch(0); // Recovery requires a new runtime entry.
      assert(ethercat.resets == 0);
    } else if (scenario == 2) {
      // MASTER_ID=-1: skip bus I/O, preserving readiness and PLC timing.
      assert(masterId == -1 && !hasRealMaster && !ethercat.available);
      ticks(20);
      assert(startup.waitSeconds == 0);
      iocRunning = true;
      ticks(7);
      assert(!startup.axisInitializationReleased);
      tick();
      assert(startup.axisInitializationReleased && !startup.commandsReleased);
      expectDispatch(0);
      expectTrace({"axis-2", "axis-7", "axis-11", "safety", "publish", "time-offset"});
      readyAxes();
      diagnosticsUpdateFreqHz = 1;
      tick();
      expectDispatch(1);
      // COMPAT: masterless dispatch and slow diagnostics retain their order;
      // receive/send and bus diagnostics are skipped, timing offset still updates.
      expectTrace({"sequence", "axis-plc-18", "axis-2", "axis-plc-23", "axis-7",
                   "axis-plc-27", "axis-11", "pvt", "master-slave", "plugin",
                   "cpp-logic", "global-plc", "status", "slow-axis-2", "slow-axis-7",
                   "slow-axis-11", "safety", "publish", "time-offset"});
      assert(ethercat.receives == 0 && ethercat.timingChecks == 0 && ethercat.resets == 0);
    } else if (scenario == 3) {
      // Explicit IOC-wait opt-out, also with MASTER_ID=-1 and no axes.
      assert(masterId == -1 && !hasRealMaster && !ethercat.available);
      runtime.activeAxisCount = 0;
      ticks(8);
      assert(!iocRunning && startup.axisInitializationReleased && startup.commandsReleased);
      expectDispatch(1);
    } else if (scenario == 4) {
      // Gate disabled retains immediate legacy dispatch.
      assert(!startupMotionHeld);
      assert(!startupCommandsHeld);
      tick();
      expectDispatch(1);
      assert(!iocRunning && startup.waitSeconds == 0);
    } else if (scenario == 5) {
      // A fresh runtime can recover following a timeout.
      assert(!startup.timedOut && startup.waitSeconds == 0 && startupMotionHeld);
      iocRunning = true;
      readyAxes();
      ticks(8);
      expectDispatch(1);
    } else if (scenario == 6) {
      // COMPAT: explicit sequence, sparse axis/PLC association, diagnostics,
      // safety and publication placement before send. Expected order is
      // written independently of the extracted production dispatch.
      iocRunning = true;
      readyAxes();
      ticks(8);
      diagnosticsUpdateFreqHz = 1;
      tick();
      expectTrace({"receive", "sequence", "axis-plc-18", "axis-2",
                   "axis-plc-23", "axis-7", "axis-plc-27", "axis-11",
                   "pvt", "master-slave", "plugin", "cpp-logic", "global-plc",
                   "bus-state", "status", "slow-axis-2", "slow-axis-7",
                   "slow-axis-11", "slow-bus", "safety", "publish", "send"});
      assert(pluginsError == 17 && cppLogicError == 23 && safetypluginError == 31);
    } else if (scenario == 7) {
      // COMPAT: stop is consumed once and enforced on both sides of ordinary
      // producers, even while startup is held; safety remains active.
      commands = ECMC_GLOBAL_CMD_STOP_ALL_AXES;
      tick();
      expectTrace({"receive", "stop-axis-2", "stop-axis-7", "stop-axis-11",
                   "axis-2", "axis-7", "axis-11",
                   "stop-axis-2", "stop-axis-7", "stop-axis-11",
                   "safety", "publish", "send"});
      assert(commands == 0 && startupCommandsHeld);
      expectDispatch(0);
      tick();
      expectTrace({"receive", "axis-2", "axis-7", "axis-11",
                   "safety", "publish", "send"});
    } else if (scenario == 8) {
      // COMPAT: disable implies stop, surrounding all ordinary producers.
      commands = ECMC_GLOBAL_CMD_DISABLE_ALL_AXES;
      tick();
      expectTrace({"receive", "sequence", "disable-axis-2", "disable-axis-7",
                   "disable-axis-11", "axis-plc-18", "axis-2", "axis-plc-23",
                   "axis-7", "axis-plc-27", "axis-11", "pvt", "master-slave",
                   "plugin", "cpp-logic", "global-plc", "disable-axis-2",
                   "disable-axis-7", "disable-axis-11", "safety", "publish", "send"});
      assert(commands == 0);
      expectDispatch(1);
    } else if (scenario == 9) {
      // Common-input health is refreshed after each axis PLC, before that axis.
      tick();
      expectTrace({"receive", "sequence", "axis-plc-18", "input-health-2", "axis-2",
                   "axis-plc-23", "input-health-7", "axis-7", "axis-plc-27",
                   "input-health-11", "axis-11", "pvt", "master-slave", "plugin",
                   "cpp-logic", "global-plc", "safety", "publish", "send"});
    } else if (scenario == 10) {
      // Masterless startup still services native input health, without running
      // held PLC producers or invoking any EtherCAT receive/send operation.
      tick();
      expectTrace({"input-health-2", "axis-2", "input-health-7", "axis-7",
                   "input-health-11", "axis-11", "safety", "publish", "time-offset"});
      expectDispatch(0);
    } else if (scenario == 11) {
      // Preserve the distinction between MASTER_ID=-1 and an uninitialized
      // real master: neither performs bus I/O, only masterless updates offset.
      ethercat.available = false;
      tick();
      expectTrace({"axis-2", "axis-7", "axis-11", "safety", "publish"});
      assert(ethercat.receives == 0 && ethercat.timingChecks == 0);
      expectDispatch(0);
    } else if (scenario == 12) {
      tick();
      expectTrace({"receive", "sim-input", "sequence", "axis-plc-18", "axis-2", "axis-plc-23", "axis-7",
        "axis-plc-27", "axis-11", "pvt", "master-slave", "plugin", "cpp-logic", "global-plc",
        "safety", "publish", "send", "sim-output"});
      expectDispatch(1);
    } else if (scenario == 13) {
      simulated.healthy = false;
      tick();
      expectTrace({"sim-input", "axis-2", "axis-7", "axis-11", "safety", "publish", "time-offset", "sim-output"});
      expectDispatch(0);
      iocRunning = true; ticks(10);
      assert(!startup.axisInitializationReleased && !startup.commandsReleased);
    }
  }
};
// COMPAT: empty, sparse and full membership; source order/axis PLC mapping;
// snapshot lifetime and fresh membership on a later runtime entry. These checks
// exercise the production constructor, not a fixture implementation of it.
void checkRuntimeObjects() {
  Rig rig;
  const auto &sparse = rig.runtime;
  assert(sparse.activeAxisCount == 3);
  assert(sparse.activeAxes[0] == &rig.axes[0]);
  assert(sparse.activeAxes[1] == &rig.axes[1]);
  assert(sparse.activeAxes[2] == &rig.axes[2]);
  assert(sparse.activeAxisPlcId[0] == 18 && sparse.activeAxisPlcId[1] == 23 &&
         sparse.activeAxisPlcId[2] == 27);
  assert(sparse.activeMasterSlaveCount == 1 && sparse.activeMasterSlaves[0] == &rig.masterSlave);
  assert(sparse.activePluginCount == 1 && sparse.activePlugins[0] == &rig.plugin);
  assert(sparse.activeCppLogicCount == 1 && sparse.activeCppLogics[0] == &rig.cppLogic);
  assert(sparse.activeMotionSeqCount == 1 && sparse.activeMotionSeqs[0] == &rig.sequence);

  Axis *axisObjects[ECMC_MAX_AXES] = {};
  Task *masterSlaveObjects[ECMC_MAX_MST_SLVS_SMS] = {};
  Task *pluginObjects[ECMC_MAX_PLUGINS] = {};
  Task *cppLogicObjects[ECMC_MAX_PLUGINS] = {};
  Task *motionSequenceObjects[ECMC_MAX_MOTION_SEQUENCES] = {};
  const RuntimeObjects empty(axisObjects, masterSlaveObjects, pluginObjects,
                             cppLogicObjects, motionSequenceObjects);
  assert(empty.activeAxisCount == 0 && empty.activeMasterSlaveCount == 0 &&
         empty.activePluginCount == 0 && empty.activeCppLogicCount == 0 &&
         empty.activeMotionSeqCount == 0);
  Task alternate{"alternate"};
  for (int i = 0; i < ECMC_MAX_AXES; ++i) axisObjects[i] = &rig.axes[i % 3];
  for (int i = 0; i < ECMC_MAX_MST_SLVS_SMS; ++i)
    masterSlaveObjects[i] = i % 2 ? &alternate : &rig.masterSlave;
  for (int i = 0; i < ECMC_MAX_PLUGINS; ++i) {
    pluginObjects[i] = i % 2 ? &alternate : &rig.plugin;
    cppLogicObjects[i] = i % 2 ? &alternate : &rig.cppLogic;
  }
  for (int i = 0; i < ECMC_MAX_MOTION_SEQUENCES; ++i)
    motionSequenceObjects[i] = i % 2 ? &alternate : &rig.sequence;
  const RuntimeObjects full(axisObjects, masterSlaveObjects, pluginObjects,
                            cppLogicObjects, motionSequenceObjects);
  assert(full.activeAxisCount == ECMC_MAX_AXES);
  assert(full.activeMasterSlaveCount == ECMC_MAX_MST_SLVS_SMS);
  assert(full.activePluginCount == ECMC_MAX_PLUGINS && full.activeCppLogicCount == ECMC_MAX_PLUGINS);
  assert(full.activeMotionSeqCount == ECMC_MAX_MOTION_SEQUENCES);
  for (int i = 0; i < ECMC_MAX_AXES; ++i) {
    assert(full.activeAxes[i] == &rig.axes[i % 3]);
    assert(full.activeAxisPlcId[i] == i + ECMC_MAX_PLCS);
  }
  for (int i = 0; i < ECMC_MAX_MST_SLVS_SMS; ++i)
    assert(full.activeMasterSlaves[i] == (i % 2 ? &alternate : &rig.masterSlave));
  for (int i = 0; i < ECMC_MAX_PLUGINS; ++i) {
    assert(full.activePlugins[i] == (i % 2 ? &alternate : &rig.plugin));
    assert(full.activeCppLogics[i] == (i % 2 ? &alternate : &rig.cppLogic));
  }
  for (int i = 0; i < ECMC_MAX_MOTION_SEQUENCES; ++i)
    assert(full.activeMotionSeqs[i] == (i % 2 ? &alternate : &rig.sequence));

  // Membership is copied, while pointed-to state remains live. This models
  // source edits between runtime entries, not permission to edit topology in RT.
  rig.axisObjects[2] = nullptr;
  rig.pluginObjects[5] = &alternate;
  rig.plugin.result = 99;
  assert(sparse.activeAxes[0] == &rig.axes[0]);
  assert(sparse.activePlugins[0] == &rig.plugin && sparse.activePlugins[0]->result == 99);
  const RuntimeObjects next(rig.axisObjects, rig.masterSlaveObjects, rig.pluginObjects,
                            rig.cppLogicObjects, rig.motionSequenceObjects);
  assert(next.activeAxisCount == 2 && next.activeAxes[0] == &rig.axes[1]);
  assert(next.activeAxisPlcId[0] == 23 && next.activeAxisPlcId[1] == 27);
  assert(next.activePluginCount == 1 && next.activePlugins[0] == &alternate);
}
int main() {
  checkRuntimeObjects();
  for (int scenario = 0; scenario < 14; ++scenario) {
    startupGateConfigured = scenario != 4 && scenario != 8 && scenario != 9 && scenario != 12;
    iocRunning = false;
    iocExitRequested = false;
    commands = 0;
    waitSetting = scenario == 3 ? "0" : nullptr;
    Rig rig(scenario == 2 || scenario == 3 || scenario == 10 || scenario == 13 ? -1 : 0);
    configuredSimulations.clear();
    if (scenario == 12 || scenario == 13) configuredSimulations.push_back(&rig.simulated);
    rig.plc.dataInputs = scenario == 9 || scenario == 10;
    rig.run(scenario);
  }
  puts("Startup gate: IOC readiness, bus stability, timing timeout, axis readiness, "
       "runtime membership, dispatch order, stop/disable enforcement, safety/publication/send, "
       "masterless operation, opt-outs, and runtime reentry passed.");
}
'''

with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / 'startup_gate.cpp'
    exe = Path(directory) / 'startup_gate'
    cpp.write_text(stubs + body)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11', '-O2',
                    '-I' + str(ROOT / 'devEcmcSup/com'),
                    '-I' + str(ROOT / 'devEcmcSup/ethercat'),
                    '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)],
                   check=True)
    subprocess.run([str(exe)], check=True)
