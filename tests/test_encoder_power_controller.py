#!/usr/bin/env python3
"""Exercise the production real-axis control branch with unavailable encoder feedback.

Drive/PID boundaries record calls; the power/ready/wake sequence itself is
covered by the production axis/encoder stepping suite.
"""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'devEcmcSup/motion/ecmcAxisReal.cpp').read_text()
start = source.index('  if (axisEnabled && masterOK) {')
brace = source.index('{', start)
depth, end = 1, brace+1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
branch = source[start:end]
code = r'''
#include <cassert>
enum { ECMC_DRV_MODE_CSV=0, ECMC_DRV_MODE_CSP=1 };
struct PID {
  int calls=0, resets=0;
  double control(double,double) { ++calls; return 2; }
  void reset() { ++resets; }
};
struct Drive {
  double velocity=0, position=0;
  void setVelSet(double v) { velocity=v; }
  void setCspPosSet(double v) { position=v; }
};
struct Monitor {
  bool deadband=false;
  void setEnable(bool) {}
  bool getCtrlInDeadband() { return deadband; }
};
struct Axis {
  struct {
    struct { int drvMode=ECMC_DRV_MODE_CSV, cspDrvEncIndex=1; } control_;
    struct {
      struct { bool busy=false; } statusWord_;
      double cntrlError=1, currentVelocitySetpoint=0;
      double currentPositionSetpoint=10, currentCSPPositionSetpointOffset=0;
    } status_;
  } data_;
  PID pid, *cntrl_=&pid; Drive drive, *drv_=&drive; Monitor mon, *mon_=&mon;
  bool ready=true, encoderPowerEnablePending_=false;
  bool encoderPowerReady() { return ready; }
  void step() { const bool axisEnabled=true, masterOK=true;
'''+branch+r'''
  }
};
int main() {
  Axis a;
  a.step(); assert(a.drive.velocity==2 && a.pid.calls==1);
  a.ready=false; a.step();
  assert(a.drive.velocity==0 && a.pid.calls==1 && a.pid.resets==1);
  a.ready=true; a.encoderPowerEnablePending_=true; a.step();
  assert(a.drive.velocity==0 && a.pid.calls==1 && a.pid.resets==2);
  a.encoderPowerEnablePending_=false; a.step();
  assert(a.drive.velocity==2 && a.pid.calls==2);
  // CSP holding must preserve the compensated command, not drop its offset.
  Axis csp; csp.data_.control_.drvMode=ECMC_DRV_MODE_CSP;
  csp.step(); assert(csp.drive.position==12 && csp.pid.calls==1);
  csp.ready=false; csp.step();
  assert(csp.drive.position==12 && csp.pid.calls==1 && csp.pid.resets==1);
  csp.ready=true; csp.encoderPowerEnablePending_=true;
  csp.data_.status_.currentPositionSetpoint=10.5; csp.step();
  assert(csp.drive.position==12 && csp.pid.calls==1);
  csp.encoderPowerEnablePending_=false; csp.step();
  assert(csp.drive.position==12.5 && csp.pid.calls==2);
}
'''
with tempfile.TemporaryDirectory(prefix='ecmc-encoder-control-') as tmp:
    src, exe = Path(tmp)/'check.cpp', Path(tmp)/'check'
    src.write_text(code)
    subprocess.run([os.environ.get('CXX','c++'), '-std=c++11', '-Wall', '-Wextra',
                    str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Encoder power: CSV output stops, CSP command holds its offset, PID resumes only after wake completion.')
