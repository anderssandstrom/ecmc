#!/usr/bin/env python3
"""Exercise production gate/producer dispatch and axis command guards without EPICS."""
# Expectation basis: DOC: startup hold/release contract; COMPAT: internal ordering.
# Rationale, sources and open questions: EXPECTATIONS.md
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1] / 'devEcmcSup'
main = (root / 'main/ecmcMainThread.cpp').read_text()
axis = (root / 'motion/ecmcAxisBase.cpp').read_text()
header = (root / 'motion/ecmcAxisBase.h').read_text()


def between(source, begin, end):
    start = source.index(begin)
    return source[start:source.index(end, start)]


def method(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


# Compile the real command guards. Unrelated runtime validation after each guard
# is represented by counters so an early request cannot be silently queued.
guards = between(axis, 'int ecmcAxisBase::setExecute(bool execute) {',
                 '  // Auto enable if needed') + '''
  if (execute) ++motionRequests;
  return 0;
}
'''
guards += between(axis, 'int ecmcAxisBase::setExecute(bool execute, bool ignoreBusy) {',
                  '  if (execute && data_.control_.command') + '''
  (void)ignoreBusy;
  if (execute) ++motionRequests;
  return 0;
}
'''
guards += between(axis, 'int ecmcAxisBase::setEnable(bool enable) {',
                  '  if (data_.status_.statusWord_.enable') + '''
  enabled = enable;
  return 0;
}
'''
guards += between(axis, 'void ecmcAxisBase::preExecute(bool masterOK) {',
                  '  if (ecmcStartupMotionHeld())') + '''
  (void)masterOK;
}
'''
startup_state = between(main, 'struct StartupState {', '}  // namespace')
release = between(main, '    if (startup.axisInitializationReleased && !startup.commandsReleased)',
                  '    const unsigned int globalMotionCommands')
producers = between(main, '    // Motion\n', '    // Enforce the global command')

stubs = r'''
#include <cassert>
#include <atomic>
constexpr int ERROR_AXIS_BUSY = 1, ECMC_MODE_CONFIG = 0;
std::atomic<bool> startupCommandsHeld {true};
bool ecmcStartupCommandsHeld() { return startupCommandsHeld.load(); }
void ecmcRtLoggerLogInfo(const char *) {}
int waitWarnings = 0;
void ecmcRtLoggerLogError(const char *, ...) { ++waitWarnings; }
struct ecmcAxisBase {
  std::atomic<bool> motorRecordStartupReady_ {true};
  bool inStartup = false, startupEnablePending_ = false, enabled = false;
  int motionRequests = 0, samples = 0;
  struct { struct { bool enableAtStartup = true; } control_; } data_;
  bool getInStartupPhase() { return inStartup; }
  void execute(bool ok) { ++samples; preExecute(ok); }
  void setMRCnen(bool) {}
  int setExecute(bool);
  int setExecute(bool, bool);
  int setEnable(bool);
  void preExecute(bool);
'''
stubs += method(header, 'bool getMotorRecordStartupReady() const')
stubs += method(header, 'void setMotorRecordStartupReady(bool ready)')
stubs += r'''
};
struct Producer {
  void updateDataInputHealth(int) {}
  int calls = 0;
  void executeRT(double) { ++calls; }
  void execute() { ++calls; }
  void execute(bool) { ++calls; }
  void execute(int, bool) { ++calls; }
  int exeRTFunc(int) { ++calls; return 0; }
};
struct Ethercat { bool timing = true; bool timingStartupReady() { return timing; } } bus;
Ethercat *ec = &bus;
bool ecInitDone = true, ecStat = true;
bool dataInputsConfigured = false;
constexpr int ECMC_MAX_PLCS = 16;
''' + startup_state + r'''
StartupState startup(true);
int i = 0, appModeCmd = 1;
double mcuPeriod = 1000000;
double ecTimeoutSeconds = 30;
std::atomic<bool> iocExitRequested {false};
ecmcAxisBase withRecord, withoutRecord;
Producer sequence, plc, pvt, slave, plugin, cpp;
Producer *pvtCtrl_ = &pvt, *plcs = &plc;
// Controlled dispatch membership; the production builder is tested in test_startup_gate.py.
struct {
  int activeAxisCount = 2;
  ecmcAxisBase *activeAxes[2] = {&withRecord, &withoutRecord};
  int activeAxisPlcId[2] = {0, 1};
  Producer *activeMotionSeqs[1] = {&sequence};
  Producer *activeMasterSlaves[1] = {&slave};
  Producer *activePlugins[1] = {&plugin};
  Producer *activeCppLogics[1] = {&cpp};
  int activeMotionSeqCount = 1, activeMasterSlaveCount = 1;
  int activePluginCount = 1, activeCppLogicCount = 1;
} runtime;
int controllerError = 0, pluginsError = 0, cppLogicError = 0;
'''
tests = r'''
int totalProducerCalls() {
  return sequence.calls + plc.calls + pvt.calls + slave.calls + plugin.calls + cpp.calls;
}
int main() {
  startup.axisInitializationReleased = true;
  // Only a loaded motor record registers a pending handshake. A native axis or
  // a driver axis with no loaded record starts ready and cannot deadlock startup.
  assert(withoutRecord.getMotorRecordStartupReady());
  withRecord.setMotorRecordStartupReady(false);
  withoutRecord.enabled = true; // A prior command cannot survive the hold.
  withRecord.startupEnablePending_ = true;
  ecTimeoutSeconds = .002;
  for (int n = 0; n < 3; ++n) {
    assert(withRecord.setEnable(true) == ERROR_AXIS_BUSY);
    assert(withRecord.setExecute(true) == ERROR_AXIS_BUSY);
    assert(withRecord.setExecute(true, true) == ERROR_AXIS_BUSY);
    assert(withRecord.setExecute(false) == 0);
    assert(withRecord.setEnable(false) == 0);
    tick();
    assert(!startup.commandsReleased && totalProducerCalls() == 0);
    assert(!withRecord.enabled && !withoutRecord.enabled);
  }
  assert(waitWarnings >= 2); // Timeout reports hold and blocking slot without release.
  assert(withRecord.samples == 3 && withRecord.motionRequests == 0);
  assert(withRecord.startupEnablePending_); // Preserve configured startup enable.
  withRecord.setMotorRecordStartupReady(true); // Full motor handshake finished.
  ecStat = false;
  tick();
  assert(!startup.commandsReleased && totalProducerCalls() == 0);
  ecStat = true; bus.timing = false;
  tick();
  assert(!startup.commandsReleased && totalProducerCalls() == 0);
  bus.timing = true;
  tick();
  assert(startup.commandsReleased && !ecmcStartupCommandsHeld());
  assert(sequence.calls == 1 && plc.calls == 3 && pvt.calls == 1);
  assert(slave.calls == 1 && plugin.calls == 1 && cpp.calls == 1);
  assert(withRecord.enabled && !withRecord.startupEnablePending_);
  assert(withRecord.motionRequests == 0); // No rejected motion was replayed.
  assert(withRecord.setExecute(true) == 0 && withRecord.motionRequests == 1);
  // No loaded motor records: axis initialization alone is sufficient.
  startup.commandsReleased = false; startupCommandsHeld = true;
  withRecord.inStartup = true;
  tick();
  assert(!startup.commandsReleased);
  withRecord.inStartup = false;
  tick();
  assert(startup.commandsReleased);
  // A runtime fault is handled normally; this startup gate never auto-resets it.
  ecStat = false;
  tick();
  assert(startup.commandsReleased);
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp, exe = Path(directory) / 'execution_gate.cpp', Path(directory) / 'execution_gate'
    cpp.write_text(stubs + guards + '\nvoid tick() {\n' + release + producers + '\n}\n' + tests)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O2',
                    '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Startup execution gate: producers, command rejection, restore handshake, and absent motor records passed.')
