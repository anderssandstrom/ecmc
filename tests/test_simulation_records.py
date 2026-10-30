#!/usr/bin/env python3
"""Real simulation-record generator with controlled EPICS static-database calls.

DOC: simulation-io.md. Tests record definitions and bound native buffers, not
CA/PVA transport, EPICS record processing or real asyn conversion code.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SUP = ROOT/'devEcmcSup'
headers = {
    'dbDefs.h': '#define PVNAME_STRINGSZ 61\n',
    'dbAccess.h': '#pragma once\nstruct DBBASE {};\nextern DBBASE *pdbbase;\n',
    'dbStaticLib.h': '''#pragma once
#include "dbAccess.h"
#include <cstdio>
struct DBENTRY {};
void dbInitEntry(DBBASE *, DBENTRY *);
void dbFinishEntry(DBENTRY *);
long dbFindRecordType(DBENTRY *, const char *);
long dbFindRecord(DBENTRY *, const char *);
long dbReadDatabaseFP(DBBASE **, FILE *, const char *, const char *);
''',
}
fixture = r'''
#include <cassert>
#include <cstring>
#include <regex>
#include <set>
#include <string>
#include "ecmcIoConfig.h"
#include "ecmcAsynPortDriver.h"
#include "ecmcDataLink.h"
#include "ecmcErrorsList.h"
#include "dbStaticLib.h"
DBBASE database, *pdbbase = &database;
app_mode_type appModeCmd = ECMC_MODE_CONFIG;
extern app_mode_type appModeStat;
bool iocRunning = false, failLoad = false;
extern "C" int ecmcIocRunning() { return iocRunning; }
std::vector<std::unique_ptr<ecmcSimulationDriver>> drivers;
const std::vector<std::unique_ptr<ecmcSimulationDriver>> &ecmcSimulationDrivers() { return drivers; }
std::set<std::string> records;
std::string definitions, missingType;
int loads = 0, dbEntries = 0;
void dbInitEntry(DBBASE *, DBENTRY *) { ++dbEntries; }
void dbFinishEntry(DBENTRY *) { --dbEntries; }
long dbFindRecordType(DBENTRY *, const char *name) { return missingType == name ? 1 : 0; }
long dbFindRecord(DBENTRY *, const char *name) { return records.count(name) ? 0 : 1; }
long dbReadDatabaseFP(DBBASE **, FILE *file, const char *, const char *) {
  ++loads;
  definitions.clear();
  char buffer[1024];
  while (const size_t n = fread(buffer,1,sizeof(buffer),file)) definitions.append(buffer,n);
  fclose(file); // Same ownership contract as EPICS dbReadDatabaseFP.
  if (failLoad) return 1;
  const std::regex pattern("record\\([^,]+,\"([^\"]+)\"\\)");
  for (auto i = std::sregex_iterator(definitions.begin(),definitions.end(),pattern);
       i != std::sregex_iterator(); ++i) {
    assert((*i)[1].str().find('.') == std::string::npos);
    assert(records.insert((*i)[1]).second);
  }
  return 0;
}
int main() {
  appModeStat = ECMC_MODE_CONFIG;
  ecmcAsynPortDriver port;
  drivers.emplace_back(new ecmcSimulationDriver(0,&port));
  auto &driver = *drivers[0];
  assert(driver.addDevice(0,14,2,123) == 0);
  assert(driver.addEntry(14,2,123,2,3,0,0,0,ECMC_EC_U16,"driveStatus01",true) == 0);
  assert(driver.addEntry(14,2,123,1,2,0,0,0,ECMC_EC_U16,"driveControl01",true) == 0);
  assert(driver.addEntry(14,2,123,2,3,0,0,0,ECMC_EC_S32,"positionActual01",true) == 0);
  assert(driver.addEntry(14,2,123,2,3,0,0,0,ECMC_EC_F64,"analogInput01",true) == 0);
  assert(driver.addEntry(14,2,123,2,3,0,0,0,ECMC_EC_U64,"timestamp01",true) == 0);
  assert(driver.addEntry(14,2,123,2,3,0,0,0,ECMC_EC_U32,"disabled",false) == 0);
  assert(driver.addAlias(14,"positionActual01","positionAlias") == 0);
  const char *prefix = "TEST:SIM:";
  // Preflight refuses names/types/collisions before adding any records/views.
  assert(loadSimulationRecords("bad\"prefix") != 0 && loads == 0);
  assert(loadSimulationRecords("bad.prefix:") != 0 && loads == 0);
  assert(loadSimulationRecords(std::string(60,'X').c_str()) != 0 && loads == 0);
  records.insert("TEST:SIM:ec0:s14:positionActual01-RB");
  assert(loadSimulationRecords(prefix) == ERROR_MAIN_SIM_RECORDS_LOAD_FAILED && loads == 0);
  assert(!port.find("sim.ec0.s14.driveStatus01"));
  records.clear(); missingType = "int64in";
  assert(loadSimulationRecords(prefix) == ERROR_MAIN_SIM_RECORDS_LOAD_FAILED && loads == 0);
  missingType.clear();
  records.insert("TEST:SIM:ec0:s14:driveControl01-Set");
  assert(loadSimulationRecords(prefix) == ERROR_MAIN_SIM_RECORDS_LOAD_FAILED && loads == 0);
  records.clear();
  assert(loadSimulationRecords(prefix) == 0 && loads == 1 && dbEntries == 0);
  assert(records.size() == 10); // Input injection/readback pairs and output Set/readback pair.
  assert(records.count("TEST:SIM:ec0:s14:driveStatus01"));
  assert(records.count("TEST:SIM:ec0:s14:driveStatus01-RB"));
  assert(records.count("TEST:SIM:ec0:s14:driveControl01"));
  assert(records.count("TEST:SIM:ec0:s14:driveControl01-Set"));
  assert(!records.count("TEST:SIM:ec0:s14:driveControl01-RB"));
  assert(!records.count("TEST:SIM:ec0:s14:ZERO") && !records.count("TEST:SIM:ec0:s14:disabled"));
  assert(definitions.find("record(mbboDirect,\"TEST:SIM:ec0:s14:driveStatus01\")") != std::string::npos);
  assert(definitions.find("record(mbbiDirect,\"TEST:SIM:ec0:s14:driveControl01\")") != std::string::npos);
  assert(definitions.find("record(mbboDirect,\"TEST:SIM:ec0:s14:driveControl01-Set\")") != std::string::npos);
  assert(definitions.find("record(longout,\"TEST:SIM:ec0:s14:positionActual01\")") != std::string::npos);
  assert(definitions.find("record(ao,\"TEST:SIM:ec0:s14:analogInput01\")") != std::string::npos);
  assert(definitions.find("record(int64out,\"TEST:SIM:ec0:s14:timestamp01\")") != std::string::npos);
  assert(definitions.find("@asynMask(SIM_TEST,0,0xffffffff,1)") != std::string::npos);
  assert(definitions.find("field(NOBT,\"16\")") != std::string::npos);
  assert(definitions.find("field(PINI,\"NO\")") != std::string::npos);
  assert(loadSimulationRecords(prefix) == 0 && loads == 1); // No duplicates on repeat.
  assert(loadSimulationRecords("OTHER:") == ERROR_MAIN_IO_BACKEND_LOCKED);
  auto *status = port.find("sim.ec0.s14.driveStatus01");
  auto *control = port.find("sim.ec0.s14.driveControl01");
  auto *position = port.find("sim.ec0.s14.positionActual01");
  assert(status && status->writable && control && control->writable);
  assert(control->data == port.find("ec0.s14.driveControl01")->data);
  assert(position->data == port.find("ec0.s14.positionActual01")->data);
  ecmcDataLink encoder, drive;
  assert(encoder.setDataAtIndex(driver.findEntry(14,"positionActual01")->access,0,-1) == 0);
  assert(drive.setDataAtIndex(driver.findEntry(14,"driveControl01")->access,0,0) == 0);
  assert(driver.validate() == 0);
  int32_t raw = -123;
  memcpy(position->data,&raw,sizeof(raw)); // Stand-in for the asyn write boundary.
  driver.readInputs({},{});
  double actual = 0;
  assert(encoder.readDataValueDouble(0,&actual) == 0 && actual == -123);
  assert(drive.writeDataValue(0,1) == 0);
  driver.writeOutputs({});
  assert(control->lastRTValue == 1); // Same storage; no copied model image.
  uint64_t manual = 0;
  memcpy(control->data,&manual,sizeof(manual)); // Manual -Set write, same bound buffer.
  driver.writeOutputs({});
  assert(control->lastRTValue == 0);
  assert(drive.writeDataValue(0,1) == 0); // Cyclic owner may overwrite the manual value.
  driver.writeOutputs({});
  assert(control->lastRTValue == 1);
  auto *stamp = port.find("sim.ec0.s14.timestamp01");
  uint64_t wide = UINT64_MAX;
  memcpy(stamp->data,&wide,sizeof(wide));
  uint64_t exact = 0;
  assert(driver.findEntry(14,"timestamp01")->access->readValue(&exact) == 0 && exact == wide);
  assert(driver.addEntry(14,2,123,2,3,0,0,0,ECMC_EC_U8,"later",true) == 0);
  failLoad = true;
  assert(loadSimulationRecords(prefix) == ERROR_MAIN_SIM_RECORDS_LOAD_FAILED);
  failLoad = false;
  assert(loadSimulationRecords(prefix) == 0 && records.count("TEST:SIM:ec0:s14:later"));
  iocRunning = true;
  assert(loadSimulationRecords(prefix) == ERROR_MAIN_APP_MODE_ALREADY_RUNTIME);
  iocRunning = false; appModeCmd = ECMC_MODE_RUNTIME;
  assert(loadSimulationRecords(prefix) == ERROR_MAIN_APP_MODE_ALREADY_RUNTIME);
  drivers.clear();
  assert(!position->data && !control->data && !status->data);
}
'''
with tempfile.TemporaryDirectory(prefix='ecmc-simulation-records-') as directory:
    directory = Path(directory)
    for name, text in headers.items():
        (directory/name).write_text(text)
    cpp, exe = directory/'records.cpp', directory/'records'
    cpp.write_text(fixture)
    includes = [directory, ROOT/'tools/plc-test/offline', SUP/'com', SUP/'main', SUP/'misc', SUP/'motion']
    sources = [cpp, SUP/'main/ecmcSimulationRecords.cpp', SUP/'misc/ecmcSimulationDriver.cpp',
               SUP/'com/ecmcDataAccess.cpp', SUP/'com/ecmcDataItem.cpp', SUP/'com/ecmcDataLink.cpp',
               SUP/'main/ecmcError.cpp', ROOT/'tests/monitor/support.cpp']
    subprocess.run([os.environ.get('CXX','c++'), '-std=c++17','-O2','-Wall','-Wextra',
        '-DECMC_ASYN_ASYNPARAMINT64', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
        *['-I'+str(path) for path in includes], *map(str,sources), '-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('Simulation records: types, names, shared buffers, permissions, preflight, lifecycle and output publication passed.')
