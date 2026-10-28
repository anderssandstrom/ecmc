#!/usr/bin/env python3
"""Check production final-validation order for deferred PLC input links.

Fake endpoints model call order/failure propagation, not IOC execution or motion.
"""
# INFERRED: deferred PLC links resolve before axis input validation; COMPAT:
# existing validation order remains unchanged without pending declarations.
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'devEcmcSup/main/ecmcMainThread.cpp').read_text()
start = source.index('int validateConfig() {')
brace = source.index('{', start)
end, depth = brace + 1, 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
validation = source[start:end]
fixture = r'''
#include <cassert>
#include <vector>
#define LOGINFO4(...) ((void)0)
#define LOGERR(...) ((void)0)
const int ECMC_MAX_AXES = 2, ECMC_MAX_MST_SLVS_SMS = 2;
std::vector<int> trace;
struct Bus {
  bool initialized = false;
  int error = 0;
  bool getInitDone() { return initialized; }
  int checkReadyForRuntime() { trace.push_back(1); return error; }
} bus, *ec = &bus;
struct PLCs {
  bool pending = false, resolved = false;
  int error = 0;
  bool hasPendingDataInputs() const { return pending; }
  int validate() {
    trace.push_back(2);
    if (!error) { resolved = true; pending = false; }
    return error;
  }
} producer, *plcs = &producer;
struct Axis {
  bool requiresResolved = false;
  int error = 0;
  int validate() {
    trace.push_back(3);
    if (requiresResolved) assert(producer.resolved);
    return error;
  }
} axis, *axes[ECMC_MAX_AXES] = {&axis, nullptr};
struct SM {
  int validate() { trace.push_back(4); return 0; }
  int validateAxisOwnership(SM *) { trace.push_back(5); return 0; }
} stateMachine, *masterSlaveSMs[ECMC_MAX_MST_SLVS_SMS] = {&stateMachine, nullptr};
'''
checks = r'''
int main() {
  // Legacy sequence remains axis -> state machine -> PLC.
  assert(validateConfig() == 0 && trace == std::vector<int>({3, 4, 2}));
  trace.clear();
  producer.pending = true; producer.resolved = false;
  axis.requiresResolved = true;
  assert(validateConfig() == 0 && trace == std::vector<int>({2, 3, 4}));
  trace.clear();
  producer.pending = true; producer.resolved = false; producer.error = 73;
  assert(validateConfig() == 73 && trace == std::vector<int>({2}));
  trace.clear(); producer.error = 0;
  assert(validateConfig() == 0 && trace == std::vector<int>({2, 3, 4}));
  trace.clear();
  // A failed axis check still prevents successful final validation.
  producer.pending = true; axis.error = 74;
  assert(validateConfig() == 74 && trace == std::vector<int>({2, 3}));
  trace.clear(); axis.error = 0;
  bus.initialized = true; bus.error = 75;
  assert(validateConfig() == 75 && trace == std::vector<int>({1}));
}
'''
with tempfile.TemporaryDirectory(prefix='ecmc-link-startup-') as directory:
    cpp, exe = Path(directory) / 'validate.cpp', Path(directory) / 'validate'
    cpp.write_text(fixture + validation + checks)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Wno-unused-but-set-variable',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Deferred PLC inputs: validation order and failure propagation passed.')
