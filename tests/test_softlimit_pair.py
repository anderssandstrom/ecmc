#!/usr/bin/env python3
"""Test production monitor pair restoration and its enable-state policy."""
# Expectation basis: DOC: restore pair contract; COMPAT: numeric errors/internal state.
# Rationale, sources and open questions: EXPECTATIONS.md
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'devEcmcSup/motion/ecmcMonitor.cpp').read_text()
header = (root / 'devEcmcSup/main/ecmcErrorsList.h').read_text()
def method(signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]
constants = '\n'.join(x for x in header.splitlines() if x.startswith('#define ERROR_MON_TOL_OUT_OF_RANGE'))
code = r'''
#include <cassert>
#include <cmath>
#include <limits>
#include <initializer_list>
#include <cstdio>
using std::isfinite;
struct Word { bool enableSoftLimitBwd = false, enableSoftLimitFwd = false; };
struct StatusWord { bool softlimbwdena = false, softlimfwdena = false; };
struct Data {
  struct { double softLimitBwd = 0, softLimitFwd = 0; Word controlWord_; } control_;
  struct { StatusWord statusWord_; } status_;
};
struct ecmcMonitor {
  Data data;
  Data *data_ = &data;
  bool softLimitReenablePending_ = false;
  int restoreSoftLimitPair(double, double);
  int disableSoftLimitsForZeroRange();
};
'''
code += constants + '\n' + method('int ecmcMonitor::restoreSoftLimitPair(')
code += method('int ecmcMonitor::disableSoftLimitsForZeroRange()')
code += r'''
int main() {
  for (double low : {-20., 0., 10.}) {
    ecmcMonitor mon;
    assert(mon.restoreSoftLimitPair(low, low + 10) == 0);
    assert(mon.data.control_.softLimitBwd == low && mon.data.control_.softLimitFwd == low + 10);
    assert(mon.data.control_.controlWord_.enableSoftLimitBwd && mon.data.control_.controlWord_.enableSoftLimitFwd);
    assert(mon.data.status_.statusWord_.softlimbwdena && mon.data.status_.statusWord_.softlimfwdena);
    for (bool bwd : {false, true}) for (bool fwd : {false, true}) {
      mon.data.control_.controlWord_ = {bwd, fwd};
      mon.data.status_.statusWord_ = {bwd, fwd};
      assert(mon.restoreSoftLimitPair(30, 40) == 0);
      assert(mon.data.control_.controlWord_.enableSoftLimitBwd == bwd);
      assert(mon.data.control_.controlWord_.enableSoftLimitFwd == fwd);
      assert(mon.data.status_.statusWord_.softlimbwdena == bwd);
      assert(mon.data.status_.statusWord_.softlimfwdena == fwd);
      for (double invalid : {40., 50., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        assert(mon.restoreSoftLimitPair(invalid, 40) == ERROR_MON_TOL_OUT_OF_RANGE);
        assert(mon.data.control_.softLimitBwd == 30 && mon.data.control_.softLimitFwd == 40);
        assert(mon.data.control_.controlWord_.enableSoftLimitBwd == bwd);
        assert(mon.data.control_.controlWord_.enableSoftLimitFwd == fwd);
      }
    }
    mon.softLimitReenablePending_ = true;
    assert(mon.restoreSoftLimitPair(0, 0) == 0);
    assert(!mon.data.control_.controlWord_.enableSoftLimitBwd && !mon.data.control_.controlWord_.enableSoftLimitFwd);
    assert(!mon.softLimitReenablePending_);
  }
  puts("Soft-limit pair: positive/negative ranges, enable policy, zero sentinel and invalid-input preservation passed.");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
