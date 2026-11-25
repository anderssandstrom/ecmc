#!/usr/bin/env python3
"""Check finite RT capture, partial export and replacement of snapshots."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
checks = r'''
#include "ecmcRtTimingCapture.h"
#include <cassert>
#include <fstream>
#include <string>
#include <thread>

int main() {
  ecmcRtTimingCapture capture;
  assert(!capture.collecting());
  assert(!capture.arm(0));
  assert(!capture.arm(ecmcRtTimingCapture::capacity + 1));
  assert(capture.writeCsv("timing.csv") == -1);
  assert(capture.arm(2));
  assert(!capture.arm(1));
  assert(capture.writeCsv("timing.csv") == -1);

  std::thread cyclic([&] {
    assert(capture.collecting());
    capture.record({100, 110, 120, 130, 150, 160, 170});
    assert(capture.collecting());
    capture.record({200, 210, 210, 215, 240, 0, 251});
    assert(!capture.collecting());
  });
  cyclic.join();
  assert(capture.writeCsv("missing-directory/timing.csv") == -2);
  assert(capture.writeCsv("timing.csv") == 2);

  std::ifstream file("timing.csv");
  std::string line;
  assert(std::getline(file, line));
  assert(line == "scheduled_ns,wake_ns,after_asyn_ns,after_motor_mutex_ns,"
                 "before_send_ns,after_ethercat_ns,cycle_end_ns");
  assert(std::getline(file, line) && line == "100,110,120,130,150,160,170");
  assert(std::getline(file, line) && line == "200,210,210,215,240,0,251");
  assert(!std::getline(file, line));

  assert(capture.arm(2));
  capture.record({300, 301, 302, 303, 304, 305, 306});
  capture.finish(); // Simulate leaving runtime before the capture fills.
  assert(capture.writeCsv("partial.csv") == 1 && !capture.collecting());
}
'''

with tempfile.TemporaryDirectory(prefix='ecmc-rt-timing-') as directory:
    work = Path(directory)
    cpp = work / 'timing.cpp'
    binary = work / 'timing'
    cpp.write_text(checks)
    subprocess.run([
        os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
        '-pthread', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
        '-I' + str(root / 'devEcmcSup/main'), str(cpp), '-o', str(binary),
    ], check=True)
    subprocess.run([str(binary)], cwd=work, check=True)
print('RT timing capture: bounded acquisition, CSV export, reuse and partial stop passed.')
