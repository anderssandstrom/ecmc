#!/usr/bin/env python3
"""Compile native target dispatch and auto-enable with boundary stand-ins; no IOC.

Uses production target callbacks, move wrappers, auto-enable entry/SM and packed
status declarations. Does not model trajectory integration or motor processing.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'devEcmcSup/motion/ecmcAxisBase.cpp').read_text()
motion = (ROOT / 'devEcmcSup/motion/ecmcMotion.cpp').read_text()
header = (ROOT / 'devEcmcSup/motion/ecmcAxisData.h').read_text()


def method(text, signature):
    start = text.index(signature)
    return text[start:text.index('\n}', start) + 2] + '\n'


word = header[header.index('typedef struct {\n  unsigned char enable '):
              header.index('typedef struct {\n  double  externalTrajectoryPosition;')]
methods = ''.join(method(source, name) for name in (
    'void ecmcAxisBase::setAutoExecuteOnTargetPos(',
    'bool ecmcAxisBase::getAutoExecuteOnTargetPos(',
    'int ecmcAxisBase::moveOnTargetPosWrite(',
    'asynStatus ecmcAxisBase::axisAsynWriteTargetPos(',
    'int ecmcAxisBase::moveAbsolutePosition(\n',
    'int ecmcAxisBase::moveRelativePosition(\n',
    'int ecmcAxisBase::setExecute(bool execute) {',
    'void ecmcAxisBase::autoEnableSM(',
))
methods += ''.join(method(motion, name) for name in (
    'int setAxisTargetPos(', 'int setAxisAutoExecuteOnTargetPos(',
    'int getAxisAutoExecuteOnTargetPos(',
))
stub = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <limits>
#include "ecmcErrorsList.h"
#define ecmcRtLoggerLogInfo(...) ((void)0)
#define ecmcRtLoggerLogError(...) ((void)0)
#define ecmcRtLoggerLogDebug(...) ((void)0)
#define LOGINFO4(...) ((void)0)
enum motionCommandTypes { ECMC_CMD_MOVEREL=2, ECMC_CMD_MOVEABS=3, ECMC_CMD_HOMING=10 };
enum { ECMC_DATA_SOURCE_INTERNAL=0, ECMC_SEQ_HOME_SET_POS=15, ECMC_ASYN_AX_STATUS_ID=0 };
using asynStatus = int;
enum asynParamType { asynParamInt32, asynParamFloat64 };
enum { asynSuccess=0, asynError=1 };
struct Param { int refreshes=0; void refreshParamRT(int) { ++refreshes; } };
class ecmcAxisBase {
 public:
  struct Control {
    double positionTarget=0, velocityTarget=.4, accelerationTarget=2, decelerationTarget=3;
    motionCommandTypes command=ECMC_CMD_MOVEABS;
    int cmdData=0;
    struct { bool enableDbgPrintout=false; } controlWord_;
  };
  struct Status { ecmcAxisStatusWordType statusWord_{}; int axisId=1; double sampleTime=.01; };
  Param param;
  struct Data { Control control_; Status status_, statusOld_; Param *axAsynParams_[1]; } data_;
  struct Seq { bool global=false; void setGlobalBusy(bool b) { global=b; } } seq;
  bool autoExecuteOnTargetPos_=false, enableAutoEnable_=true, autoEnableRequest_=false;
  double autoEnableTimoutS_=1, autoEnableTimeCounter_=0;
  bool motionStartPending_=false; bool *motionStartPermissive_=nullptr;
  bool blocked=false, blockCom=false, startup=false, busy=false, execute=false;
  int error=0, source=0, starts=0, requests=0, targetWrites=0, staged=0;
  double applied=0;
  motionCommandTypes active=ECMC_CMD_MOVEABS;
  ecmcAxisBase() { data_.axAsynParams_[0]=&param; data_.status_.statusWord_.enable=1; }
  bool getBlockCom() { return blockCom; }
  bool getBlocked() { return blocked; }
  bool getStartupCommandsHeld() { return startup; }
  int setExternalCommandBlockedError() { return 101; }
  int setMasterSlaveCommandBlockedError() { return 102; }
  int getErrorID() { return error; }
  bool getError() { return error!=0; }
  int setErrorID(int e) { error=e; return e; }
  int getTrajDataSourceType() { return source; }
  bool getBusy() { return busy || seq.global; }
  bool getExecute() { return execute; }
  motionCommandTypes getCommand() { return active; }
  int setCommand(motionCommandTypes c) { active=c; data_.control_.command=c; return 0; }
  int setCmdData(int v) { data_.control_.cmdData=v; return 0; }
  bool getEnable() { return data_.status_.statusWord_.enable; }
  int setEnable(bool v) { data_.status_.statusWord_.enable=v; return 0; }
  Seq *getSeq() { return &seq; }
  void bumpMotionCommandRequestCounter() { ++requests; }
  void setTargetVel(double v) { data_.control_.velocityTarget=v; }
  void setAcc(double v) { data_.control_.accelerationTarget=v; }
  void setDec(double v) { data_.control_.decelerationTarget=v; }
  void setTargetPos(double v) { applied=v; ++targetWrites; }
  void setTargetPosAndCmd(double v) { data_.control_.positionTarget=v; ++staged; }
  void setWarningID(int) {}
  int setExecute(bool);
  int setExecute(bool v, bool) {
    if (v) { assert(getEnable()); ++starts; }
    execute=v; busy=v; data_.status_.statusWord_.localBusy=v;
    return 0;
  }
  void autoEnableSM();
  void setAutoExecuteOnTargetPos(bool);
  bool getAutoExecuteOnTargetPos() const;
  int moveOnTargetPosWrite(double);
  int moveAbsolutePosition(double,double,double,double);
  int moveRelativePosition(double,double,double,double);
  asynStatus axisAsynWriteTargetPos(void*,size_t,asynParamType);
};
ecmcAxisBase *axes[2]{};
#define CHECK_AXIS_RETURN_IF_ERROR(i) if ((i)!=1 || !axes[i]) return ERROR_MAIN_AXIS_INDEX_OUT_OF_RANGE;
#define CHECK_AXIS_RETURN_IF_ERROR_AND_BLOCK_COM(i) CHECK_AXIS_RETURN_IF_ERROR(i) if(axes[i]->getBlockCom()) return 101;
#define CHECK_AXIS_SEQ_RETURN_IF_ERROR(i) ((void)0);
'''
checks = r'''
int main() {
  // Verify actual word declarations, all old flag positions, and free bits.
  ecmcAxisStatusWordType w{}; uint32_t raw=0;
  w.enable=1; w.blocked=1; w.autoExecuteOnTargetPos=1;
  memcpy(&raw,&w,4); assert(raw==((1u<<24)|(1u<<23)|1u));
  ecmcAxisStatusWord2Type w2{};
  int state=1003; w2.seqstate=state; w2.lastilock=31;
  memcpy(&raw,&w2,4); assert(raw==((31u<<8)|(1003u&255u)));
  ecmcAxisBase a; axes[1]=&a;
  assert(!a.getAutoExecuteOnTargetPos());
  double pos=.2;
  assert(a.axisAsynWriteTargetPos(&pos,8,asynParamFloat64)==0);
  assert(a.starts==0 && a.data_.control_.positionTarget==.2);
  assert(setAxisTargetPos(1,.3)==0 && a.staged==1 && a.starts==0);
  assert(setAxisAutoExecuteOnTargetPos(1,1)==0 && a.starts==0);
  assert(a.data_.status_.statusWord_.autoExecuteOnTargetPos==1 && a.param.refreshes==1);
  int mode=0; assert(getAxisAutoExecuteOnTargetPos(1,&mode)==0 && mode==1);
  assert(a.axisAsynWriteTargetPos(&pos,4,asynParamInt32)==asynError);
  assert(a.starts==0);
  assert(a.axisAsynWriteTargetPos(&pos,8,asynParamFloat64)==0 && a.starts==1);
  assert(a.applied==.2 && a.data_.control_.accelerationTarget==2 && a.data_.control_.decelerationTarget==3);
  assert(setAxisTargetPos(1,.4)==0 && a.starts==1 && a.applied==.4); // retarget
  a.setExecute(false,false);
  a.data_.control_.command=ECMC_CMD_MOVEREL;
  assert(setAxisTargetPos(1,-.1)==0 && a.active==ECMC_CMD_MOVEREL && a.starts==2);
  a.setExecute(false,false);
  assert(setAxisTargetPos(1,-.1)==0 && a.starts==3); // identical write retriggers
  a.setExecute(false,false);
  int writes=a.targetWrites;
  a.blocked=true; assert(setAxisTargetPos(1,.6)==102); a.blocked=false;
  a.blockCom=true; assert(setAxisTargetPos(1,.6)==101); a.blockCom=false;
  a.startup=true; assert(setAxisTargetPos(1,.6)==ERROR_AXIS_BUSY); a.startup=false;
  a.error=123; assert(setAxisTargetPos(1,.6)==123); a.error=0;
  a.source=1; assert(setAxisTargetPos(1,.6)==ERROR_MAIN_TRAJ_SOURCE_NOT_INTERNAL); a.source=0;
  a.data_.control_.command=ECMC_CMD_HOMING;
  assert(setAxisTargetPos(1,.6)==ERROR_SEQ_COMMAND_NOT_SUPPORTED);
  a.data_.control_.command=ECMC_CMD_MOVEABS;
  a.active=ECMC_CMD_HOMING; a.busy=true;
  assert(setAxisTargetPos(1,.6)==ERROR_AXIS_BUSY); a.busy=false;
  assert(setAxisTargetPos(1,std::numeric_limits<double>::infinity())==ERROR_SEQ_TARGET_POS_OUT_OF_RANGE);
  assert(a.targetWrites==writes && a.starts==3 && !a.autoEnableRequest_);
  // Normal auto-enable: request is staged, no motion before stable feedback.
  a.setEnable(false);
  assert(setAxisTargetPos(1,.5)==0 && a.autoEnableRequest_ && a.seq.global);
  assert(a.starts==3);
  a.autoEnableSM(); assert(a.getEnable() && a.starts==3);
  a.data_.status_.statusWord_.enabled=1;
  a.autoEnableSM(); assert(a.starts==3);
  a.data_.statusOld_.statusWord_.enabled=1;
  a.autoEnableSM(); assert(a.starts==4 && !a.autoEnableRequest_ && !a.seq.global);
  a.setExecute(false,false); a.setEnable(false); a.enableAutoEnable_=false;
  assert(setAxisTargetPos(1,.6)==ERROR_AXIS_NOT_ENABLED && !a.autoEnableRequest_ && !a.seq.global);
  assert(setAxisAutoExecuteOnTargetPos(1,0)==0);
  assert(!a.data_.status_.statusWord_.autoExecuteOnTargetPos);
  assert(setAxisTargetPos(1,.6)==0 && a.starts==4 && a.staged==2);
  assert(setAxisAutoExecuteOnTargetPos(0,1)==ERROR_MAIN_AXIS_INDEX_OUT_OF_RANGE);
  // Missing feedback must use the existing auto-enable timeout path.
  ecmcAxisBase timeout; axes[1]=&timeout; timeout.setEnable(false);
  timeout.setAutoExecuteOnTargetPos(true);
  assert(setAxisTargetPos(1,.3)==0 && timeout.autoEnableRequest_);
  timeout.autoEnableTimeCounter_=2;
  timeout.autoEnableSM();
  assert(timeout.error==ERROR_AXIS_AUTO_ENABLE_TIMEOUT && timeout.starts==0);
  assert(!timeout.getEnable() && !timeout.autoEnableRequest_ && !timeout.seq.global);
  assert(parseSet("SetAxisAutoExecuteOnTargetPos(1,0)")==0);
  assert(parseGet("GetAxisAutoExecuteOnTargetPos(1)")==0);
  assert(parseSet("SetAxisAutoExecuteOnTargetPos(1,1)")==0);
  assert(parseGet("GetAxisAutoExecuteOnTargetPos(1)")==1);
  assert(parseSet("SetAxisAutoExec(1,0)")==0);
  assert(parseGet("GetAxisAutoExec(1)")==0);
  assert(parseSet("SetAxisAutoExec(1,1)")==0);
  assert(parseGet("GetAxisAutoExec(1)")==1);
}
'''
parser = (ROOT/'devEcmcSup/com/ecmcCmdParser.c').read_text()


def parser_branch(prefix):
    start = parser.index('  nvals = sscanf(myarg_1, "'+prefix)
    return parser[start:parser.index('\n  }', start)+4]


parsers = r'''
#define SEND_RESULT_OR_ERROR_AND_RETURN_INT(call) do { int err=(call); return err ? err : iValue; } while(0)
int parseSet(const char *myarg_1) {
  int nvals=0, iValue=0, iValue2=0;
''' + parser_branch('SetAxisAutoExec(') + parser_branch('SetAxisAutoExecuteOnTargetPos') + r'''
  return -1;
}
int parseGet(const char *myarg_1) {
  int nvals=0, motor_axis_no=0, iValue=0;
''' + parser_branch('GetAxisAutoExec(') + parser_branch('GetAxisAutoExecuteOnTargetPos') + r'''
  return -1;
}
'''
# The real initialization, rather than enabling the feature by default.
assert 'autoExecuteOnTargetPos_       = false;' in source
assert len('Cfg.SetAxisAutoExec(1023,1)') < 40
assert len('GetAxisAutoExec(1023)') < 40
bit_checks = ''
for bit, field in enumerate(('enable','enabled','execute','busy','attarget','moving',
                            'limitfwd','limitbwd','homeswitch','homed','inrealtime',
                            'trajsource','encsource','plccmdallowed','softlimfwdena',
                            'softlimbwdena','instartup','sumilockfwd','sumilockbwd',
                            'softlimilockfwd','softlimilockbwd','localBusy','globalBusy',
                            'blocked','autoExecuteOnTargetPos')):
    bit_checks += f'w={{}}; w.{field}=1; memcpy(&raw,&w,4); assert(raw==(1u<<{bit}));\n'
checks = checks.replace('  ecmcAxisStatusWord2Type w2{};', bit_checks+'  ecmcAxisStatusWord2Type w2{};')
with tempfile.TemporaryDirectory(prefix='ecmc-target-write-') as temp:
    src, exe = Path(temp)/'check.cpp', Path(temp)/'check'
    src.write_text(stub.replace('class ecmcAxisBase {', word+'\nclass ecmcAxisBase {') + methods + parsers + checks)
    subprocess.run([os.environ.get('CXX','c++'), '-std=c++11', '-Wall', '-Wextra',
                    '-I'+str(ROOT/'devEcmcSup/main'), str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Native target writes: default staging, ABS/REL, retarget, rejection, normal auto-enable and packed status layout pass.')
