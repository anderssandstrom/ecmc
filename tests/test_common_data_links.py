#!/usr/bin/env python3
"""Exercise real common access and PLC binding lifecycle with fake producers.

Actual encoder/monitor input processing is additionally exercised by test_monitor.
No IOC scheduler, EtherLab transport or physical stop is represented here.
"""
# DOC: common-data-links.md; COMPAT: native entry storage/publication semantics.
from pathlib import Path
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
SUP = ROOT / 'devEcmcSup'


def method(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


axis_source = (SUP / 'motion/ecmcAxisBase.cpp').read_text()
main = (SUP / 'plc/ecmcPLCMain.cpp').read_text()
header = (SUP / 'plc/ecmcPLCMain.h').read_text()
task = (SUP / 'plc/ecmcPLCTask.cpp').read_text()
task_header = (SUP / 'plc/ecmcPLCTask.h').read_text()
general = (SUP / 'main/ecmcGeneral.cpp').read_text()
utils = (SUP / 'com/ecmcAsynPortDriverUtils.cpp').read_text()
parser = (SUP / 'com/ecmcCmdParser.c').read_text()
checks = "#define ERROR_PLC_SOURCE_INVALID 0x20603\n" + header[header.index('#define ERROR_PLCS_INDEX_OUT_OF_RANGE'):header.index('  struct Alias')]
fields = header[header.index('  struct DataInputLink {'):header.index('  int          globalVariableCount_;')]
fixture = r'''
#include <cassert>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include <limits>
#include <map>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include "ecmcDataLink.h"
#include "ecmcSimulationDriver.h"
#include "ecmcAxisData.h"
#include "ecmcErrorsList.h"
#include "ecmcPLCTaskErrors.h"
// Minimal provider for compiling the actual historical compatibility header.
class ecmcEcEntry {
public:
  std::shared_ptr<ecmcDataAccess> view;
  std::shared_ptr<ecmcDataAccess> getDataAccess() { return view; }
};
struct ecmcEcSlave {
  std::map<std::string, ecmcEcEntry *> entries;
  ecmcEcEntry *findEntry(std::string name) {
    auto it = entries.find(name); return it == entries.end() ? nullptr : it->second;
  }
};
struct ecmcEc {
  int master = 0;
  ecmcEcSlave *slave = nullptr, *sim = nullptr;
  int getMasterIndex() { return master; }
  ecmcEcSlave *findSlave(int position) { return position == 3 ? slave : nullptr; }
  ecmcEcSlave *getSlave(int position) { return position == -1 ? sim : nullptr; }
};
ecmcEc *ec = nullptr;
std::map<int, ecmcSimulationDriver *> simulationRegistry;
ecmcSimulationDriver *ecmcFindSimulation(int id) {
  auto found = simulationRegistry.find(id);
  return found == simulationRegistry.end() ? nullptr : found->second;
}
#define ECMCECENTRY_H_
#include "ecmcEcEntryLink.h"
extern app_mode_type appModeStat;
app_mode_type appModeCmd = ECMC_MODE_CONFIG;
int allowCallbackEpicsState = 1;
void ecmcRtLoggerLogError(const char *, ...);
struct SourceObjects : ecmcDataLink {
  double getTargetVel() { return 11; }
  double getAcc() { return 12; }
  double getDec() { return 13; }
  double getSoftLimitBwd() { return -14; }
  double getSoftLimitFwd() { return 14; }
  bool getEnableSoftLimitBwd() { return true; }
  bool getEnableSoftLimitFwd() { return false; }
  double getHomePosition() { return 15; }
  double getKp() { return 16; }
  double getKi() { return 17; }
  double getKd() { return 18; }
  double getKff() { return 19; }
};
struct ecmcAxisBase {
  struct { ecmcAxisDataStatus status_ = {}; } data_;
  SourceObjects objects;
  SourceObjects *getTraj() { return &objects; }
  SourceObjects *getSeq() { return &objects; }
  SourceObjects *getCntrl() { return &objects; }
  ecmcAxisDataStatus *getAxisStatusDataPtr() { return &data_.status_; }
  int getSumInterlock() { return 0; }
  decltype(ECMC_DIR_BACKWARD) getAxisSetDirection() { return ECMC_DIR_BACKWARD; }
  bool getBlockCom() { return false; }
  bool getAllowCmdFromPLC() { return true; }
  double getExtSetPos() { return data_.status_.externalTrajectoryPosition; }
  double getExtActPos() { return data_.status_.externalEncoderPosition; }
  std::vector<std::shared_ptr<ecmcDataAccess>> dataSources_;
  static ecmcAxisDataType parseDataSource(const char *, int * = nullptr);
  std::shared_ptr<ecmcDataAccess> getDataSource(const char *);
  int readDataSource(ecmcAxisDataType, double *);
  void invalidateDataSources();
  ~ecmcAxisBase() { invalidateDataSources(); }
  ecmcDataLink encoder, drive;
  SourceObjects &monitor = objects;
  ecmcDataLink *getConfigEnc() { return &encoder; }
  SourceObjects *getMon() { return &objects; }
  ecmcDataLink *getMonInput() { return &monitor; }
  ecmcDataLink *getDrv() { return &drive; }
};
ecmcAxisBase *axes[ECMC_MAX_AXES] = {};
struct ecmcPLCDataIF {
  double value = 0;
  std::string name;
  ecmcDataItem item;
  explicit ecmcPLCDataIF(const char *n) : name(n), item(n) {
    item.setEcmcDataPointer(reinterpret_cast<uint8_t *>(&value), sizeof(value));
    item.setEcmcDataType(ECMC_EC_F64);
    item.setAllowWriteToEcmc(true);
  }
  const char *getVarName() { return name.c_str(); }
  ecmcDataItem *getDataItem() { return &item; }
  double getData() { return value; }
};
struct ecmcPLCTask {
  unsigned dataBindingUsers_ = 0;
  bool compiled = true;
  std::string raw;
  ecmcPLCDataIF *compileVariable = nullptr;
  int parseCalls = 0;
  const std::string *getRawExpr() { return &raw; }
  int validate() { if (!raw.empty()) compiled = true; return 0; }
  int error = 0;
  std::string exprStr_ = "original";
  bool compiled_ = true;
  void updateAsyn() {}
  void rebuildVariableAccessLists() {}
  int clearExpr();
  ecmcPLCDataIF *localArray_[2] = {}, *globalArray_[2] = {};
  int localVariableCount_ = 0, globalVariableCount_ = 0;
  bool getCompiled() { return compiled; }
  int getErrorID() { return error; }
  ecmcPLCDataIF *findBindingVariable(const char *) const;
'''
for signature in ('void pinDataBindings()', 'void unpinDataBindings()', 'bool hasDataBindings() const'):
    fixture += method(task_header, signature)
fixture += r'''
};
''' + checks + r'''
struct ecmcPLCMain {
  ~ecmcPLCMain();
''' + fields + r'''
  ecmcAxisBase *axes_[ECMC_MAX_AXES] = {};
  ecmcPLCTask *plcs_[ECMC_MAX_PLCS + ECMC_MAX_AXES] = {};
  ecmcPLCDataIF *plcEnable_[ECMC_MAX_PLCS + ECMC_MAX_AXES] = {};
  ecmcPLCDataIF *plcError_[ECMC_MAX_PLCS + ECMC_MAX_AXES] = {};
  int plcStartAfterEpics_[ECMC_MAX_PLCS + ECMC_MAX_AXES] = {};
  int setErrorID(const char *, const char *, int, int code) { return code; }
  int setAxisArrayPointer(ecmcAxisBase *, int);
  int linkDataInput(int, const char *, int, ecmcDataLink *, int, int);
  int linkAxisDataInput(int, const char *, int, ecmcDataLink *, int, int);
  int linkResolvedDataInput(const std::shared_ptr<ecmcDataAccess> &, int, ecmcDataLink *, int, int = -1, int = -1);
  int unlinkDataInput(ecmcDataLink *, int);
  bool hasPendingDataInputs() const;
  bool hasDataInputProducers() const;
  int validate();
  int validate(int);
  int getCompiled(int);
  int updateAllScanTimeVars() { return 0; }
  int addExprLine(int index, const char *) {
    auto *task = plcs_[index]; ++task->parseCalls;
    if (task->compileVariable) {
      task->localArray_[0] = task->compileVariable;
      task->localVariableCount_ = 1;
    }
    return 0;
  }
  int validateDataInputs();
  void updateDataInputHealth(int);
};
'''
implementation = ''.join(method(axis_source, signature) for signature in (
    'ecmcAxisDataType ecmcAxisBase::parseDataSource(', 'int ecmcAxisBase::readDataSource(',
    'std::shared_ptr<ecmcDataAccess> ecmcAxisBase::getDataSource(', 'void ecmcAxisBase::invalidateDataSources()'))
implementation += method(task, 'ecmcPLCDataIF *ecmcPLCTask::findBindingVariable(')
implementation += method(task, 'int ecmcPLCTask::clearExpr()')
implementation += method(main, 'ecmcPLCMain::~ecmcPLCMain()')
implementation += method(header, 'bool hasPendingDataInputs() const').replace('bool hasPendingDataInputs()', 'bool ecmcPLCMain::hasPendingDataInputs()')
implementation += method(header, 'bool hasDataInputProducers() const').replace('bool hasDataInputProducers()', 'bool ecmcPLCMain::hasDataInputProducers()')
implementation += method(main, 'int ecmcPLCMain::setAxisArrayPointer(')
for signature in ('int ecmcPLCMain::validate()', 'int ecmcPLCMain::validate(int plcIndex)',
                  'int ecmcPLCMain::getCompiled(int plcIndex)', 'int ecmcPLCMain::linkDataInput(', 'int ecmcPLCMain::unlinkDataInput(',
                  'int ecmcPLCMain::linkAxisDataInput(', 'int ecmcPLCMain::linkResolvedDataInput(', 'int ecmcPLCMain::validateDataInputs()', 'void ecmcPLCMain::updateDataInputHealth('):
    implementation += method(main, signature)
fixture += "\necmcPLCMain *plcs = nullptr;\n"
for signature in ('int parseEcPath(', 'static bool validObjectPath(', 'int getMainObjectType(',
                  'int getAxSubObjectType(', 'int getAxEncFuncType(',
                  'int getAxMonFuncType(', 'int getAxDriveFuncType('):
    implementation += method(utils, signature)
for signature in ('int resolveDataInputTarget(', 'int resolveEthercatDataSource(', 'int linkDataToObject(', 'int unlinkDataFromObject('):
    implementation += method(general, signature)
implementation += method(parser, 'static char *trimWhitespace(')
start = parser.index('  // Bind a scalar source')
end = parser.index('  /// "Cfg.SetStartupGate(enable)"', start)
implementation += "\nint parseCommand(const char *myarg_1) {\n" + r'''
  int nvals = 0, iValue = 0;
  char cIdBuffer[4096] = {}, cIdBuffer2[4096] = {};
''' + parser[start:end] + "\n return -777;\n}\n"
tests = r'''
int published = 0;
int publication(void *) { ++published; return 0; }
int main() {
  appModeStat = ECMC_MODE_CONFIG;
  uint64_t word = UINT64_MAX;
  int validity = 1;
  ecmcDataAccess legacy;
  legacy.identity = &word; legacy.data = reinterpret_cast<uint8_t *>(&word);
  legacy.type = ECMC_EC_U64; legacy.bits = 64;
  legacy.wordStorage = legacy.writable = true;
  legacy.validity = &validity;
  legacy.publish = publication;
  ecmcEcEntry entryProvider;
  entryProvider.view = std::make_shared<ecmcDataAccess>(legacy);
  ecmcEcEntryLink compatible;
  assert(compatible.setEntryAtIndex(&entryProvider, 0, -1) == 0);
  uint64_t legacyRead = 0;
  assert(compatible.readEcEntryValue(0, &legacyRead) == 0 && legacyRead == UINT64_MAX);
  ecmcDataLink link;
  assert(link.setDataAtIndex(legacy, 0, -1) == 0);
  uint64_t raw = 0;
  assert(link.readDataValue(0, &raw) == 0 && raw == UINT64_MAX);
  assert(link.writeDataValue(0, 123) == 0 && word == 123 && published == 1);
  validity = 0;
  raw = 999;
  assert(link.readDataValue(0, &raw) == ERROR_EC_ENTRY_EC_DOMAIN_ERROR && raw == 999);
  assert(link.writeDataValue(0, 456) == ERROR_EC_ENTRY_EC_DOMAIN_ERROR);
  assert(word == 456 && published == 2); // Preserve write-before-health-error.
  validity = 1;
  assert(link.writeDataBits(0, 4, 7) == 0 && word == 456); // Legacy invalid-field no-op.
  assert(link.setDataAtIndex(legacy, 0, 60) == 0);
  assert(link.writeDataBits(0, 4, 15) == 0);
  assert(link.readDataBits(0, 4, &raw) == 0 && raw == 15);
  assert(published == 2); // Bit writes retain their old no-publication behavior.

  ecmcPLCDataIF global("global.position"), local("static.limit"), enable("enable"), error("error");
  global.value = 12.5; local.value = 1; enable.value = 1;
  ecmcPLCTask producer;
  producer.globalArray_[0] = &global; producer.globalVariableCount_ = 1;
  producer.localArray_[0] = &local; producer.localVariableCount_ = 1;
  ecmcAxisBase axis;
  ecmcPLCMain manager;
  axis.data_.status_.axisId = 2;
  manager.axes_[2] = &axis;
  manager.plcs_[0] = &producer;
  manager.plcEnable_[0] = &enable;
  manager.plcError_[0] = &error;
  ecmcDataLink encoderInput, monitorInput;
  // Test restoring a previous EtherCAT descriptor on unlink.
  assert(encoderInput.setDataAtIndex(legacy, 0, -1) == 0);
  assert(manager.linkDataInput(0, "global.position", 2, &encoderInput, 0, false) == 0);
  assert(manager.linkDataInput(0, "static.limit", 2, &monitorInput, 0, true) == 0);
  assert(manager.hasPendingDataInputs() && !producer.hasDataBindings());
  assert(encoderInput.getDataAtIndex(0, nullptr)->data == legacy.data);
  assert(manager.validateDataInputs() == 0 && !manager.hasPendingDataInputs());
  assert(producer.dataBindingUsers_ == 2 && producer.hasDataBindings());
  assert(producer.clearExpr() == ERROR_PLC_DATA_SOURCE_BOUND);
  assert(producer.localArray_[0] == &local && producer.exprStr_ == "original");
  assert(manager.linkDataInput(0, "global.missing", 2, &encoderInput, 1, false) == 0);
  assert(manager.validateDataInputs() == ERROR_PLC_VARIABLE_NOT_FOUND);
  assert(manager.unlinkDataInput(&encoderInput, 1) == 0);
  assert(manager.linkDataInput(0, "global.position", 2, &encoderInput, 0, false) == ERROR_PLC_DATA_SOURCE_BOUND);
  assert(manager.validateDataInputs() == 0);
  manager.updateDataInputHealth(2);
  double position = 0;
  assert(encoderInput.readDataValueDouble(0, &position) == 0 && position == 12.5);
  global.value = -3.25;
  assert(encoderInput.readDataValueDouble(0, &position) == 0 && position == -3.25);
  assert(monitorInput.readDataValue(0, &raw) == 0 && raw == 1);
  local.value = -1;
  assert(monitorInput.readDataValue(0, &raw) == 0 && raw == 0);
  assert(encoderInput.writeDataValueDouble(0, 99) == ERROR_EC_ENTRY_WRITE_FAIL);
  assert(global.value == -3.25); // Input binding cannot bypass a write handler.
  for (int fault = 0; fault < 4; ++fault) {
    enable.value = 1; error.value = 0; producer.compiled = true; producer.error = 0;
    if (fault == 0) enable.value = 0;
    if (fault == 1) error.value = 1;
    if (fault == 2) producer.compiled = false;
    if (fault == 3) producer.error = 7;
    manager.updateDataInputHealth(2);
    position = 42;
    assert(encoderInput.readDataValueDouble(0, &position) == ERROR_EC_ENTRY_EC_DOMAIN_ERROR);
    assert(position == 42);
  }
  enable.value = 1; error.value = 0; producer.compiled = true; producer.error = 0;
  manager.updateDataInputHealth(2);
  global.value = std::numeric_limits<double>::quiet_NaN();
  assert(!encoderInput.checkDataValid(0));
  global.value = 8;
  assert(encoderInput.readDataValueDouble(0, &position) == 0 && position == 8);
  // Runtime entry invalidates cached producer health until recomputed.
  assert(manager.validateDataInputs() == 0 && !encoderInput.checkDataValid(0));
  manager.updateDataInputHealth(2);
  appModeCmd = ECMC_MODE_RUNTIME;
  assert(manager.unlinkDataInput(&encoderInput, 0) == ERROR_MAIN_APP_MODE_ALREADY_RUNTIME);
  assert(manager.linkDataInput(0, "global.position", 2, &encoderInput, 1, false) == ERROR_MAIN_APP_MODE_ALREADY_RUNTIME);
  appModeCmd = ECMC_MODE_CONFIG;
  assert(manager.unlinkDataInput(&encoderInput, 0) == 0);
  assert(encoderInput.getDataAtIndex(0, nullptr)->data == legacy.data);
  assert(producer.dataBindingUsers_ == 1);
  assert(manager.unlinkDataInput(&monitorInput, 0) == 0);
  assert(!monitorInput.checkDataExist(0) && !producer.hasDataBindings());
  // Source owner destruction invalidates retained bindings before any value
  // storage is freed. No dangling health pointer is dereferenced by a reader.
  ecmcDataLink retainedInput;
  auto *owner = new ecmcPLCMain;
  auto *ownedProducer = new ecmcPLCTask;
  auto *ownedValue = new ecmcPLCDataIF("static.owned");
  ownedValue->value = 7;
  ownedProducer->localArray_[0] = ownedValue; ownedProducer->localVariableCount_ = 1;
  owner->plcs_[0] = ownedProducer; owner->axes_[2] = &axis;
  owner->plcEnable_[0] = &enable;
  assert(owner->linkDataInput(0, "static.owned", 2, &retainedInput, 0, 0) == 0);
  assert(owner->validateDataInputs() == 0);
  owner->updateDataInputHealth(2);
  assert(retainedInput.readDataValueDouble(0, &position) == 0 && position == 7);
  delete owner;
  delete ownedValue;
  position = 42;
  assert(retainedInput.readDataValueDouble(0, &position) == ERROR_EC_ENTRY_EC_DOMAIN_ERROR && position == 42);
  // Exercise the real reload body after unpinning, with owned static storage.
  ecmcPLCTask reload;
  reload.localArray_[0] = new ecmcPLCDataIF("static.reload");
  reload.localVariableCount_ = 1;
  auto beforeReload = reload.localArray_[0]->item.getDataAccess();
  reload.pinDataBindings();
  assert(reload.clearExpr() == ERROR_PLC_DATA_SOURCE_BOUND && beforeReload->data);
  reload.unpinDataBindings();
  assert(reload.clearExpr() == 0 && !beforeReload->data && reload.localVariableCount_ == 0);
  // Exercise the real configuration resolver and bounded command parser.
  plcs = &manager; axes[2] = &axis;
  global.value = 8;
  assert(parseCommand("LinkDataToObject(plcs.plc0.global.position,ax2.enc.actpos,0)") == 0);
  assert(manager.validateDataInputs() == 0);
  manager.updateDataInputHealth(2);
  assert(axis.encoder.readDataValueDouble(0, &position) == 0 && position == 8);
  assert(parseCommand("UnlinkDataFromObject(ax2.enc.actpos)") == 0);
  assert(parseCommand("LinkDataToObject(plcs.plc0.static.limit,ax2.mon.lowlim,0)") == ERROR_MAIN_ECMC_LINK_INVALID);
  assert(parseCommand("LinkDataToObject(plcs.plc0.static.limit,ax2.mon.lowlim,1)") == 0);
  assert(manager.validateDataInputs() == 0);
  manager.updateDataInputHealth(2);
  local.value = 1;
  assert(axis.monitor.readDataValue(0, &raw) == 0 && raw == 1);
  assert(parseCommand("UnlinkDataFromObject(ax2.mon.lowlim)") == 0);
  assert(parseCommand("LinkDataToObject(plcs.plc0.global.position,ax2.drv.control,2)") == ERROR_MAIN_ECMC_LINK_INVALID);
  assert(parseCommand("LinkDataToObject(plcs.plc0.global.position,ax2.drv.status,2)") == 0);
  assert(manager.validateDataInputs() == 0);
  manager.updateDataInputHealth(2);
  assert(axis.drive.readDataValue(ECMC_DRIVEBASE_ENTRY_INDEX_STATUS_WORD, &raw) == 0 && raw == 8);
  assert(parseCommand("UnlinkDataFromObject(ax2.drv.status)") == 0);
  assert(parseCommand("LinkDataToObject(plcs.plc0.global.position,ax2.enc.actpos,0") == ERROR_MAIN_PARSER_INVALID_FORMAT);
  assert(parseCommand("UnlinkDataFromObject(ax2.enc.actpos)junk") == ERROR_MAIN_PARSER_INVALID_FORMAT);
  const std::string oversized = "LinkDataToObject(plcs.plc0." + std::string(1024, 'x') + ",ax2.enc.actpos,0)";
  assert(parseCommand(oversized.c_str()) == ERROR_MAIN_PARSER_INVALID_FORMAT);
  // Producer identity is part of the source path, never a separate command.
  for (const char *bad : {"plcs.plc-1.static.limit", "plcs.plc.static.limit",
       "plcs.plc999999999999999999999.static.limit", "plcs.plc0static.limit",
       "plcs.plc0.static.", "plcs.plc0.global.", "plcs.plc0.ax1.drv.enable",
       "plcs.ax.static.limit", "plcs.ax999999999999999.static.limit"}) {
    auto previous = axis.monitor.getDataAtIndex(0, nullptr);
    assert(linkDataToObject(bad, const_cast<char *>("ax2.mon.lowlim"), 1) == ERROR_MAIN_ECMC_LINK_INVALID);
    assert(axis.monitor.getDataAtIndex(0, nullptr) == previous);
  }
  assert(parseCommand("LinkPLCDataToObject(0,static.limit,ax2.mon.lowlim,1)") == -777);
  manager.plcs_[ECMC_MAX_PLCS + 1] = &producer;
  manager.plcEnable_[ECMC_MAX_PLCS + 1] = &enable;
  assert(parseCommand("LinkDataToObject(plcs.ax1.static.limit,ax2.mon.lowlim,1)") == 0);
  assert(manager.validateDataInputs() == 0);
  manager.updateDataInputHealth(2);
  assert(axis.monitor.readDataValue(0, &raw) == 0 && raw == 1);
  assert(parseCommand("UnlinkDataFromObject(ax2.mon.lowlim)") == 0);
  manager.plcs_[ECMC_MAX_PLCS + 1] = nullptr;
  assert(!producer.hasDataBindings());
  // Normal workflow: axis/link declarations first; producer PLCs come later.
  ecmcDataLink delayedInput, secondDelayed, cancelled;
  assert(delayedInput.setDataAtIndex(legacy, 0, 4) == 0);
  auto beforeDeferred = delayedInput.getDataAtIndex(0, nullptr);
  assert(manager.linkDataInput(5, "static.later", 2, &delayedInput, 0, 0) == 0);
  assert(manager.linkDataInput(5, "static.second", 2, &secondDelayed, 0, 1) == 0);
  assert(manager.hasPendingDataInputs());
  assert(delayedInput.getDataAtIndex(0, nullptr) == beforeDeferred);
  assert(manager.validateDataInputs() == ERROR_PLCS_PLC_NULL);
  ecmcPLCTask lateProducer;
  manager.plcs_[5] = &lateProducer;
  manager.plcEnable_[5] = &enable;
  lateProducer.compiled = false;
  assert(manager.validateDataInputs() == ERROR_PLC_COMPILE_ERROR);
  ecmcPLCDataIF lateValue("static.later"), secondValue("static.second");
  lateValue.value = 6.25; secondValue.value = 1;
  lateProducer.localArray_[0] = &lateValue; lateProducer.localVariableCount_ = 1;
  lateProducer.compiled = true;
  // A valid first source plus a missing second source installs/pins neither.
  assert(manager.validateDataInputs() == ERROR_PLC_VARIABLE_NOT_FOUND);
  assert(!lateProducer.hasDataBindings() && !secondDelayed.checkDataExist(0));
  assert(delayedInput.getDataAtIndex(0, nullptr) == beforeDeferred);
  lateProducer.localArray_[1] = &secondValue; lateProducer.localVariableCount_ = 2;
  // Metadata errors also leave the complete pending batch untouched.
  secondValue.item.setEcmcDataPointer(reinterpret_cast<uint8_t *>(&secondValue.value), 1);
  assert(manager.validateDataInputs() == ERROR_PLC_SOURCE_INVALID);
  assert(!lateProducer.hasDataBindings() && manager.hasPendingDataInputs());
  secondValue.item.setEcmcDataPointer(reinterpret_cast<uint8_t *>(&secondValue.value), sizeof(double));
  // The RT-entry validator runs in STARTUP after the mode command changed.
  appModeCmd = ECMC_MODE_RUNTIME; appModeStat = ECMC_MODE_STARTUP;
  assert(manager.validateDataInputs() == 0 && !manager.hasPendingDataInputs());
  assert(lateProducer.dataBindingUsers_ == 2);
  appModeCmd = ECMC_MODE_CONFIG; appModeStat = ECMC_MODE_CONFIG;
  manager.updateDataInputHealth(2);
  assert(delayedInput.readDataValueDouble(0, &position) == 0 && position == 6.25);
  assert(secondDelayed.readDataValue(0, &raw) == 0 && raw == 1);
  assert(manager.validateDataInputs() == 0 && lateProducer.dataBindingUsers_ == 2);
  assert(manager.unlinkDataInput(&delayedInput, 0) == 0);
  int priorBit = -1;
  assert(delayedInput.getDataAtIndex(0, &priorBit) == beforeDeferred && priorBit == 4);
  assert(manager.unlinkDataInput(&secondDelayed, 0) == 0 && !lateProducer.hasDataBindings());
  manager.plcs_[5] = nullptr;
  // Exercise the real manager validation pipeline: parse raw PLC code, compile,
  // then resolve names, all while entering STARTUP and before axis validation.
  ecmcDataLink lastPLCInput;
  assert(manager.linkDataInput(7, "static.last", 2, &lastPLCInput, 0, 0) == 0);
  ecmcPLCDataIF lastValue("static.last"); lastValue.value = 9;
  ecmcPLCTask lastProducer;
  lastProducer.compiled = false; lastProducer.raw = "static.last:=9;";
  lastProducer.compileVariable = &lastValue;
  manager.plcs_[7] = &lastProducer; manager.plcEnable_[7] = &enable;
  appModeCmd = ECMC_MODE_RUNTIME; appModeStat = ECMC_MODE_STARTUP;
  assert(manager.validate() == 0 && lastProducer.parseCalls == 1 && lastProducer.compiled);
  assert(!manager.hasPendingDataInputs() && lastProducer.hasDataBindings());
  manager.updateDataInputHealth(2);
  assert(lastPLCInput.readDataValueDouble(0, &position) == 0 && position == 9);
  appModeCmd = ECMC_MODE_CONFIG; appModeStat = ECMC_MODE_CONFIG;
  assert(manager.unlinkDataInput(&lastPLCInput, 0) == 0);
  manager.plcs_[7] = nullptr;
  // Pending declarations can be cancelled without creating or pinning a PLC.
  assert(cancelled.setDataAtIndex(legacy, 0, 3) == 0);
  auto cancelPrevious = cancelled.getDataAtIndex(0, nullptr);
  assert(manager.linkDataInput(6, "static.cancelled", 2, &cancelled, 0, 1) == 0);
  assert(manager.unlinkDataInput(&cancelled, 0) == 0);
  assert(cancelled.getDataAtIndex(0, &priorBit) == cancelPrevious && priorBit == 3);
  // Do not silently overwrite a destination reconfigured after declaration.
  assert(manager.linkDataInput(0, "static.limit", 2, &cancelled, 0, 1) == 0);
  assert(cancelled.setDataAtIndex(legacy, 0, 2) == 0);
  auto newerBinding = cancelled.getDataAtIndex(0, nullptr);
  assert(manager.validateDataInputs() == ERROR_PLC_DATA_SOURCE_BOUND);
  assert(manager.unlinkDataInput(&cancelled, 0) == 0);
  assert(cancelled.getDataAtIndex(0, &priorBit) == newerBinding && priorBit == 2);
  // EtherCAT sources use the identical command and neutral link lifecycle.
  auto preconfiguredEntry = std::make_shared<ecmcDataAccess>(legacy);
  assert(axis.encoder.setDataAtIndex(preconfiguredEntry, 0, 5) == 0);
  auto previousHardware = axis.encoder.getDataAtIndex(0, nullptr);
  assert(parseCommand("LinkDataToObject(ec0.s3.Position,ax2.enc.actpos,0)") == ERROR_MAIN_EC_NULL);
  ecmcEc hardware;
  ecmcEcSlave slave, simulated;
  hardware.slave = &slave; hardware.sim = &simulated; ec = &hardware;
  uint64_t hardwareWord = UINT64_MAX;
  int domainHealthy = 1, customHealthy = 1;
  auto entryAccess = std::make_shared<ecmcDataAccess>();
  entryAccess->data = reinterpret_cast<uint8_t *>(&hardwareWord);
  entryAccess->identity = &hardwareWord;
  entryAccess->type = ECMC_EC_U64; entryAccess->bits = 64;
  entryAccess->wordStorage = true;
  entryAccess->validation = ecmcDataAccess::DIRECT_STATUS;
  entryAccess->validity = &domainHealthy;
  ecmcEcEntry hardwareEntry; hardwareEntry.view = entryAccess;
  slave.entries["Position"] = &hardwareEntry;
  simulated.entries["ONE"] = &hardwareEntry;
  assert(parseCommand("LinkDataToObject(ec0.s3.Position,ax2.enc.actpos,0)") == 0);
  // Native whole-entry access must use the very same provider descriptor.
  assert(axis.encoder.getDataAtIndex(0, nullptr) == entryAccess);
  assert(manager.validateDataInputs() == 0);
  assert(!manager.hasDataInputProducers() && manager.dataInputIndices_[2].empty());
  assert(axis.encoder.readDataValue(0, &raw) == 0 && raw == UINT64_MAX);
  domainHealthy = 0; raw = 97;
  assert(axis.encoder.readDataValue(0, &raw) == ERROR_EC_ENTRY_EC_DOMAIN_ERROR && raw == 97);
  assert(manager.validateDataInputs() == 0); // Bad bus health is not dead storage.
  domainHealthy = 1;
  assert(parseCommand("UnlinkDataFromObject(ax2.enc.actpos)") == 0);
  int restoredBit = -1;
  assert(axis.encoder.getDataAtIndex(0, &restoredBit) == previousHardware && restoredBit == 5);
  // Bit 63 works as both a Boolean/raw input and a numeric encoder input.
  for (const char *destination : {"ax2.mon.lowlim", "ax2.enc.actpos"}) {
    std::string command = std::string("LinkDataToObject(ec0.s3.Position.63,") + destination + ",0)";
    assert(parseCommand(command.c_str()) == 0);
    auto *input = strcmp(destination, "ax2.mon.lowlim") == 0 ?
      static_cast<ecmcDataLink *>(&axis.monitor) : &axis.encoder;
    hardwareWord = UINT64_C(1) << 63;
    assert(input->readDataValue(0, &raw) == 0 && raw == 1);
    assert(input->readDataValueDouble(0, &position) == 0 && position == 1);
    hardwareWord = 2;
    assert(input->readDataValue(0, &raw) == 0 && raw == 0);
    assert(input->readDataValueDouble(0, &position) == 0 && position == 0);
    domainHealthy = 0; raw = 97;
    assert(input->readDataValue(0, &raw) == ERROR_EC_ENTRY_EC_DOMAIN_ERROR && raw == 97);
    domainHealthy = 1;
    command = std::string("UnlinkDataFromObject(") + destination + ")";
    assert(parseCommand(command.c_str()) == 0);
  }
  // Preserve a custom provider's filtered-health callback through bit views.
  entryAccess->validation = ecmcDataAccess::CUSTOM_STATUS;
  entryAccess->customValidity = [](void *health) { return *static_cast<int *>(health); };
  entryAccess->validityContext = &customHealthy;
  assert(parseCommand("LinkDataToObject(ec0.s3.Position.0,ax2.mon.lowlim,0)") == 0);
  hardwareWord = 1;
  assert(axis.monitor.readDataValue(0, &raw) == 0 && raw == 1);
  customHealthy = 0; raw = 97;
  assert(axis.monitor.readDataValue(0, &raw) != 0 && raw == 97);
  customHealthy = 1;
  assert(parseCommand("UnlinkDataFromObject(ax2.mon.lowlim)") == 0);
  entryAccess->validation = ecmcDataAccess::DIRECT_STATUS;
  assert(parseCommand("LinkDataToObject(ec0.s-1.ONE,ax2.mon.lowlim,0)") == 0);
  assert(parseCommand("UnlinkDataFromObject(ax2.mon.lowlim)") == 0);
  hardware.master = -1; // Existing masterless simulated-slave entry convention.
  assert(parseCommand("LinkDataToObject(ec-1.s-1.ONE,ax2.mon.lowlim,0)") == 0);
  assert(parseCommand("UnlinkDataFromObject(ax2.mon.lowlim)") == 0);
  hardware.master = 0;
  // Floating sources preserve native reads; integer inputs require conversion.
  uint64_t floatingWord = 0;
  auto floatingAccess = std::make_shared<ecmcDataAccess>(*entryAccess);
  floatingAccess->data = reinterpret_cast<uint8_t *>(&floatingWord);
  floatingAccess->type = ECMC_EC_F64;
  ecmcEcEntry floatingEntry; floatingEntry.view = floatingAccess;
  slave.entries["Float"] = &floatingEntry;
  double floatingValue = 4.5;
  std::memcpy(&floatingWord, &floatingValue, sizeof(floatingValue));
  assert(parseCommand("LinkDataToObject(ec0.s3.Float,ax2.mon.lowlim,0)") == ERROR_MAIN_ECMC_LINK_INVALID);
  assert(parseCommand("LinkDataToObject(ec0.s3.Float.0,ax2.mon.lowlim,0)") == ERROR_MAIN_ECMC_LINK_INVALID);
  assert(parseCommand("LinkDataToObject(ec0.s3.Float,ax2.enc.actpos,0)") == 0);
  assert(axis.encoder.readDataValueDouble(0, &position) == 0 && position == 4.5);
  assert(parseCommand("UnlinkDataFromObject(ax2.enc.actpos)") == 0);
  assert(parseCommand("LinkDataToObject(ec0.s3.Float,ax2.drv.status,2)") == 0);
  raw = 97;
  assert(axis.drive.readDataValue(ECMC_DRIVEBASE_ENTRY_INDEX_STATUS_WORD, &raw) != 0 && raw == 97);
  floatingValue = 4;
  std::memcpy(&floatingWord, &floatingValue, sizeof(floatingValue));
  assert(axis.drive.readDataValue(ECMC_DRIVEBASE_ENTRY_INDEX_STATUS_WORD, &raw) == 0 && raw == 4);
  assert(parseCommand("UnlinkDataFromObject(ax2.drv.status)") == 0);
  assert(parseCommand("LinkDataToObject(ec0.s3.Float,ax2.mon.lowlim,1)") == 0);
  assert(axis.monitor.readDataValue(0, &raw) == 0 && raw == 1);
  assert(parseCommand("UnlinkDataFromObject(ax2.mon.lowlim)") == 0);
  for (const char *bad : {"ec1.s3.Position", "ec0.s4.Position", "ec0.s3.Missing",
       "ec0.s3.Position.64", "ec0.s3.Position.-1", "ec0.s3.Position.1junk",
       "ec99999999999999999999999.s3.Position", "ec0.Position", "ec0.s3."}) {
    auto prior = axis.monitor.getDataAtIndex(0, nullptr);
    assert(linkDataToObject(bad, const_cast<char *>("ax2.mon.lowlim"), 0) != 0);
    assert(axis.monitor.getDataAtIndex(0, nullptr) == prior);
  }
  assert(parseCommand("LinkDataToObject(ec0.s3.Position.0,ax2.mon.lowlim,0)") == 0);
  entryAccess->data = nullptr; // Entry owner invalidates before destroying storage.
  raw = 97;
  assert(axis.monitor.readDataValue(0, &raw) != 0 && raw == 97);
  assert(manager.validateDataInputs() == ERROR_PLC_SOURCE_INVALID);
  assert(parseCommand("UnlinkDataFromObject(ax2.mon.lowlim)") == 0);
  // A retained view must not dereference a domain pointer freed with its owner.
  auto ownedDomain = std::make_unique<int>(1);
  entryAccess->data = reinterpret_cast<uint8_t *>(&hardwareWord);
  entryAccess->validity = ownedDomain.get();
  assert(parseCommand("LinkDataToObject(ec0.s3.Position.0,ax2.mon.lowlim,0)") == 0);
  assert(!axis.monitor.getDataAtIndex(0, nullptr)->validity);
  entryAccess->data = nullptr;
  ownedDomain.reset();
  raw = 97;
  assert(axis.monitor.readDataValue(0, &raw) != 0 && raw == 97);
  assert(parseCommand("UnlinkDataFromObject(ax2.mon.lowlim)") == 0);
  ec = nullptr;
  // The same paths and common command resolve memory-backed I/O in mixed mode.
  ecmcSimulationDriver memory(1, nullptr);
  simulationRegistry[1] = &memory;
  assert(memory.addDevice(0, 5, 1, 2) == 0);
  assert(memory.addEntry(5, 1, 2, 2, 3, 0x1a00, 0x6000, 1, ECMC_EC_S32, "positionActual01", true) == 0);
  assert(memory.addEntry(5, 1, 2, 1, 2, 0x1600, 0x7000, 1, ECMC_EC_U16, "controlWord01", true) == 0);
  assert(memory.setValue(5, "positionActual01", "-321") == 0);
  assert(memory.addEntry(5, 1, 2, 1, 2, 0x1600, 0x7000, 2, ECMC_EC_F64, "floatingOutput", true) == 0);
  assert(parseCommand("LinkDataToObject(ec1.s5.floatingOutput.3,ax2.drv.control,0)") == ERROR_MAIN_ECMC_LINK_INVALID);
  ec = &hardware;
  assert(parseCommand("LinkDataToObject(ec1.s5.positionActual01,ax2.enc.actpos,0)") == 0);
  assert(axis.encoder.readDataValueDouble(0, &position) == 0 && position == -321);
  assert(parseCommand("LinkDataToObject(ec1.s5.controlWord01.3,ax2.drv.control,0)") == 0);
  assert(axis.drive.writeDataValue(0, 1) == 0);
  assert(memory.findEntry(5, "controlWord01")->value == 8);
  // No model is baked in: writing control does not change feedback.
  assert(axis.encoder.readDataValueDouble(0, &position) == 0 && position == -321);
  memory.setHealthy(false); position = 77;
  assert(axis.encoder.readDataValueDouble(0, &position) != 0 && position == 77);
  memory.setHealthy(true);
  assert(parseCommand("UnlinkDataFromObject(ax2.drv.control)") == 0);
  assert(parseCommand("UnlinkDataFromObject(ax2.enc.actpos)") == 0);
  simulationRegistry.clear(); ec = nullptr;
  // Native axis sources exist without any producer PLC or parsed expression.
  auto sourceAxis = std::make_unique<ecmcAxisBase>();
  sourceAxis->data_.status_.axisId = 1;
  axes[1] = sourceAxis.get();
  sourceAxis->data_.status_.currentPositionActual = 37.5;
  auto native = sourceAxis->getDataSource("ax1.enc.actpos");
  assert(native && native == sourceAxis->getDataSource("ax1.enc.actpos"));
  assert(native->data == reinterpret_cast<uint8_t *>(&sourceAxis->data_.status_.currentPositionActual));
  assert(!sourceAxis->getDataSource("ax1.enc.typo"));
  assert(!sourceAxis->getDataSource("ax2.enc.actpos"));
  assert(!sourceAxis->getDataSource("ax999999999999999999.enc.actpos"));
  auto computed = sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_POS_ERROR);
  assert(computed);
  sourceAxis->data_.status_.currentPositionSetpoint = 40;
  assert(computed->readDouble(&position) == 0 && position == 2.5);
  // Check every supported name resolves before any PLC has registered it.
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_AXIS_ID));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_POS_SET));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_POS_ACT));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_POS_TARGET));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_POS_ERROR));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_POS_RAW));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_CNTRL_OUT));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_VEL_SET));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_VEL_ACT));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_VEL_SET_FF_RAW));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_VEL_SET_RAW));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_CYCLE_COUNTER));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_ERROR));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_COMMAND));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_CMD_DATA));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_SEQ_STATE));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_INTERLOCK_TYPE));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_TRAJ_SOURCE));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_ENC_SOURCE));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_ENABLE));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_ENABLED));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_EXECUTE));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_BUSY));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_AT_TARGET));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_HOMED));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_LIMIT_BWD));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_LIMIT_FWD));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_HOME_SWITCH));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_RESET));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_VEL_TARGET_SET));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_ACC_TARGET_SET));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_DEC_TARGET_SET));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_SOFT_LIMIT_BWD));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_SOFT_LIMIT_FWD));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_SOFT_LIMIT_BWD_ENABLE));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_SOFT_LIMIT_FWD_ENABLE));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_TRAJ_DIRECTION));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_ENC_HOMEPOS));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_BLOCK_COM));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_INTERLOCK_FWD_TYPE));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_INTERLOCK_BWD_TYPE));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_ALLOW_PLC_CMD));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_POS_SET_EXTERNAL));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_POS_ACT_EXTERNAL));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_CTRL_KP_CMD));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_CTRL_KI_CMD));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_CTRL_KD_CMD));
  assert(sourceAxis->getDataSource("ax1." ECMC_AXIS_DATA_STR_CTRL_KFF_CMD));

  auto rawPosition = sourceAxis->getDataSource("ax1.enc.rawpos");
  // Native integer descriptors must not round through a double.
  assert(rawPosition);
  {
    sourceAxis->data_.status_.currentPositionActualRaw = INT64_MAX;
    assert(rawPosition->readValue(&raw) == 0 && raw == static_cast<uint64_t>(INT64_MAX));
  }
  ecmcDataAccess unsignedPosition = *rawPosition;
  unsignedPosition.exactUnsigned = true;
  sourceAxis->data_.status_.currentPositionActualRaw = -1;
  assert(!unsignedPosition.valid() && unsignedPosition.readValue(&raw) != 0);
  sourceAxis->data_.status_.currentPositionActualRaw = INT64_MAX;
  assert(unsignedPosition.valid() && unsignedPosition.readValue(&raw) == 0 && raw == static_cast<uint64_t>(INT64_MAX));
  assert(parseCommand("LinkDataToObject(ax1.enc.actpos,ax2.enc.actpos,0)") == 0);
  assert(manager.validateDataInputs() == 0);
  manager.updateDataInputHealth(2);
  assert(axis.encoder.readDataValueDouble(0, &position) == 0 && position == 37.5);
  sourceAxis->data_.status_.currentPositionActual = -2.25;
  assert(axis.encoder.readDataValueDouble(0, &position) == 0 && position == -2.25);
  // Different signals on the same axis are allowed.
  sourceAxis->data_.status_.statusWord_.enable = 1;
  assert(parseCommand("LinkDataToObject(ax1.drv.enable,ax1.mon.highlim,0)") == 0);
  assert(sourceAxis->monitor.readDataValue(1, &raw) == 0 && raw == 1);
  sourceAxis->data_.status_.statusWord_.enable = 0;
  assert(sourceAxis->monitor.readDataValue(1, &raw) == 0 && raw == 0);
  assert(parseCommand("UnlinkDataFromObject(ax1.mon.highlim)") == 0);
  // 1 -> 2 already exists. An unrelated 2 -> 1 signal is also allowed.
  axis.data_.status_.statusWord_.enable = 1;
  assert(parseCommand("LinkDataToObject(ax2.drv.enable,ax1.mon.lowlim,0)") == 0);
  assert(sourceAxis->monitor.readDataValue(0, &raw) == 0 && raw == 1);
  assert(parseCommand("UnlinkDataFromObject(ax1.mon.lowlim)") == 0);
  // Reciprocal links read existing samples, without recursive evaluation.
  axis.data_.status_.currentPositionActual = 21;
  assert(parseCommand("LinkDataToObject(ax2.enc.actpos,ax1.enc.actpos,0)") == 0);
  assert(sourceAxis->encoder.readDataValueDouble(0, &position) == 0 && position == 21);
  assert(parseCommand("UnlinkDataFromObject(ax1.enc.actpos)") == 0);
  // Only an identical source/destination signal is rejected. Axis spelling
  // aliases must not bypass this check; failed binds preserve the old slot.
  auto prior = sourceAxis->monitor.getDataAtIndex(0, nullptr);
  assert(parseCommand("LinkDataToObject(ax1.enc.actpos,ax1.enc.actpos,0)") == ERROR_PLC_SOURCE_INVALID);
  assert(parseCommand("LinkDataToObject(ax01.mon.lowlim,ax1.mon.lowlim,0)") == ERROR_PLC_SOURCE_INVALID);
  assert(sourceAxis->monitor.getDataAtIndex(0, nullptr) == prior);
  assert(parseCommand("UnlinkDataFromObject(ax2.enc.actpos)") == 0);
  sourceAxis->data_.status_.statusWord_.enable = 1;
  sourceAxis->data_.status_.statusWord_.enabled = 0;
  assert(parseCommand("LinkDataToObject(ax1.drv.enable,ax2.mon.lowlim,0)") == 0);
  assert(axis.monitor.readDataValue(0, &raw) == 0 && raw == 1);
  sourceAxis->data_.status_.statusWord_.enable = 0;
  assert(axis.monitor.readDataValue(0, &raw) == 0 && raw == 0);
  assert(parseCommand("LinkDataToObject(ax1.enc.typo,ax2.mon.highlim,1)") == ERROR_MAIN_ECMC_LINK_INVALID);
  assert(parseCommand("LinkDataToObject(ax1.enc.actpos,ax2.mon.highlim,0)") == ERROR_MAIN_ECMC_LINK_INVALID);
  assert(parseCommand("LinkDataToObject(ax1.enc.actpos,ax2.drv.control,2)") == ERROR_MAIN_ECMC_LINK_INVALID);
  assert(parseCommand("LinkDataToObject(ax1.enc.actpos,ax2.enc.actpos,0)junk") == ERROR_MAIN_PARSER_INVALID_FORMAT);
  sourceAxis.reset(); axes[1] = nullptr;
  raw = 999;
  assert(axis.monitor.readDataValue(0, &raw) != 0 && raw == 999);
  assert(!native->data && manager.validateDataInputs() == ERROR_PLC_SOURCE_INVALID);
  assert(parseCommand("UnlinkDataFromObject(ax2.mon.lowlim)") == 0);
  appModeStat = ECMC_MODE_RUNTIME;
  assert(parseCommand("LinkDataToObject(plcs.plc0.global.position,ax2.enc.actpos,0)") == ERROR_MAIN_APP_MODE_ALREADY_RUNTIME);
  assert(parseCommand("LinkDataToObject(ax1.enc.actpos,ax2.enc.actpos,0)") == ERROR_MAIN_APP_MODE_ALREADY_RUNTIME);
  assert(parseCommand("LinkDataToObject(ec0.s3.Position,ax2.enc.actpos,0)") == ERROR_MAIN_APP_MODE_ALREADY_RUNTIME);
  appModeStat = ECMC_MODE_CONFIG;
  plcs = nullptr; axes[2] = nullptr;
  manager.plcs_[0] = nullptr; // This fixture's other producer is stack-owned.
  puts("Common data: native storage, EtherCAT sources/health/bits, lazy axis sources, same-axis/reciprocal links, direct-self rejection, PLC binding, faults, lifetime and unlink passed.");
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp, exe = Path(directory) / 'links.cpp', Path(directory) / 'links'
    cpp.write_text(fixture.replace('#include "ecmcEcEntryLink.h"',
        '#include "' + str(SUP / 'ethercat/ecmcEcEntryLink.h') + '"') + implementation + tests)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O2', '-Wall', '-Wextra',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-fno-omit-frame-pointer',
                    '-I' + str(ROOT / 'tools/plc-test/offline'),
                    '-I' + str(SUP / 'main'), '-I' + str(SUP / 'com'),
                    '-I' + str(SUP / 'motion'), '-I' + str(SUP / 'plc'), '-I' + str(SUP / 'ethercat'),
                    str(cpp), str(SUP / 'com/ecmcDataItem.cpp'),
                    str(SUP / 'com/ecmcDataLink.cpp'), str(SUP / 'misc/ecmcSimulationDriver.cpp'), str(SUP / 'com/ecmcDataAccess.cpp'), str(SUP / 'main/ecmcError.cpp'),
                    str(ROOT / 'tests/monitor/support.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
