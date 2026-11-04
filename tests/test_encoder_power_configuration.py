#!/usr/bin/env python3
"""Production pair configuration/validation with controlled encoder/drive boundaries.

The real drive suite separately checks that constructor/configuration leaves
mode NONE until DriveBase::validate classifies the velocity link as CSV.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'devEcmcSup/motion/ecmcAxisBase.cpp').read_text()
def method(signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start)+2]
methods = '\n'.join(method(s) for s in (
    'int ecmcAxisBase::validateEncoderPowerBackup(',
    'int ecmcAxisBase::setEncoderPowerBackup(',
    'int ecmcAxisBase::validateBase()',
))
code = r'''
#include <cassert>
#include <cmath>
#include "ecmcErrorsList.h"
#define ecmcRtLoggerLogError(...) ((void)0)
enum { ECMC_MAX_ENCODERS=8, ECMC_ENCODER_TYPE_INCREMENTAL=0, ECMC_ENCODER_TYPE_ABSOLUTE=1,
       ECMC_DATA_SOURCE_INTERNAL=0, ECMC_DRV_MODE_CSV=0, ECMC_DRV_MODE_CSP=1, ECMC_DRV_MODE_NONE=10 };
struct Encoder {
 bool power=false; int type=0,state=1;
 bool getPowerEnable(){return power;} int getType(){return type;} int getPowerState(){return state;}
};
struct Monitor { bool getEnableAtTargetMon(){return true;} double getAtTargetTol(){return .01;} };
struct Drive {};
class ecmcAxisBase {
public:
 struct Data {
  struct { int encoderCount=1, axisId=6; struct { int encsource=0,encoderMode=0; } statusWord_; } status_;
  struct { double moduloRange=0; int drvMode=ECMC_DRV_MODE_NONE; struct { int encoderMode=0; } controlWord_; } control_;
 } data_;
 Encoder source,backup; Encoder *encArray_[8]={&source};
 Monitor mon; Monitor *mon_=&mon; Drive drive; bool real=true,realtime=false,busy=false;
 int encoderPowerSourceIndex_=-1,encoderPowerBackupIndex_=-1,encoderMode_=0,error=0;
 double encoderPowerDiffTol_=.01; bool encoderPowerBackupAligned_=false;
 ecmcAxisBase(){source.type=ECMC_ENCODER_TYPE_ABSOLUTE;source.power=true;}
 Drive *getDrv(){return real?&drive:nullptr;}
 bool getEnable(){return false;} bool getEnabled(){return false;} bool getBusy(){return busy;}
 bool getRealTimeStarted(){return realtime;} int getEncoderForceOn(){return 0;}
 int setErrorID(int value){error=value;return value;}
 int validateEncoderPowerBackup(int,int); int setEncoderPowerBackup(int,int,double); int validateBase();
};
'''+methods+r'''
int main(){
 ecmcAxisBase a;
 // Source configured before its backup; cached real-drive mode is not ready yet.
 assert(a.setEncoderPowerBackup(1,2,.02)==0);
 assert(a.encoderPowerBackupIndex_==1);
 assert(a.validateBase()==ERROR_ENC_POWER_CONFIG); // missing backup, safely rejected
 a.encArray_[1]=&a.backup;a.data_.status_.encoderCount=2;
 assert(a.validateBase()==ERROR_ENC_POWER_CONFIG); // drive not validated yet
 a.data_.control_.drvMode=ECMC_DRV_MODE_CSV;
 assert(a.validateBase()==0);
 a.data_.control_.drvMode=ECMC_DRV_MODE_CSP;
 assert(a.validateBase()==ERROR_ENC_POWER_CONFIG); // still rejects CSP
 a.data_.control_.drvMode=ECMC_DRV_MODE_CSV;
 a.backup.type=ECMC_ENCODER_TYPE_ABSOLUTE;
 assert(a.validateBase()==ERROR_ENC_POWER_CONFIG);
 a.backup.type=ECMC_ENCODER_TYPE_INCREMENTAL;a.backup.power=true;
 assert(a.validateBase()==ERROR_ENC_POWER_CONFIG);
 a.backup.power=false;a.data_.control_.moduloRange=360;
 assert(a.validateBase()==ERROR_ENC_POWER_CONFIG);
 a.data_.control_.moduloRange=0;a.data_.status_.statusWord_.encsource=1;
 assert(a.validateBase()==ERROR_ENC_POWER_CONFIG);
 a.data_.status_.statusWord_.encsource=0;assert(a.validateBase()==0);
 // Runtime updates require immediate validity and must not replace the old pair on failure.
 a.realtime=true;
 assert(a.setEncoderPowerBackup(1,3,.03)==ERROR_ENC_POWER_CONFIG);
 assert(a.encoderPowerBackupIndex_==1 && a.encoderPowerDiffTol_==.02);
 a.source.state=3;assert(a.setEncoderPowerBackup(1,2,.03)==ERROR_ENC_POWER_CONFIG);
 a.source.state=1;a.busy=true;
 assert(a.setEncoderPowerBackup(1,2,.03)==ERROR_AXIS_BUSY);
 a.busy=false;assert(a.setEncoderPowerBackup(1,2,.03)==0);
 ecmcAxisBase limits;
 assert(limits.setEncoderPowerBackup(1,1,.01)==ERROR_ENC_POWER_CONFIG);
 assert(limits.setEncoderPowerBackup(1,9,.01)==ERROR_ENC_POWER_CONFIG);
 assert(limits.setEncoderPowerBackup(1,2,0)==ERROR_ENC_POWER_CONFIG);
 assert(limits.setEncoderPowerBackup(1,2,NAN)==ERROR_ENC_POWER_CONFIG);
}
'''
with tempfile.TemporaryDirectory(prefix='ecmc-encoder-pair-config-') as tmp:
    src,exe=Path(tmp)/'check.cpp',Path(tmp)/'check'
    src.write_text(code)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra',
                    '-I'+str(root/'devEcmcSup/main'),str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('Encoder pair: order-independent startup, final CSV/type/range validation and strict runtime changes pass.')
