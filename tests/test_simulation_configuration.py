#!/usr/bin/env python3
"""Exercise memory-backend selection and actual configuration-routing branches.

The hardware fallback is a counter fixture, not EtherLab. Every new simulation
branch is compiled with its production signature; storage/registry are real code.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SUP = ROOT/'devEcmcSup'
api = (SUP/'ethercat/ecmcEthercat.cpp').read_text()
utils = (SUP/'com/ecmcAsynPortDriverUtils.cpp').read_text()
parser = (SUP/'com/ecmcCmdParser.c').read_text()
configuration = (SUP/'main/ecmcIoConfig.cpp').read_text()
motion = (SUP/'motion/ecmcMotion.cpp').read_text()


def method(text, signature):
    a = text.index(signature)
    brace = text.index('{', a)
    i, depth = brace+1, 1
    while depth:
        depth += (text[i] == '{') - (text[i] == '}')
        i += 1
    return text[a:i]


fixture = r'''
#include <cassert>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include "ecmcIoConfig.h"
#include "ecmcErrorsList.h"
#include "ecmcEthercat.h"
#include "ecmcMotion.h"
#include "ecmcDataLink.h"
struct ConstantEntry {
  uint64_t value = 0;
  bool failExport = false;
  std::shared_ptr<ecmcDataAccess> access = std::make_shared<ecmcDataAccess>();
  explicit ConstantEntry(ecmcEcDataType type) {
    access->data = reinterpret_cast<uint8_t *>(&value);
    access->identity = this; access->type = type;
    access->bits = ecmcDataAccess::sizeOf(type)*8;
    access->wordStorage = true; access->writable = true;
  }
  std::shared_ptr<ecmcDataAccess> getDataAccess() {
    if (failExport) throw std::bad_alloc();
    return access;
  }
};
struct Constants {
  ConstantEntry position{ECMC_EC_S32}, control{ECMC_EC_U16};
  int motionChecks = 0;
  ConstantEntry *findEntry(const std::string &name) {
    if (name == "positionActual01") return &position;
    if (name == "controlWord01") return &control;
    return nullptr;
  }
  int setEnableSDOCheck(int value) { motionChecks = value; return 0; }
};
struct Hardware {
  int requests = 0, id = -1;
  Constants device;
  bool getInitDone() { return requests != 0; }
  int getMasterIndex() { return id; }
  Constants *findSlave(int position) { return requests && position == 2 ? &device : nullptr; }
  Constants *getSlave(int position) { return requests && position == -1 ? &device : nullptr; }
  int init(int value) { ++requests; id = value; return 0; }
} hardware, *ec = &hardware;
ecmcAsynPortDriver *asynPort = nullptr;
app_mode_type appModeCmd = ECMC_MODE_CONFIG;
extern app_mode_type appModeStat;
int sampleRateChangeAllowed = 1;
#undef LOGINFO4
#define LOGINFO4(...) ((void)0)
#undef LOGERR
#define LOGERR(...) ((void)0)
struct Drive : ecmcDataLink {
  bool brake = false, reduce = false;
  int setEnableBrake(int value) { brake = value; return 0; }
  int setEnableReduceTorque(int value) { reduce = value; return 0; }
};
struct Axis {
  bool blocked = false, homeEnabled = false, interlockEnabled = false;
  ecmcDataLink encoder, monitor;
  Drive drive;
  bool getBlockCom() { return blocked; }
  void setExternalCommandBlockedError() {}
  ecmcDataLink *getConfigEnc() { return &encoder; }
  ecmcDataLink *getMon() { return &monitor; }
  Drive *getDrv() { return &drive; }
};
Axis *axes[ECMC_MAX_AXES] = {};
int setAxisMonEnableExternalInterlock(int axis, int value) { axes[axis]->interlockEnabled = value; return 0; }
'''
# Compile the actual registry and complete numeric motion-link functions.
# Both providers resolve into the real common data-link implementation.
registry = configuration[configuration.index('namespace {'):]
implementation = method(utils, 'int parseEcPath(')
implementation += method(utils, 'ecmcEcDataType getEcDataTypeFromStr(')
implementation += method(utils, 'ecmcEcDataType getEcDataType(')
implementation += registry
for name in ("linkEcEntryToAxisEnc", "linkEcEntryToAxisDrv", "linkEcEntryToAxisMon"):
    implementation += method(motion, "int "+name+"(")
for match in re.finditer(r'^int (ec[A-Z]\w*|readEc\w*|writeEc\w*)\s*\(', api, re.M):
    body = method(api, 'int '+match[1]+'(')
    if match[1] == 'ecSetMaster':
        implementation += body
        continue
    brace = body.index('{')
    first = body[brace+1:].lstrip()
    if first.startswith('if (') and 'ecmc' in first[:90]:
        route = method(first, 'if (')
        implementation += body[:brace+1]+'\n'+route+'\nreturn -900;\n}\n'
    elif match[1] == 'ecAddSimEntryGlobal':
        implementation += body
implementation += method(parser, 'static char *trimWhitespace(')
a = parser.index('  // Backend selection precedes device creation;')
z = parser.index('  // Bind a scalar source', a)
implementation += '''
int command(const char *myarg_1) {
  int nvals = 0, iValue = 0, iValue2 = 0;
  char cIdBuffer[1024] = {}, cIdBuffer2[1024] = {};
''' + parser[a:z] + '\nreturn -901;\n}\n'
checks = r'''
// Define after the production caller so only ecmcMotion.h declares this API.
// A fixture declaration must not conceal a missing production declaration.
int setAxisHomeSwitchEnable(int axis, int value) { axes[axis]->homeEnabled = value; return 0; }
// Exercise identical binding commands and side effects against both providers.
void checkMotionBindings(Axis &axis, uint64_t &positionValue, uint64_t &controlValue) {
  char position[] = "positionActual01", control[] = "controlWord01", empty[] = "", missing[] = "missing";
  positionValue = 123; controlValue = 0;
  assert(linkEcEntryToAxisEnc(2,position,0,0,-1) == 0);
  double actual = 0;
  assert(axis.encoder.readDataValueDouble(0,&actual) == 0 && actual == 123);
  assert(linkEcEntryToAxisDrv(2,control,0,ECMC_DRIVEBASE_ENTRY_INDEX_BRAKE_OUTPUT,3) == 0);
  assert(axis.drive.brake);
  assert(axis.drive.writeDataValue(ECMC_DRIVEBASE_ENTRY_INDEX_BRAKE_OUTPUT,1) == 0);
  assert(controlValue == 8);
  assert(linkEcEntryToAxisDrv(2,empty,0,ECMC_DRIVEBASE_ENTRY_INDEX_BRAKE_OUTPUT,-1) == 0);
  assert(!axis.drive.brake);
  assert(linkEcEntryToAxisDrv(2,control,0,ECMC_DRIVEBASE_ENTRY_INDEX_REDUCE_TORQUE_OUTPUT,4) == 0);
  assert(axis.drive.reduce);
  assert(linkEcEntryToAxisDrv(2,empty,0,ECMC_DRIVEBASE_ENTRY_INDEX_REDUCE_TORQUE_OUTPUT,-1) == 0);
  assert(!axis.drive.reduce);
  axis.homeEnabled = axis.interlockEnabled = false;
  assert(linkEcEntryToAxisMon(2,control,0,ECMC_MON_ENTRY_INDEX_HOMESENSOR,3) == 0);
  assert(axis.homeEnabled);
  uint64_t home = 0;
  assert(axis.monitor.readDataValue(ECMC_MON_ENTRY_INDEX_HOMESENSOR,&home) == 0 && home == 1);
  assert(linkEcEntryToAxisMon(2,control,0,ECMC_MON_ENTRY_INDEX_EXTINTERLOCK,4) == 0);
  assert(axis.interlockEnabled);
  assert(linkEcEntryToAxisEnc(2,missing,0,0,-1) == ERROR_MAIN_EC_ENTRY_NULL);
  assert(linkEcEntryToAxisMon(99,control,0,0,-1) == ERROR_MAIN_EC_SLAVE_NULL);
  assert(linkEcEntryToAxisEnc(2,position,0,-1,-1) == ERROR_MAIN_ENCODER_ENTRY_INDEX_OUT_OF_RANGE);
  assert(linkEcEntryToAxisDrv(2,control,0,-1,-1) == ERROR_MAIN_DRIVE_ENTRY_INDEX_OUT_OF_RANGE);
  assert(linkEcEntryToAxisMon(2,control,0,-1,-1) == ERROR_MAIN_MONITOR_ENTRY_INDEX_OUT_OF_RANGE);
  axis.blocked = true;
  assert(linkEcEntryToAxisEnc(2,position,0,0,-1) == ERROR_MAIN_AXIS_EXTERNAL_COM_DISABLED);
  assert(linkEcEntryToAxisDrv(2,control,0,0,-1) == ERROR_MAIN_AXIS_EXTERNAL_COM_DISABLED);
  assert(linkEcEntryToAxisMon(2,control,0,0,-1) == ERROR_MAIN_AXIS_EXTERNAL_COM_DISABLED);
  axis.blocked = false;
}
int main() {
  appModeStat = ECMC_MODE_CONFIG;
  assert(command("SetIoBackend(0,1)") == 0);
  assert(ecSetMaster(0) == 0 && hardware.requests == 0);
  assert(setIoBackend(0,0) == ERROR_MAIN_IO_BACKEND_LOCKED);
  assert(ecAddSlave(0,2,123,456) == 0);
  char s32[] = "S32", position[] = "positionActual01", u16[] = "U16", control[] = "controlWord01";
  assert(ecAddEntry(2,123,456,2,3,0x1a00,0x6000,1,s32,position,1) == 0);
  assert(ecAddEntry(2,123,456,1,2,0x1600,0x7000,1,u16,control,1) == 0);
  assert(ecSlaveConfigDC(2,0x300,2000000,-1000,0,0) == 0);
  assert(ecSlaveConfigWatchDog(2,2498,1000) == 0);
  char value[] = "1024";
  assert(ecAddSdoDT(2,0x8000,1,value,u16) == 0);
  uint32_t readback = 0;
  assert(ecReadSdo(2,0x8000,1,2,&readback) == 0 && readback == 1024);
  assert(ecVerifySdo(2,0x8000,1,1024,2) == 0);
  assert(ecAddSdoBuffer(2,0x8000,2,"01 02 ff",3) == 0);
  uint8_t bytes[3] = {};
  auto *simulation = ecmcFindSimulation(0);
  // slaveVerify.cmd immediately writes the factory-reset SDO after verification.
  // Store the request in simulation without resetting or requesting hardware.
  assert(ecVerifySlave(0,3,0x2,0x13893052,0) == 0);
  assert(ecWriteSdo(3,0x1011,1,1684107116,4) == 0);
  uint32_t resetRequest = 0;
  assert(ecReadSdo(3,0x1011,1,4,&resetRequest) == 0 && resetRequest == 1684107116);
  assert(ecWriteSdo(3,0x1011,1,0,0) == ERROR_MAIN_ECMC_COMMAND_FORMAT_ERROR);
  assert(ecWriteSdo(3,0x1011,1,0,5) == ERROR_MAIN_ECMC_COMMAND_FORMAT_ERROR);
  assert(ecReadSdo(3,0x1011,1,4,&resetRequest) == 0 && resetRequest == 1684107116);
  assert(ecWriteSdo(99,0x1011,1,0,4) != 0);
  assert(hardware.requests == 0);
  assert(simulation->getObject(2,0x8000,2,bytes,3) == 0 && bytes[2] == 255);
  assert(simulation->findDevice(2)->settings["dc"][2] == -1000);
  assert(command("SimSetValue(ec0.s2.positionActual01,-123)") == 0);
  assert(command("SimSetValue(ec0.s2.controlWord01,15)") == 0);
  assert(simulation->findEntry(2,control)->value == 15);
  Axis axis; axes[0] = &axis;
  assert(linkEcEntryToAxisEnc(2,position,0,0,-1) == 0);
  assert(linkEcEntryToAxisDrv(2,control,0,0,3) == 0);
  assert(linkEcEntryToAxisMon(2,control,0,ECMC_MON_ENTRY_INDEX_HOMESENSOR,0) == 0);
  assert(axis.homeEnabled);
  assert(ecmcValidateSimulations() == 0 && simulation->isActive());
  double actual = 0;
  assert(axis.encoder.readDataValueDouble(0,&actual) == 0 && actual == -123);
  assert(axis.drive.writeDataValue(0,0) == 0 && simulation->findEntry(2,control)->value == 7);
  axis.blocked = true;
  assert(linkEcEntryToAxisEnc(2,position,0,0,-1) == ERROR_MAIN_AXIS_EXTERNAL_COM_DISABLED);
  axis.blocked = false;
  { ecmcIoConfigurationScope hardwareContext(1); assert(!ecmcSelectedSimulation()); }
  assert(ecmcSelectedSimulation() == simulation);
  assert(command("SimSetHealthy(0,0)") == 0 && !simulation->updateIoStatus());
  assert(command("SimSetHealthy(0,1)") == 0 && simulation->updateIoStatus());
  checkMotionBindings(axis, simulation->findEntry(2,position)->value, simulation->findEntry(2,control)->value);
  assert(simulation->findDevice(2)->settings["enableSDOCheck"][0] == 1);
  // Mixed ownership: hardware selection does not change the simulation's data.
  assert(ecSetMaster(1) == 0 && hardware.requests == 1);
  assert(!ecmcSelectedSimulation() && ecmcFindSimulation(0) == simulation);
  checkMotionBindings(axis, hardware.device.position.value, hardware.device.control.value);
  assert(hardware.device.motionChecks == 1);
  hardware.device.position.failExport = true;
  assert(linkEcEntryToAxisEnc(2,position,0,0,-1) == ERROR_EC_ENTRY_LINK_FAILED);
  hardware.device.position.failExport = false;
  // Negative slave IDs retain the legacy constants lookup path.
  assert(linkEcEntryToAxisEnc(-1,position,0,0,-1) == 0);
  assert(command("SimSetValue(ec0.s2.positionActual01,42)") == 0);
  assert(hardware.requests == 1);
  assert(ecSetMaster(0) == 0 && ecmcSelectedSimulation() == simulation);
  assert(hardware.requests == 1);
  assert(command("SetIoBackend(1,1)") == ERROR_MAIN_IO_BACKEND_LOCKED);
  assert(command("SetIoBackend(0,1)junk") == ERROR_MAIN_PARSER_INVALID_FORMAT);
  appModeCmd = ECMC_MODE_RUNTIME; appModeStat = ECMC_MODE_RUNTIME;
  assert(setIoBackend(2,1) == ERROR_MAIN_APP_MODE_ALREADY_RUNTIME);
  auto retained = simulation->findEntry(2,position)->access;
  appModeCmd = ECMC_MODE_CONFIG; appModeStat = ECMC_MODE_CONFIG;
  ecmcClearSimulations();
  assert(!retained->data && !retained->valid());
}
'''
with tempfile.TemporaryDirectory(prefix='ecmc-simulation-config-') as directory:
    # The real command parser is C: its new API header must stay C-compatible.
    c_header = Path(directory)/'commands.c'
    c_header.write_text('#include "ecmcIoConfig.h"\nint probe(void) { return setIoBackend(0,1); }\n')
    subprocess.run([os.environ.get('CC','cc'), '-std=c11', '-fsyntax-only',
        '-I'+str(SUP/'main'), str(c_header)], check=True)
    cpp, exe = Path(directory)/'configuration.cpp', Path(directory)/'configuration'
    cpp.write_text(fixture+implementation+checks)
    subprocess.run([os.environ.get('CXX','c++'), '-std=c++17','-O2','-Wall','-Wextra',
        '-Wno-unused-parameter', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
        '-I'+str(ROOT/'tools/plc-test/offline'), '-I'+str(SUP/'com'), '-I'+str(SUP/'main'),
        '-I'+str(SUP/'misc'), '-I'+str(SUP/'motion'), '-I'+str(SUP/'ethercat'),
        str(cpp),str(SUP/'misc/ecmcSimulationDriver.cpp'),str(SUP/'com/ecmcDataAccess.cpp'),
        str(SUP/'com/ecmcDataItem.cpp'),str(SUP/'com/ecmcDataLink.cpp'),
        str(SUP/'main/ecmcError.cpp'),str(ROOT/'tests/monitor/support.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
