#!/usr/bin/env python3
"""Bound timing uses its own provider, with no global master or cyclic lookup.

Compile the production EtherCAT timing adapter against controlled clock/device
state. No hardware timestamp accuracy or EtherLab transport is represented.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SUP = ROOT / 'devEcmcSup'
source = (SUP / 'ethercat/ecmcEthercatSignalTiming.cpp').read_text()
checks = r'''
#include <cassert>
#include "ecmcDataLink.h"
int main() {
  ecmcEc first, second;
  auto &device = first.slaves[2];
  first.cycle.nominalPeriodNs = 1000;
  first.cycle.receiveTimeNs = 10000;
  first.sendTime = 11000;
  device.timing.valid = true;
  device.timing.cycleOffsetKnown = device.timing.eventOffsetKnown = true;
  device.timing.cycleOffset = -1;
  device.timing.eventOffsetNs = 50;
  device.timing.uncertaintyNs = 100;
  uint64_t value = 1;
  int healthy = 1;
  auto access = std::make_shared<ecmcDataAccess>();
  access->data = reinterpret_cast<uint8_t *>(&value);
  access->type = ECMC_EC_U64; access->bits = 64;
  access->validation = ecmcDataAccess::DIRECT_STATUS;
  access->validity = &healthy;
  ecmcBindEthercatTiming(first, device, *access);
  auto originalTiming = access->timing;
  ecmcBindEthercatTiming(first, device, *access);
  assert(access->timing == originalTiming);
  ecmcDataLink link;
  assert(link.setDataAtIndex(access, 0, -1) == 0);
  ecmcSignalTime time;
  assert(link.readDataTime(0, time));
  assert(time.sampleTimeNs == 9050 && time.controllerTimeNs == 11000);
  assert(time.quality == ecmcSignalTimeQuality::CYCLE_BOUNDED && time.uncertaintyNs == 100);
  second.cycle.receiveTimeNs = 999999;
  assert(link.readDataTime(0, time) && time.sampleTimeNs == 9050);
  first.cycle.receiveTimeNs += 1000;
  assert(link.readDataTime(0, time) && time.sampleTimeNs == 10050);
  healthy = 0;
  assert(!link.readDataTime(0, time) && time.sampleTimeNs == 0);
  healthy = 1;
  device.timing.valid = false;
  assert(!link.readDataTime(0, time) && time.quality == ecmcSignalTimeQuality::INVALID);
  device.timing.valid = true;
  access->data = nullptr; // Owner invalidation prevents calls through retained timing.
  assert(!link.readDataTime(0, time));
  access->data = reinterpret_cast<uint8_t *>(&value);
  access->timing.reset(); // Providers without timing never borrow another bus's clock.
  assert(!link.readDataTime(0, time));
  assert(!link.readDataTime(-1, time) && !link.readDataTime(20, time));
}
'''
with tempfile.TemporaryDirectory(prefix='ecmc-signal-timing-') as directory:
    cpp, exe = Path(directory)/'timing.cpp', Path(directory)/'timing'
    cpp.write_text(source + checks)
    command = [os.environ.get('CXX', 'c++'), '-std=c++17', '-O2',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all']
    command += ['-I'+str(ROOT/'tools/plc-test/offline')]
    command += ['-I'+str(SUP/p) for p in ('com', 'main', 'ethercat', 'motion', 'misc')]
    command += [str(cpp), str(SUP/'com/ecmcDataAccess.cpp'), str(SUP/'com/ecmcDataLink.cpp'),
                str(SUP/'com/ecmcDataItem.cpp'), str(SUP/'main/ecmcError.cpp'),
                str(ROOT/'tests/monitor/support.cpp'), '-o', str(exe)]
    subprocess.run(command, check=True)
    subprocess.run([str(exe)], check=True)
print('Signal timing: provider clocks, quality, health, missing capability and invalidation passed.')
