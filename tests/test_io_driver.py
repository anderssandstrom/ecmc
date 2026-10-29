#!/usr/bin/env python3
"""Exercise the actual I/O interface/adapter with a controlled EtherCAT endpoint.

Checks clocks/arguments, phase order and status forwarding, not real transport.
The full cycle ordering and masterless path are covered by test_startup_gate.
"""
# DOC: ethercat-simulation-boundary.md; COMPAT: existing EtherCAT phase semantics.
from pathlib import Path
import os
import subprocess
import tempfile

SUP = Path(__file__).resolve().parents[1] / 'devEcmcSup'
fixture = r'''
#include <cassert>
#include <vector>
#include "ecmcIoDriver.h" // Must be usable without EtherCAT/EPICS headers.
struct ecmcEc {
  bool initialized = true, timing = false;
  int status = 0, resets = 0;
  timespec input{}, receiveOffset{}, outputOffset{}, fallbackOffset{};
  std::vector<int> calls;
  bool getInitDone() { calls.push_back(1); return initialized; }
  void receive(timespec time, timespec offset) {
    calls.push_back(2); input = time; receiveOffset = offset;
  }
  void checkDomainsState() { calls.push_back(3); }
  int statusOK() { calls.push_back(4); return status; }
  void send(timespec offset) { calls.push_back(5); outputOffset = offset; }
  void setTimeOffest(timespec offset) { calls.push_back(6); fallbackOffset = offset; }
  bool timingStartupReady(bool = false) const { return timing; }
  void resetStartupCommunicationErrors() { calls.push_back(7); ++resets; }
  bool checkState() { calls.push_back(8); return false; }
  void slowExecute() { calls.push_back(9); }
};
#define ECMCEC_H_
#include "ecmcEthercatDriver.h"
bool equal(timespec a, timespec b) {
  return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec;
}
int main() {
  ecmcEc master;
  ecmcEthercatDriver ethercat(master);
  ecmcIoDriver &io = ethercat;
  const timespec input = {123, 987654321};
  const timespec offset = {1700000000, 123456789};
  // Enabled transport must still process inputs/outputs when health is bad.
  assert(io.isActive());
  io.readInputs(input, offset);
  assert(io.updateIoStatus() == 0);
  io.writeOutputs(offset);
  assert(master.calls == std::vector<int>({1, 2, 3, 4, 5}));
  assert(equal(master.input, input) && equal(master.receiveOffset, offset));
  assert(equal(master.outputOffset, offset));
  master.calls.clear(); master.status = 23;
  // Do not silently coerce the existing integer status result to a Boolean.
  assert(io.updateIoStatus() == 23 && master.calls == std::vector<int>({3, 4}));
  assert(!io.timingReady()); master.timing = true; assert(io.timingReady());
  master.calls.clear();
  io.resetStartupErrors(); io.updateDiagnostics(); io.slowExecute();
  assert(master.resets == 1 && master.calls == std::vector<int>({7, 8, 9}));
  master.calls.clear(); master.initialized = false;
  assert(!io.isActive());
  io.setTimeOffset(offset); // Caller uses this instead of bus I/O in masterless mode.
  assert(master.calls == std::vector<int>({1, 6}));
  assert(equal(master.fallbackOffset, offset));
}
'''
with tempfile.TemporaryDirectory(prefix='ecmc-io-driver-') as directory:
    cpp, exe = Path(directory) / 'driver.cpp', Path(directory) / 'driver'
    cpp.write_text(fixture)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I' + str(SUP / 'com'), '-I' + str(SUP / 'ethercat'),
                    str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('I/O driver: phase order, clocks, health, startup and slow forwarding passed.')
