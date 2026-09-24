#!/usr/bin/env python3
"""Compile the production error scan with lightweight stand-ins (no EPICS needed)."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'devEcmcSup/main/ecmcGeneral.cpp').read_text()
# Exercise the actual implementation, excluding unrelated EPICS commands.
implementation = source[source.index('namespace {'):source.index('int controllerErrorReset()')]
definitions = (ROOT / 'devEcmcSup/main/ecmcDefinitions.h').read_text()
constants = '\n'.join(re.findall(
    r'^#define ECMC_MAX_(?:AXES|DATA_STORAGE_OBJECTS|PLUGINS)\s+\d+',
    definitions, re.M))
stubs = r'''
#include <cassert>
#include <cstddef>
#include <thread>
#include <random>
struct Error {
  int value = 0;
  bool getError() { return value != 0; }
  int getErrorID() { return value; }
  bool getInitDone() { return true; }
};
struct ecmcDataStorage : Error {};
struct ecmcAxisBase : Error {};
struct ecmcPluginLib : Error {};
struct ecmcCppLogicLib : Error {};
ecmcDataStorage *dataStorages[ECMC_MAX_DATA_STORAGE_OBJECTS] = {};
ecmcAxisBase *axes[ECMC_MAX_AXES] = {};
ecmcPluginLib *plugins[ECMC_MAX_PLUGINS] = {};
ecmcCppLogicLib *cppLogics[ECMC_MAX_PLUGINS] = {};
Error master, plc, pvt;
Error *ec = &master, *plcs = &plc, *pvtCtrl_ = &pvt;
int pluginsError = 0, cppLogicError = 0, shmAccessError = 0;
'''
tests = r'''
int main() {
  prepareControllerErrorObjectsRT();
  assert(getControllerError() == 0);
  assert(errorObjectsRT.axes.count == 0);
  clearControllerErrorObjectsRT();

  ecmcDataStorage storageLow, storageHigh;
  ecmcAxisBase axis;
  ecmcPluginLib plugin;
  ecmcCppLogicLib logic;
  dataStorages[2] = &storageLow;
  dataStorages[ECMC_MAX_DATA_STORAGE_OBJECTS - 1] = &storageHigh;
  axes[ECMC_MAX_AXES - 1] = &axis;
  plugins[ECMC_MAX_PLUGINS - 1] = &plugin;
  cppLogics[ECMC_MAX_PLUGINS - 1] = &logic;
  Error *priority[] = {&master, &storageLow, &storageHigh, &plc,
                      &axis, &pvt, &plugin, &logic};
  prepareControllerErrorObjectsRT();
  assert(errorObjectsRT.storages.count == 2);
  assert(errorObjectsRT.axes.count == 1);
  // All priorities, with lower-priority errors simultaneously present.
  for (int i = 0; i < 8; ++i) priority[i]->value = 100 + i;
  pluginsError = 200;
  cppLogicError = 201;
  shmAccessError = 202;
  for (int i = 0; i < 8; ++i) {
    assert(getControllerError() == 100 + i);
    priority[i]->value = 0;
  }
  assert(getControllerError() == 200);
  pluginsError = 0;
  assert(getControllerError() == 201);
  cppLogicError = 0;
  assert(getControllerError() == 202);
  shmAccessError = 0;
  assert(getControllerError() == 0);

  // Compare live error transitions in sparse and compact paths.
  std::mt19937 rng(17);
  for (int cycle = 0; cycle < 10000; ++cycle) {
    for (auto *object : priority) object->value = rng() % 3 ? 0 : rng() % 100 + 1;
    int compact = getControllerError();
    clearControllerErrorObjectsRT();
    assert(getControllerError() == compact);
    prepareControllerErrorObjectsRT();
  }
  for (auto *object : priority) object->value = 0;

  // A separate command thread must use live membership, not the RT snapshot.
  ecmcAxisBase newAxis;
  newAxis.value = 333;
  axes[0] = &newAxis;
  std::thread command([] { assert(getControllerError() == 333); });
  command.join();
  assert(getControllerError() == 0);
  clearControllerErrorObjectsRT();
  assert(getControllerError() == 333);

  // Runtime restart rebuilds membership, including removal/replacement.
  axes[ECMC_MAX_AXES - 1] = nullptr;
  prepareControllerErrorObjectsRT();
  assert(errorObjectsRT.axes.count == 1);
  assert(getControllerError() == 333);
  clearControllerErrorObjectsRT();
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / 'error_scan.cpp'
    exe = Path(directory) / 'error_scan'
    cpp.write_text(constants + '\n' + stubs + implementation + tests)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11', '-O2',
                    '-Wall', '-Wextra', '-Werror', '-pthread', str(cpp),
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Controller error scan: priority, live errors, thread isolation, and restart checks passed.')
