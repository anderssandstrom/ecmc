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
#include "ecmcDataLink.h"
#include "ecmcAxisData.h"
#include "ecmcErrorsList.h"
#include "ecmcPLCTaskErrors.h"
// Minimal provider for compiling the actual historical compatibility header.
class ecmcEcEntry {
public:
  std::shared_ptr<ecmcDataAccess> view;
  std::shared_ptr<ecmcDataAccess> getDataAccess() { return view; }
};
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
  int unlinkDataInput(ecmcDataLink *, int);
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
implementation += method(main, 'int ecmcPLCMain::setAxisArrayPointer(')
for signature in ('int ecmcPLCMain::linkDataInput(', 'int ecmcPLCMain::unlinkDataInput(',
                  'int ecmcPLCMain::linkAxisDataInput(', 'int ecmcPLCMain::validateDataInputs()', 'void ecmcPLCMain::updateDataInputHealth('):
    implementation += method(main, signature)
fixture += "\necmcPLCMain *plcs = nullptr;\n"
for signature in ('static bool validObjectPath(', 'int getMainObjectType(',
                  'int getAxSubObjectType(', 'int getAxEncFuncType(',
                  'int getAxMonFuncType(', 'int getAxDriveFuncType('):
    implementation += method(utils, signature)
for signature in ('int resolveDataInputTarget(', 'int linkPLCDataToObject(', 'int linkDataToObject(', 'int unlinkDataFromObject('):
    implementation += method(general, signature)
implementation += method(parser, 'static char *trimWhitespace(')
start = parser.index('  // Bind a PLC scalar')
end = parser.index('  /// "Cfg.SetStartupGate(enable)"', start)
implementation += "\nint parseCommand(const char *myarg_1) {\n" + r'''
  int nvals = 0, iValue = 0, iValue2 = 0;
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
  assert(producer.dataBindingUsers_ == 2 && producer.hasDataBindings());
  assert(producer.clearExpr() == ERROR_PLC_DATA_SOURCE_BOUND);
  assert(producer.localArray_[0] == &local && producer.exprStr_ == "original");
  assert(manager.linkDataInput(0, "global.missing", 2, &encoderInput, 1, false) == ERROR_PLC_VARIABLE_NOT_FOUND);
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
  assert(parseCommand("LinkPLCDataToObject(0,global.position,ax2.enc.actpos,0)") == 0);
  manager.updateDataInputHealth(2);
  assert(axis.encoder.readDataValueDouble(0, &position) == 0 && position == 8);
  assert(parseCommand("UnlinkDataFromObject(ax2.enc.actpos)") == 0);
  assert(parseCommand("LinkPLCDataToObject(0,static.limit,ax2.mon.lowlim,0)") == ERROR_MAIN_ECMC_LINK_INVALID);
  assert(parseCommand("LinkPLCDataToObject(0,static.limit,ax2.mon.lowlim,1)") == 0);
  manager.updateDataInputHealth(2);
  local.value = 1;
  assert(axis.monitor.readDataValue(0, &raw) == 0 && raw == 1);
  assert(parseCommand("UnlinkDataFromObject(ax2.mon.lowlim)") == 0);
  assert(parseCommand("LinkPLCDataToObject(0,global.position,ax2.drv.control,2)") == ERROR_MAIN_ECMC_LINK_INVALID);
  assert(parseCommand("LinkPLCDataToObject(0,global.position,ax2.drv.status,2)") == 0);
  manager.updateDataInputHealth(2);
  assert(axis.drive.readDataValue(ECMC_DRIVEBASE_ENTRY_INDEX_STATUS_WORD, &raw) == 0 && raw == 8);
  assert(parseCommand("UnlinkDataFromObject(ax2.drv.status)") == 0);
  assert(parseCommand("LinkPLCDataToObject(0,global.position,ax2.enc.actpos,0") == ERROR_MAIN_PARSER_INVALID_FORMAT);
  assert(parseCommand("UnlinkDataFromObject(ax2.enc.actpos)junk") == ERROR_MAIN_PARSER_INVALID_FORMAT);
  const std::string oversized = "LinkPLCDataToObject(0," + std::string(1024, 'x') + ",ax2.enc.actpos,0)";
  assert(parseCommand(oversized.c_str()) == ERROR_MAIN_PARSER_INVALID_FORMAT);
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
  assert(parseCommand("LinkPLCDataToObject(0,global.position,ax2.enc.actpos,0)") == ERROR_MAIN_APP_MODE_ALREADY_RUNTIME);
  assert(parseCommand("LinkDataToObject(ax1.enc.actpos,ax2.enc.actpos,0)") == ERROR_MAIN_APP_MODE_ALREADY_RUNTIME);
  appModeStat = ECMC_MODE_CONFIG;
  plcs = nullptr; axes[2] = nullptr;
  manager.plcs_[0] = nullptr; // This fixture's other producer is stack-owned.
  puts("Common data: native storage, lazy axis sources, same-axis/reciprocal links, direct-self rejection, PLC binding, faults, lifetime and unlink passed.");
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp, exe = Path(directory) / 'links.cpp', Path(directory) / 'links'
    cpp.write_text(fixture.replace('#include "ecmcEcEntryLink.h"',
        '#include "' + str(SUP / 'ethercat/ecmcEcEntryLink.h') + '"') + implementation + tests)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O2', '-Wall', '-Wextra',
                    '-I' + str(ROOT / 'tools/plc-test/offline'),
                    '-I' + str(SUP / 'main'), '-I' + str(SUP / 'com'),
                    '-I' + str(SUP / 'motion'), '-I' + str(SUP / 'plc'), '-I' + str(SUP / 'ethercat'),
                    str(cpp), str(SUP / 'com/ecmcDataItem.cpp'),
                    str(SUP / 'com/ecmcDataLink.cpp'), str(SUP / 'com/ecmcDataAccess.cpp'), str(SUP / 'main/ecmcError.cpp'),
                    str(ROOT / 'tests/monitor/support.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
