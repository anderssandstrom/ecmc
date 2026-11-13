#!/usr/bin/env python3
"""Compile the production text publisher with a recording asyn sink.
No motor driver, IOC, selected-axis state or real-time thread is required.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = (root/'devEcmcSup/motion/ecmcRtLoggerPortDriver.cpp').read_text()
state = src[src.index('struct AxisTextStatus {'):src.index('const char *ECMC_RT_LOGGER_PORT_NAME')]
publisher = src[src.index('  int axisIlockShortTextParam_'):src.index('  void publishAxisSoftLimits()')]
setters = src[src.index('void ecmcRtLoggerPortDriverSetAxisStatusText('):]
program = r'''
#include <atomic>
#include <string>
#include <map>
#include <cassert>
#include <cstring>
#include <cstdio>
#include "ecmcAxisDiagnosticText.h"
struct ecmcError {
 static const char *convertErrorIdToString(int) { return "native error"; }
 static const char *convertWarningIdToString(int) { return "native warning"; }
};
''' + state + setters + '\nstruct Publisher {\n' + publisher + r'''
 std::map<std::pair<int,int>,std::string> texts;
 void setStringParam(int axis, int param, const char *text) { texts[{axis,param}]=text; }
 void callParamCallbacks(int) {}
 Publisher() { axisIlockShortTextParam_=1; axisMessageTextParam_=2; axisIlockTextParam_=3; }
 std::string interlock(int axis) { return texts[{axis,1}]; }
 std::string longInterlock(int axis) { return texts[{axis,3}]; }
 std::string message(int axis) { return texts[{axis,2}]; }
};
int main() {
 Publisher p;
 p.publishAxisText();
 assert(p.interlock(1).empty() && p.longInterlock(1).empty());
 ecmcRtLoggerPortDriverSetAxisStatusText(1,0,0,ECMC_INTERLOCK_NONE,ECMC_CMD_MOVEABS);
 ecmcRtLoggerPortDriverSetAxisStatusText(2,0,0,ECMC_INTERLOCK_HARD_FWD,ECMC_CMD_MOVEVEL);
 p.publishAxisText();
 assert(p.interlock(1)=="IgnAbs");
 assert(p.longInterlock(1)=="Ignored ABS: positioning disabled");
 assert(p.longInterlock(2)=="Hard limit forward");
 const int commands[] = {ECMC_CMD_MOVEREL, ECMC_CMD_MOVEPVTABS, ECMC_CMD_HOMING, ECMC_CMD_MOVEVEL, ECMC_CMD_JOG};
 const char *labels[] = {"IgnRel", "IgnPVT", "IgnHome", "IgnVel", "IgnVel"};
 const char *details[] = {"Ignored REL: positioning disabled", "Ignored PVT: positioning disabled",
                         "Ignored HOME: homing disabled", "Ignored VEL: velocity disabled", "Ignored VEL: velocity disabled"};
 for (int i=0; i<5; ++i) {
  ecmcRtLoggerPortDriverSetAxisStatusText(1,0,0,0,commands[i]);
  p.publishAxisText();
  assert(p.interlock(1)==labels[i]);
  assert(p.longInterlock(1)==details[i]);
  assert(p.longInterlock(1).size()<40);
 }
 assert(p.interlock(2)=="HrdFwd");
 assert(p.message(1).empty());
 ecmcRtLoggerPortDriverSetAxisMotorMessage(1,"motor command error");
 p.publishAxisText(); assert(p.message(1)=="motor command error");
 ecmcRtLoggerPortDriverSetAxisStatusText(1,123,456,0,ECMC_CMD_NOCMD);
 p.publishAxisText(); assert(p.message(1)=="E: native error (0x7b)");
 assert(p.interlock(1).empty());
 assert(p.longInterlock(1).empty());
 ecmcRtLoggerPortDriverSetAxisStatusText(1,0,456,0,ECMC_CMD_NOCMD);
 p.publishAxisText(); assert(p.message(1)=="W: native warning (0x1c8)");
 ecmcRtLoggerPortDriverSetAxisMotorMessage(1,"");
 ecmcRtLoggerPortDriverSetAxisStatusText(1,0,0,0,ECMC_CMD_NOCMD);
 p.publishAxisText(); assert(p.message(1).empty());
 assert(p.interlock(2)=="HrdFwd");
 ecmcRtLoggerPortDriverSetAxisStatusText(-1,1,1,1,1);
 ecmcRtLoggerPortDriverSetAxisStatusText(ECMC_MAX_AXES,1,1,1,1);
}
'''
with tempfile.TemporaryDirectory() as directory:
    source=Path(directory)/'test.cpp'; source.write_text(program)
    binary=Path(directory)/'test'
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-I'+str(root/'devEcmcSup/main'),
                    '-I'+str(root/'devEcmcSup/motion'),str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('Axis diagnostic text: axis isolation, interlock priority, errors, warnings and motor fallback passed')
