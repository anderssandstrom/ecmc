#!/usr/bin/env python3
"""Exercise startup diagnostic publication without EPICS/asyn dependencies."""
# Expectation basis: DOC: diagnostic publication contract; COMPAT: callback shape.
# Rationale, sources and open questions: EXPECTATIONS.md
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'devEcmcSup/motor/ecmcMotorRecordAxis.cpp').read_text()
start = source.index('void ecmcMotorRecordAxis::publishStartupDiagnostics()')
method = source[start:source.index('\n}', start) + 2]
poll = source[source.index('asynStatus ecmcMotorRecordAxis::poll(bool *moving)'):]
assert poll.index('publishStartupDiagnostics();') < poll.index('readEcmcAxisStatusData();')
code = r'''
#include <cassert>
#include <string>
bool held = true;
int ecmcStartupCommandsHeld() { return held; }
struct Native { bool ready = false; bool getMotorRecordStartupReady() { return ready; } };
struct Controller {
  int startupCommandsHeld_ = 0, startupMotorReady_ = 1, startupLimitsReady_ = 2, startupRestoreStatus_ = 3;
  int calls = 0, address = -1;
  void callParamCallbacks(int axis) { ++calls; address = axis; }
};
struct ecmcMotorRecordAxis {
  Controller controller;
  Controller *pC_ = &controller;
  Native native;
  struct Local { Native *ecmcAxis; } drvlocal {&native};
  int axisNo_ = 7, params[3] = {-1, -1, -1};
  bool updateFirstPollDone_ = false;
  std::string text, reason = "waiting for IOC running";
  void setIntegerParam(int param, int value) { assert(param < 3); params[param] = value; }
  void setStringParam(int param, const char *value) { assert(param == 3); text = value; }
  const char *getSoftLimitRestoreStatus() { return reason.c_str(); }
  void publishStartupDiagnostics();
};
'''
code += method + r'''
int main() {
  ecmcMotorRecordAxis axis;
  axis.publishStartupDiagnostics();
  assert(axis.params[0] == 1 && axis.params[1] == 0 && axis.params[2] == 0);
  assert(axis.text == axis.reason && axis.controller.address == 7);
  axis.reason = "invalid snapshot: correct DLLM/DHLM/MRES and restart IOC";
  axis.publishStartupDiagnostics();
  assert(axis.text == axis.reason); // Preserve text longer than a stringin.
  axis.updateFirstPollDone_ = true;
  axis.reason = "complete";
  axis.publishStartupDiagnostics();
  assert(axis.params[0] == 1 && axis.params[1] == 0 && axis.params[2] == 1);
  held = false;
  axis.native.ready = true;
  axis.publishStartupDiagnostics();
  assert(axis.params[0] == 0 && axis.params[1] == 1 && axis.params[2] == 1);
  assert(axis.controller.calls == 4);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Startup diagnostics: hold, independent readiness, full text and axis-addressed callbacks passed.')
