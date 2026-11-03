#!/usr/bin/env python3
"""Production relay command parsing and C API dispatch; encoder logic is tested separately."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = (root/'devEcmcSup/com/ecmcCmdParser.c').read_text()
motion = (root/'devEcmcSup/motion/ecmcMotion.cpp').read_text()
api = motion[motion.index('int setAxisEncPwrEnable('):motion.index('int setAxisEncInvHwReady(')]
sets = parser[parser.index('    nvals = sscanf(myarg_1, "SetAxisEncPwrEnable'):parser.index('\n    nvals = sscanf(myarg_1,\n                   "SetAxisEncInvHwReady')]
gets = parser[parser.index('    nvals = sscanf(myarg_1, "GetAxisEncPwrEnable'):parser.index('    nvals = sscanf(myarg_1, "GetAxisEncAbsBits')]
code = r'''
#include <cassert>
#include <cstdio>
#include "ecmcErrorsList.h"
struct Encoder {
  bool enabled=false, high=true;
  double delay=.1, timeout=5, off=2;
  int setPowerEnable(bool v) { enabled=v; return 0; }
  int setPowerPolarity(bool v) { high=v; return 0; }
  int setPowerDelay(double v) { delay=v; return 0; }
  int setPowerTimeout(double v) { timeout=v; return 0; }
  int setPowerOffDelay(double v) { off=v; return 0; }
  bool getPowerEnable() { return enabled; }
  bool getPowerPolarity() { return high; }
  double getPowerDelay() { return delay; }
  double getPowerTimeout() { return timeout; }
  double getPowerOffDelay() { return off; }
  int getPowerState() { return enabled ? 1 : 0; }
};
struct Monitor {
  bool enabled=true; double tol=.01;
  bool getEnableAtTargetMon() { return enabled; }
  double getAtTargetTol() { return tol; }
};
struct Axis {
  Encoder encoders[2]; int selected=0; bool blocked=false; Monitor monitor;
  Encoder *getConfigEnc() { return &encoders[selected]; }
  Monitor *getMon() { return &monitor; }
};
Axis axis, *axes[3]={nullptr,nullptr,&axis};
#define CHECK_AXIS_RETURN_IF_ERROR(i) if(i<0 || i>=3 || !axes[i]) return ERROR_MAIN_AXIS_INDEX_OUT_OF_RANGE;
#define CHECK_AXIS_RETURN_IF_ERROR_AND_BLOCK_COM(i) CHECK_AXIS_RETURN_IF_ERROR(i) if(axes[i]->blocked) return ERROR_AXIS_BLOCKED;
'''+api+r'''
int parseSet(const char *myarg_1) { int nvals=0,iValue=0,iValue2=0; double dValue=0;
'''+sets+r'''
return -1;
}
#define SEND_RESULT_OR_ERROR_AND_RETURN_INT(call) do { int e=(call); return e ? -e : iValue; } while(0)
#define SEND_RESULT_OR_ERROR_AND_RETURN_DOUBLE(call) do { int e=(call); return e ? -e : fValue; } while(0)
double parseGet(const char *myarg_1) { int nvals=0,motor_axis_no=0,iValue=0; double fValue=0;
'''+gets+r'''
return -1;
}
int main() {
  axis.selected=1;
  assert(parseSet("SetAxisEncPwrPol(2,0)")==0);
  assert(parseSet("SetAxisEncPwrDelay(2,0.25)")==0);
  assert(parseSet("SetAxisEncPwrTimeout(2,3.5)")==0);
  assert(parseSet("SetAxisEncPwrOffDelay(2,1.5)")==0);
  assert(parseSet("SetAxisEncPwrEnable(2,1)")==0);
  assert(parseGet("GetAxisEncPwrEnable(2)")==1);
  assert(parseGet("GetAxisEncPwrPol(2)")==0);
  assert(parseGet("GetAxisEncPwrDelay(2)")==.25);
  assert(parseGet("GetAxisEncPwrTimeout(2)")==3.5);
  assert(parseGet("GetAxisEncPwrOffDelay(2)")==1.5);
  assert(parseGet("GetAxisEncPwrState(2)")==1);
  axis.selected=0;
  assert(parseGet("GetAxisEncPwrEnable(2)")==0);
  assert(parseGet("GetAxisEncPwrDelay(2)")==.1);
  axis.monitor.enabled=false;
  assert(parseSet("SetAxisEncPwrEnable(2,1)")==ERROR_ENC_POWER_CONFIG);
  axis.monitor.enabled=true; axis.monitor.tol=0;
  assert(parseSet("SetAxisEncPwrEnable(2,1)")==ERROR_ENC_POWER_CONFIG);
  assert(parseSet("SetAxisEncPwrEnable(2,0)")==0);
  axis.blocked=true;
  assert(parseSet("SetAxisEncPwrOffDelay(2,9)")==ERROR_AXIS_BLOCKED);
  assert(parseSet("SetAxisEncPwrPol(9,1)")==ERROR_MAIN_AXIS_INDEX_OUT_OF_RANGE);
}
'''
for command in ('Cfg.SetAxisEncPwrOffDelay(1023,1.5)', 'Cfg.SetAxisEncPwrTimeout(1023,5)'):
    assert len(command) < 40
with tempfile.TemporaryDirectory(prefix='ecmc-encoder-power-api-') as tmp:
    src, exe = Path(tmp)/'check.cpp', Path(tmp)/'check'
    src.write_text(code)
    subprocess.run([os.environ.get('CXX','c++'), '-std=c++11', '-Wall', '-Wextra',
                    '-I'+str(root/'devEcmcSup/main'), str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Encoder relay commands: parser routes, selected encoder, atTarget prerequisite, block/index checks pass.')
