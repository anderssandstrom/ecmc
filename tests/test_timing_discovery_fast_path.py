#!/usr/bin/env python3
"""Exercise the production slave input loop before/after timing discovery.

No EtherCAT master is created. Count calls at the discovery and I/O boundaries
so skipping completed discovery cannot skip cyclic inputs or asynchronous SDOs.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'devEcmcSup/ethercat/ecmcEcSlave.cpp').read_text()
start = source.index('int ecmcEcSlave::updateInputProcessImage() {')
end = source.index('\nint ecmcEcSlave::updateOutProcessImage()', start)
production = source[start:end]
harness = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
using uint = unsigned int;
struct Entry {
  int calls = 0;
  void updateInputProcessImage() { ++calls; }
};
struct Sdo {
  int calls = 0;
  void execute() { ++calls; }
};
struct ecmcEcSlave {
  Entry input;
  Entry *entryListRtInput_[1] = {&input};
  uint entryCounterRtInput_ = 1;
  Sdo sdo;
  Sdo *asyncSDOvector_[1] = {&sdo};
  int asyncSDOCounter_ = 1;
  std::atomic<bool> timingStartupReady_{false};
  struct { bool discoveryAttempted = true, discoveryComplete = false; } dcSchedule_;
  struct { bool finished = false; } smTimingRequests_[2];
  uint8_t smTimingRequestCount_ = 2;
  int smCalls = 0, dcCalls = 0, publications = 0;
  void executeSmTimingDiscovery() { ++smCalls; }
  void executeDcScheduleDiscovery() { ++dcCalls; }
  void refreshTimingAsyn() { ++publications; }
  int updateInputProcessImage();
};
'''
checks = r'''
int main() {
  ecmcEcSlave s;
  s.updateInputProcessImage();
  assert(s.smCalls == 1 && s.dcCalls == 1 && !s.timingStartupReady_);
  s.dcSchedule_.discoveryComplete = true;
  s.smTimingRequests_[0].finished = true;
  s.updateInputProcessImage();
  assert(!s.timingStartupReady_ && s.publications == 0);
  s.smTimingRequests_[1].finished = true;
  s.updateInputProcessImage();
  assert(s.timingStartupReady_ && s.publications == 1);
  assert(s.smCalls == 3 && s.dcCalls == 3);
  for (int i = 0; i < 1000; ++i) s.updateInputProcessImage();
  assert(s.smCalls == 3 && s.dcCalls == 3 && s.publications == 1);
  assert(s.input.calls == 1003 && s.sdo.calls == 1003);
  // Reconfiguration clears readiness and must resume discovery.
  s.timingStartupReady_ = false;
  s.dcSchedule_.discoveryComplete = false;
  s.updateInputProcessImage();
  assert(s.smCalls == 4 && s.dcCalls == 4 && !s.timingStartupReady_);
  // A slave without discovery requests must complete immediately.
  ecmcEcSlave unused;
  unused.smTimingRequestCount_ = 0;
  unused.dcSchedule_.discoveryAttempted = false;
  unused.updateInputProcessImage();
  assert(unused.timingStartupReady_ && unused.publications == 1);
  unused.updateInputProcessImage();
  assert(unused.smCalls == 1 && unused.dcCalls == 1);
  assert(unused.input.calls == 2 && unused.sdo.calls == 2);
}
'''
with tempfile.TemporaryDirectory(prefix='ecmc-timing-fast-path-') as directory:
    cpp = Path(directory) / 'check.cpp'
    exe = Path(directory) / 'check'
    cpp.write_text(harness + production + checks)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O2',
                    '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Timing discovery: pending, completion, steady-state I/O, reset and unused slave passed.')
