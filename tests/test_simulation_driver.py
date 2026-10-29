#!/usr/bin/env python3
"""Test the model-free simulation driver's actual memory and typed bindings.

Uses a publication fixture rather than EPICS. No physical transport or drive model.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SUP = ROOT / 'devEcmcSup'
checks = r'''
#include <cassert>
#include <cstring>
#include "ecmcSimulationDriver.h"
#include "ecmcAsynPortDriver.h"
#include "ecmcDataLink.h"
#include "ecmcErrorsList.h"
int main() {
  ecmcAsynPortDriver port;
  auto driver = std::make_unique<ecmcSimulationDriver>(0, &port);
  assert(driver->addDevice(0, 2, 123, 456) == 0);
  assert(driver->addEntry(2, 123, 456, 2, 3, 0x1a00, 0x6000, 1, ECMC_EC_S32, "positionActual01", true) == 0);
  assert(driver->addEntry(2, 123, 456, 1, 2, 0x1600, 0x7000, 1, ECMC_EC_U16, "controlWord01", true) == 0);
  auto input = driver->findEntry(2, "positionActual01")->access;
  auto output = driver->findEntry(2, "controlWord01")->access;
  assert(driver->addAlias(2, "positionActual01", "positionAlias") == 0);
  assert(driver->findEntry(2, "positionAlias")->access == input);
  assert(driver->setValue(2, "positionActual01", "-123") == 0);
  assert(driver->setValue(2, "controlWord01", "65536") != 0);
  ecmcDataLink encoder, drive;
  assert(encoder.setDataAtIndex(input, 0, -1) == 0);
  assert(drive.setDataAtIndex(output, 0, -1) == 0);
  assert(driver->validate() == 0 && driver->isActive());
  driver->readInputs({1,2}, {3,4});
  double position = 0;
  assert(encoder.readDataValueDouble(0, &position) == 0 && position == -123);
  assert(drive.writeDataValue(0, 15) == 0);
  driver->writeOutputs({3,4});
  assert(driver->findEntry(2, "controlWord01")->value == 15);
  // No baked-in behaviour: output commands never change input feedback.
  driver->readInputs({2,2}, {3,4});
  assert(encoder.readDataValueDouble(0, &position) == 0 && position == -123);
  auto *pv = port.find("ec0.s2.positionActual01");
  assert(pv && pv->writable && pv->bytes == sizeof(int32_t));
  const int32_t injected = 654;
  memcpy(pv->data, &injected, sizeof(injected));
  driver->readInputs({3,2}, {3,4});
  assert(encoder.readDataValueDouble(0, &position) == 0 && position == 654);
  auto *healthPV = port.find("ec0.simulation.healthy");
  assert(healthPV && healthPV->writable && healthPV->bytes == sizeof(int));
  const int unhealthy = 0;
  memcpy(healthPV->data, &unhealthy, sizeof(unhealthy)); position = 77;
  assert(!driver->updateIoStatus() && !driver->timingReady());
  assert(encoder.readDataValueDouble(0, &position) != 0 && position == 77);
  driver->resetStartupErrors(); assert(!input->valid());
  driver->setHealthy(true); assert(input->valid());
  assert(driver->setObjectTyped(2, 0x8000, 1, ECMC_EC_U64, "18446744073709551615") == 0);
  uint64_t object = 0;
  assert(driver->getObject(2, 0x8000, 1, &object, 8) == 0 && object == UINT64_MAX);
  assert(driver->getObject(2, 0x8000, 2, &object, 8) != 0);
  assert(driver->setSetting(2, "dc", {0x300, 2000000, -1000}) == 0);
  assert(driver->findDevice(2)->settings["dc"][2] == -1000);
  driver.reset();
  assert(!input->data && !output->data && !input->valid());
  assert(!pv->data && pv->bytes == 0);
  assert(!healthPV->data);
}
'''
with tempfile.TemporaryDirectory(prefix='ecmc-simulation-') as directory:
    cpp, exe = Path(directory)/'simulation.cpp', Path(directory)/'simulation'
    cpp.write_text(checks)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O2', '-Wall', '-Wextra',
        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
        '-I'+str(ROOT/'tools/plc-test/offline'), '-I'+str(SUP/'com'), '-I'+str(SUP/'main'), '-I'+str(SUP/'misc'), '-I'+str(SUP/'motion'),
        str(cpp), str(SUP/'misc/ecmcSimulationDriver.cpp'), str(SUP/'com/ecmcDataAccess.cpp'),
        str(SUP/'com/ecmcDataItem.cpp'), str(SUP/'com/ecmcDataLink.cpp'),
        str(SUP/'main/ecmcError.cpp'), str(ROOT/'tests/monitor/support.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
