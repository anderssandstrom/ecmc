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
group = (root / 'motion/ecmcAxisGroup.cpp').read_text()
master_slave = (root / 'motion/ecmcMasterSlaveStateMachine.cpp').read_text()


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
#include <vector>
#include <cstring>
constexpr int ERROR_AXIS_BUSY = 1, ECMC_MODE_CONFIG = 0;
std::atomic<bool> startupCommandsHeld {true};
std::atomic<int> startupSharedBlockingAxis {-1};
bool ecmcStartupCommandsHeld() { return startupCommandsHeld.load(); }
void ecmcRtLoggerLogInfo(const char *) {}
int waitWarnings = 0;
void ecmcRtLoggerLogError(const char *, ...) { ++waitWarnings; }
struct ecmcAxisBase {
  std::atomic<bool> motorRecordStartupReady_ {true};
  std::atomic<bool> startupCommandsReleased_ {false};
  std::atomic<const char *> startupHoldReason_ {"waiting for IOC/bus stabilization"};
  std::atomic<int> startupBlockingAxis_ {-1}, startupBlockingGroup_ {-1};
  const char *getStartupHoldReason() const;
  int getStartupBlockingAxis() const;
  int getStartupBlockingGroup() const;
  int id = 0;
  int getAxisID() { return id; }
  bool getStartupCommandsHeld() const;
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
stubs += method(header, 'void setStartupHoldReason(const char *reason, int axis = -1, int group = -1)')
stubs += method(header, 'void setStartupCommandsReleased(bool released)')
stubs += method(header, 'bool getMotorRecordStartupReady() const')
stubs += method(header, 'void setMotorRecordStartupReady(bool ready)')
stubs += r'''
};
struct ecmcAxisGroup {
  std::vector<ecmcAxisBase *> axes_;
  bool getStartupReady() const;
  int getStartupBlockingAxis() const;
  bool inGroup(int id) { for (auto *axis : axes_) if (axis->getAxisID() == id) return true; return false; }
};
struct ecmcMasterSlaveStateMachine {
  ecmcAxisGroup *masterGrp_, *slaveGrp_;
  int calls = 0;
  int getIndex() const { return 2; }
  bool getStartupReady() const;
  int getStartupBlockingAxis() const;
  bool isMasterAxis(int id) const;
  void execute() { ++calls; }
};
struct Producer {
  void updateDataInputHealth(int) {}
  bool getStartupReady() const { return false; }
  bool isMasterAxis(int) const { return false; }
  int calls = 0, globalCalls = 0, axisCalls[5] = {};
  void executeRT(double) { ++calls; }
  void execute() { ++calls; }
  void execute(bool) { ++calls; ++globalCalls; }
  void execute(int id, bool) { ++calls; ++axisCalls[id]; }
  int exeRTFunc(int) { ++calls; return 0; }
};
struct IoDriver { bool timing = true; bool timingReady() { return timing; } } io;
bool ioActive = true, ecStat = true, simulationReady = true;
bool dataInputsConfigured = false;
constexpr int ECMC_MAX_PLCS = 16, ECMC_MAX_MST_SLVS_SMS = 16;
''' + startup_state + r'''
StartupState startup(true);
int i = 0, appModeCmd = 1;
double mcuPeriod = 1000000;
double ecTimeoutSeconds = 30;
std::atomic<bool> iocExitRequested {false};
ecmcAxisBase withRecord, withoutRecord;
Producer sequence, plc, pvt, plugin, cpp;
ecmcAxisGroup masterGroup, physicalGroup{{&withRecord,&withoutRecord}};
ecmcMasterSlaveStateMachine slave{&masterGroup,&physicalGroup};
Producer *pvtCtrl_ = &pvt, *plcs = &plc;
// Controlled dispatch membership; the production builder is tested in test_startup_gate.py.
struct {
  int activeAxisCount = 2;
  ecmcAxisBase *activeAxes[5] = {&withRecord, &withoutRecord};
  int activeAxisPlcId[5] = {0, 1, 2, 3, 4};
  Producer *activeMotionSeqs[1] = {&sequence};
  ecmcMasterSlaveStateMachine *activeMasterSlaves[2] = {&slave};
  Producer *activePlugins[1] = {&plugin};
  Producer *activeCppLogics[1] = {&cpp};
  int activeMotionSeqCount = 1, activeMasterSlaveCount = 1;
  int activePluginCount = 1, activeCppLogicCount = 1;
} runtime;
int controllerError = 0, pluginsError = 0, cppLogicError = 0;
'''
tests = r'''
int totalProducerCalls() {
  return sequence.calls + plc.globalCalls + pvt.calls + slave.calls + plugin.calls + cpp.calls;
}
int main() {
  startup.axisInitializationReleased = true;
  // Only a loaded motor record registers a pending handshake. A native axis or
  // a driver axis with no loaded record starts ready and cannot deadlock startup.
  assert(withoutRecord.getMotorRecordStartupReady());
  withRecord.setMotorRecordStartupReady(false);
  withoutRecord.id = 1;
  withRecord.enabled = true; // A prior command cannot survive its own hold.
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
    assert(!withRecord.enabled);
    assert(!withoutRecord.getStartupCommandsHeld());
    assert(withoutRecord.setEnable(true) == 0);
    assert(withoutRecord.setExecute(true) == 0);
  }
  assert(!strcmp(withRecord.getStartupHoldReason(),"waiting for motor-record restore"));
  assert(withRecord.getStartupBlockingAxis() == 0);
  assert(!strcmp(withoutRecord.getStartupHoldReason(),"released"));
  assert(withoutRecord.getStartupBlockingAxis() == -1);
  assert(plc.axisCalls[0] == 0 && plc.axisCalls[1] == 3);
  assert(waitWarnings >= 2); // Timeout reports hold and blocking slot without release.
  assert(withRecord.samples == 3 && withRecord.motionRequests == 0);
  assert(withRecord.startupEnablePending_); // Preserve configured startup enable.
  withRecord.setMotorRecordStartupReady(true); // Full motor handshake finished.
  ecStat = false;
  tick();
  assert(!startup.commandsReleased && totalProducerCalls() == 0);
  ecStat = true; io.timing = false;
  tick();
  assert(!startup.commandsReleased && totalProducerCalls() == 0);
  io.timing = true;
  tick();
  assert(startup.commandsReleased && !ecmcStartupCommandsHeld());
  assert(sequence.calls == 1 && plc.globalCalls == 1 && pvt.calls == 1);
  assert(slave.calls == 1 && plugin.calls == 1 && cpp.calls == 1);
  assert(withRecord.enabled && !withRecord.startupEnablePending_);
  assert(withRecord.motionRequests == 0); // No rejected motion was replayed.
  assert(withRecord.setExecute(true) == 0 && withRecord.motionRequests == 1);
  // No loaded motor records: axis initialization alone is sufficient.
  withRecord.setStartupCommandsReleased(false);
  withoutRecord.setStartupCommandsReleased(false);
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
  // Mirror-like ownership: one physical axis may run while another waits.
  // Only the virtual master waits for both. A separate ready group also runs.
  ecmcAxisBase virtualAxis, otherMaster, otherPhysical;
  virtualAxis.id = 2; otherMaster.id = 3; otherPhysical.id = 4;
  ecmcAxisGroup otherMasters{{&otherMaster}}, otherSlaves{{&otherPhysical}};
  ecmcMasterSlaveStateMachine otherGroup{&otherMasters,&otherSlaves};
  masterGroup.axes_.push_back(&virtualAxis);
  runtime.activeAxes[2] = &virtualAxis; runtime.activeAxes[3] = &otherMaster;
  runtime.activeAxes[4] = &otherPhysical; runtime.activeAxisCount = 5;
  runtime.activeMasterSlaves[1] = &otherGroup; runtime.activeMasterSlaveCount = 2;
  for (auto *axis : runtime.activeAxes) axis->setStartupCommandsReleased(false);
  startupCommandsHeld = true; startup = StartupState(true);
  startup.axisInitializationReleased = true; ecStat = true;
  withRecord.inStartup = true;
  const int oldShared = totalProducerCalls(), oldGroup = slave.calls;
  tick();
  assert(!startup.commandsReleased && totalProducerCalls() == oldShared);
  assert(!withoutRecord.getStartupCommandsHeld() && withoutRecord.setEnable(true) == 0);
  assert(!withRecord.enabled && withRecord.getStartupCommandsHeld());
  assert(virtualAxis.getStartupCommandsHeld() && virtualAxis.setEnable(true) == ERROR_AXIS_BUSY);
  assert(!strcmp(virtualAxis.getStartupHoldReason(),"waiting for master/slave group startup"));
  assert(virtualAxis.getStartupBlockingAxis() == 0 && virtualAxis.getStartupBlockingGroup() == 2);
  assert(slave.calls == oldGroup && otherGroup.calls == 1);
  assert(!otherMaster.getStartupCommandsHeld() && !otherPhysical.getStartupCommandsHeld());
  withRecord.inStartup = false;
  tick();
  assert(startup.commandsReleased && !virtualAxis.getStartupCommandsHeld());
  assert(virtualAxis.getStartupBlockingAxis() == -1 && virtualAxis.getStartupBlockingGroup() == -1);
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp, exe = Path(directory) / 'execution_gate.cpp', Path(directory) / 'execution_gate'
    logic = ''.join(method(axis, signature) for signature in (
        'bool ecmcAxisBase::getStartupCommandsHeld() const',
        'const char *ecmcAxisBase::getStartupHoldReason() const',
        'int ecmcAxisBase::getStartupBlockingAxis() const',
        'int ecmcAxisBase::getStartupBlockingGroup() const'))
    logic += method(group, 'bool ecmcAxisGroup::getStartupReady() const')
    logic += method(group, 'int ecmcAxisGroup::getStartupBlockingAxis() const')
    logic += method(master_slave, 'bool ecmcMasterSlaveStateMachine::getStartupReady() const')
    logic += method(master_slave, 'int ecmcMasterSlaveStateMachine::getStartupBlockingAxis() const')
    logic += method(master_slave, 'bool ecmcMasterSlaveStateMachine::isMasterAxis(int axisIndex) const')
    cpp.write_text(stubs + logic + guards + '\nvoid tick() {\n' + release + producers + '\n}\n' + tests)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O2',
                    '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Startup execution gate: producers, command rejection, restore handshake, and absent motor records passed.')
