#!/usr/bin/env python3
"""REGRESSION: publish motor problem status with its current error classification.

Compile the production callback method without an IOC. Every emitted callback
must agree with the current MCU/local error and soft-limit interlock, including
fault entry, reset and unchanged polls. Removing the fix must fail recovery.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'devEcmcSup/motor/ecmcMotorRecordAxis.cpp').read_text()
start = source.index('void ecmcMotorRecordAxis::callParamCallbacksUpdateError()')
method = source[start:source.index('\nasynStatus ecmcMotorRecordAxis::readEcmcAxisStatusData()', start)]
fix = '''  setIntegerParam(pC_->motorStatusProblem_,
                  (drvlocal.eeAxisError != eeAxisErrorNoError) ||
                  drvlocal.ecmcSoftLimitInterlock);
'''
assert fix in method
stubs = r'''
#include <cassert>
#include <cstring>
#include <string>
constexpr int eeAxisErrorNoError=0, eeAxisErrorMCUError=1,
  eeAxisErrorIOCcomError=2, eeAxisErrorCmdError=3, eeAxisIllegalInTargetWindow=4;
constexpr int eeAxisWarningNoWarning=0, eeAxisWarningVeloZero=1, eeAxisWarningSpeedLimit=2;
constexpr int ECMC_CMD_MOVEVEL=1, ECMC_CMD_MOVEREL=2, ECMC_CMD_MOVEABS=3, ECMC_CMD_HOMING=4;
constexpr int ASYN_TRACE_FLOW=0;
void asynPrint(void *, int, const char *, ...) {}
class ecmcMotorRecordAxis {
 public:
  struct Controller { int ecmcMotorRecordErr_=0, ecmcMotorRecordErrId_=1, motorStatusProblem_=2; } controller, *pC_=&controller;
  struct Local {
    int nErrorIdMcu=0, eeAxisError=0, old_eeAxisError=0, eeAxisWarning=0, old_eeAxisWarning=0;
    int nErrorIdEpicsOld=0, nCommandActive=0, nCommandActiveOld=0;
    struct { bool sErrorMessage=false; } dirty;
    char sErrorMessage[256]="MCU error", cmdErrorMessage[256]="";
    bool illegalInTargetWindow=false, ecmcSoftLimitInterlock=false;
  } drvlocal;
  void *pPrintOutAsynUser=nullptr;
  const char *modNamEMC="test";
  int axisNo_=6, callbacks=0, params[3]={0,0,0};
  std::string message;
  void setIntegerParam(int key, int value) { params[key]=value; }
  void updateEcmcErrorMsg(const char *value) { message=value ? value : ""; }
  void callParamCallbacks() {
    ++callbacks;
    const bool error=drvlocal.nErrorIdMcu || drvlocal.dirty.sErrorMessage ||
      drvlocal.cmdErrorMessage[0] || drvlocal.illegalInTargetWindow;
    assert(params[2] == (error || drvlocal.ecmcSoftLimitInterlock));
    assert(params[0] == (drvlocal.nErrorIdMcu != 0));
    assert(params[1] == drvlocal.nErrorIdMcu);
  }
  void callParamCallbacksUpdateError();
};
'''
checks = r'''
int main() {
  ecmcMotorRecordAxis axis;
  axis.callParamCallbacksUpdateError();
  axis.drvlocal.nErrorIdMcu=0x14417;
  axis.callParamCallbacksUpdateError();
  assert(axis.params[2] == 1);
  axis.drvlocal.nErrorIdMcu=0;
  axis.callParamCallbacksUpdateError();
  assert(axis.params[2] == 0 && axis.message.empty());
  axis.callParamCallbacksUpdateError();
  axis.drvlocal.ecmcSoftLimitInterlock=true;
  axis.callParamCallbacksUpdateError();
  axis.drvlocal.ecmcSoftLimitInterlock=false;
  axis.callParamCallbacksUpdateError();
  for(int kind=0; kind<3; ++kind) {
    axis.drvlocal.dirty.sErrorMessage=(kind==0);
    axis.drvlocal.cmdErrorMessage[0]=(kind==1) ? 'E' : 0;
    axis.drvlocal.illegalInTargetWindow=(kind==2);
    axis.callParamCallbacksUpdateError();
    axis.drvlocal.dirty.sErrorMessage=false;
    axis.drvlocal.cmdErrorMessage[0]=0;
    axis.drvlocal.illegalInTargetWindow=false;
    axis.callParamCallbacksUpdateError();
  }
  assert(axis.callbacks == 12);
}
'''
with tempfile.TemporaryDirectory(prefix='ecmc-error-publication-') as directory:
    path = Path(directory)
    for label, body in (('production', method), ('old_order', method.replace(fix, ''))):
        cpp, binary = path / (label + '.cpp'), path / label
        cpp.write_text(stubs + body + checks)
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11', str(cpp), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True)
        if label == 'production':
            assert result.returncode == 0, result.stderr.decode()
        else:
            assert result.returncode != 0, 'old callback ordering must fail'
print('Motor error publication: fault/recovery/local errors/interlocks coherent at every callback; old ordering fails.')
