#include "ecmcMonitor.hpp"

#include <stdio.h>

ecmcMonitor::ecmcMonitor()
{
  initVars();
}

ecmcMonitor::ecmcMonitor(bool enableAtTargetMon, bool enableLagMon)
{
  initVars();
  enableAtTargetMon_=enableAtTargetMon;
  enableLagMon_=enableLagMon;
}

void ecmcMonitor::initVars()
{
  errorReset();
  enable_=false;
  atTargetTol_=0;
  atTargetTime_=0;
  enableAtTargetMon_=true;
  posLagTol_=0;
  posLagTime_=0;
  enableLagMon_=true;
  actPos_=0;
  motionInterlock_=false;   //Not implemented
  atTarget_=false;
  atTargetCounter_=0;
  lagErrorTraj_=false;
  lagErrorDrive_=false;
  lagMonCounter_=0;
  hardBwd_=false;
  hardFwd_=false;
  homeSwitch_=false;
  targetPos_=0;
  lagError_=0;
  bothLimitsLowInterlock_=false;
  currSetPos_=0;
  setVel_=0;
  actVel_=0;
  maxVel_=0;
  enableMaxVelMon_=true;
  velErrorTraj_=false;
  velErrorDrive_=false;
  maxVelCounterDrive_=0;
  maxVelCounterTraj_=0;
  maxVelDriveILDelay_=0;
  maxVelTrajILDelay_=200; //200 cycles
  maxVelDriveILDelay_=maxVelTrajILDelay_*2; //400 cycles default
  enableHardwareInterlock_=false;
  hardwareInterlock_=false;
  cntrlOutput_=0;
  cntrlOutputHL_=0;
  cntrlOutputHLErrorTraj_=false;
  cntrlOutputHLErrorDrive_=false;
  enableCntrlHLMon_=false;
  enableCntrlOutIncreaseAtLimitMon_=false;
  cntrlOutputOld_=0;
  cntrlOutIncreaseAtLimitErrorTraj_=false;
  cntrlOutIncreaseAtLimitErrorDrive_=false;
  cntrlOutIncreaseAtLimitCounter_=0;
  positionError_=0;
  positionErrorOld_=0;
  reasonableMoveCounter_=0;
  cycleCounter_=0;
  cntrlKff_=0;
  bwdLimitInterlock_=true;
  fwdLimitInterlock_=true;
  softLimitBwd_=true;
  softLimitFwd_=true;
  enableSoftLimitBwd_=0;
  enableSoftLimitFwd_=0;
  enableAlarmAtHardlimitBwd_=false;
  enableAlarmAtHardlimitFwd_=false;
  distToStop_=0;
  bwdSoftLimitInterlock_=false;
  fwdSoftLimitInterlock_=false;
  currSetPosOld_=0;
  extEncInterlock_=ECMC_INTERLOCK_NONE;
  extTrajInterlock_=ECMC_INTERLOCK_NONE;
}

ecmcMonitor::~ecmcMonitor()
{
  ;
}

void ecmcMonitor::setActPos(double pos)
{
  actPos_=pos;
}

void ecmcMonitor::execute()
{

  //Limits
  checkLimits();

  //At target
  checkAtTarget();

  //External interlock (on ethercat I/O)
  if(enableHardwareInterlock_ && !hardwareInterlock_ && enable_)
  {
    setErrorID(ERROR_MON_EXTERNAL_HARDWARE_INTERLOCK);
  }

  if(!enable_){
    return;
  }

  //Lag error
  checkPositionLag();

  //Max Vel
  checkMaxVelocity();

  //Controller output HL
  checkCntrlMaxOutput();

  //Controller output increase at limit switches monitoring
  checkCntrloutputIncreaseAtLimit();
}

void ecmcMonitor::setTargetPos(double pos)
{
  targetPos_=pos;
}

double ecmcMonitor::getTargetPos()
{
  return targetPos_;
}

void ecmcMonitor::setCurrentPosSet(double pos)
{
  currSetPosOld_=currSetPos_;
  currSetPos_=pos;
}

double ecmcMonitor::getCurrentPosSet()
{
  return currSetPos_;
}


bool ecmcMonitor::getAtTarget()
{
  return atTarget_;
}

bool ecmcMonitor::getHardLimitFwd()
{
  return hardFwd_;
}

bool ecmcMonitor::getHardLimitBwd()
{
  return hardBwd_;
}

interlockTypes ecmcMonitor::getTrajInterlock()
{
  if(enableHardwareInterlock_&& !hardwareInterlock_){
    return ECMC_INTERLOCK_EXTERNAL;
  }

  if(bwdLimitInterlock_){
    return ECMC_INTERLOCK_HARD_BWD;
  }

  if(fwdLimitInterlock_){
    return ECMC_INTERLOCK_HARD_FWD;
  }

  if(extTrajInterlock_==ECMC_INTERLOCK_TRANSFORM){
    return ECMC_INTERLOCK_TRANSFORM;
  }

  if(extEncInterlock_==ECMC_INTERLOCK_TRANSFORM){
    return ECMC_INTERLOCK_TRANSFORM;
  }

  if(fwdSoftLimitInterlock_){
    return ECMC_INTERLOCK_SOFT_FWD;
  }

  if(bwdSoftLimitInterlock_){
    return ECMC_INTERLOCK_SOFT_BWD;
  }

  if(cntrlOutputHLErrorTraj_){
    return ECMC_INTERLOCK_CONT_HIGH_LIMIT;
  }

  if(lagErrorTraj_){
    return ECMC_INTERLOCK_POSITION_LAG;
  }

  if(bothLimitsLowInterlock_){
    return ECMC_INTERLOCK_BOTH_LIMITS;
  }

  if(velErrorTraj_){
    return ECMC_INTERLOCK_MAX_SPEED;
  }

  if(cntrlOutIncreaseAtLimitErrorTraj_){
    return ECMC_INTERLOCK_CONT_OUT_INCREASE_AT_LIMIT_SWITCH;
  }

  return ECMC_INTERLOCK_NONE;
}

bool ecmcMonitor::getDriveInterlock()
{
  return cntrlOutIncreaseAtLimitErrorDrive_|| cntrlOutputHLErrorDrive_|| lagErrorDrive_ || bothLimitsLowInterlock_ || velErrorDrive_ || (!hardwareInterlock_ && enableHardwareInterlock_);
}

void ecmcMonitor::setAtTargetTol(double tol)
{
  atTargetTol_=tol;
}

double ecmcMonitor::getAtTargetTol()
{
  return atTargetTol_;
}

void ecmcMonitor::setAtTargetTime(int time)
{
  atTargetTime_=time;
}

int ecmcMonitor::getAtTargetTime()
{
  return atTargetTime_;
}

void ecmcMonitor::setEnableAtTargetMon(bool enable)
{
  enableAtTargetMon_=enable;
}

bool ecmcMonitor::getEnableAtTargetMon()
{
  return enableAtTargetMon_;
}

void ecmcMonitor::setPosLagTol(double tol)
{
  posLagTol_=tol;
}

double ecmcMonitor::getPosLagTol()
{
  return posLagTol_;
}

void ecmcMonitor::setPosLagTime(int time){
  posLagTime_=time;
}

int ecmcMonitor::getPosLagTime()
{
  return posLagTime_;
}

void ecmcMonitor::setEnableLagMon(bool enable)
{
  enableLagMon_=enable;
}

bool ecmcMonitor::getEnableLagMon()
{
  return enableLagMon_;
}

/*void ecmcMonitor::setHomeSwitch(bool switchState)
{
  homeSwitch_=switchState;
}*/

bool ecmcMonitor::getHomeSwitch()
{
  return homeSwitch_;
}

void ecmcMonitor::readEntries(){
  uint64_t tempRaw=0;

  //Hard limit BWD
  if (readEcEntryValue(0,&tempRaw)) {
    setErrorID(ERROR_MON_ENTRY_READ_FAIL);
    return;
  }
  hardBwd_=tempRaw>0;

  //Hard limit FWD
  if(readEcEntryValue(1,&tempRaw)){
    setErrorID(ERROR_MON_ENTRY_READ_FAIL);
    return;
  }
  hardFwd_=tempRaw>0;

  //Home
  if(readEcEntryValue(2,&tempRaw)){
    setErrorID(ERROR_MON_ENTRY_READ_FAIL);
    return;
  }
  homeSwitch_=tempRaw>0;

  if(enableHardwareInterlock_){
    if(readEcEntryValue(3,&tempRaw)){
      setErrorID(ERROR_MON_ENTRY_READ_FAIL);
      return;
    }
    hardwareInterlock_=tempRaw>0;
  }
}

void ecmcMonitor::setEnable(bool enable)
{
  enable_=enable;
}

bool ecmcMonitor::getEnable()
{
  return enable_;
}

int ecmcMonitor::validate()
{
  int error=validateEntryBit(0);
  if(error){ //Hard limit BWD
    return  setErrorID(ERROR_MON_ENTRY_HARD_BWD_NULL);
  }

  error=validateEntryBit(1);
  if(error){ //Hard limit FWD
    return  setErrorID(ERROR_MON_ENTRY_HARD_FWD_NULL);
  }

  error=validateEntryBit(2);
  if(error){ //Home
    return  setErrorID(ERROR_MON_ENTRY_HOME_NULL);
  }

  if(enableHardwareInterlock_){
    error=validateEntryBit(3);
    if(error){ //External interlock
      return  setErrorID(ERROR_MON_ENTRY_EXT_INTERLOCK_NULL);
    }
  }

  return 0;
}

int ecmcMonitor::setVelSet(double vel)
{
  setVel_=vel;
  return 0;
}

int ecmcMonitor::setActVel(double vel)
{
  actVel_=vel;
  return 0;
}

int ecmcMonitor::setMaxVel(double vel)
{
  maxVel_=vel;
  return 0;
}

int ecmcMonitor::setEnableMaxVelMon(bool enable)
{
  enableMaxVelMon_=enable;
  return 0;
}

bool ecmcMonitor::getEnableMaxVelMon()
{
  return enableMaxVelMon_;
}

int ecmcMonitor::setMaxVelDriveTime(int time)
{
  maxVelDriveILDelay_=time;
  return 0;
}

int ecmcMonitor::setMaxVelTrajTime(int time)
{
  maxVelTrajILDelay_=time;
  return 0;
}

int ecmcMonitor::reset()
{
  atTarget_=false;
  lagErrorTraj_=false;
  lagErrorDrive_=false;
  velErrorTraj_=false;
  velErrorDrive_=false;
  cntrlOutputHLErrorTraj_=false;
  cntrlOutputHLErrorDrive_=false;
  cntrlOutIncreaseAtLimitErrorTraj_=false;
  cntrlOutIncreaseAtLimitErrorDrive_=false;
  atTargetCounter_=0;
  lagMonCounter_=0;
  maxVelCounterDrive_=0;
  maxVelCounterTraj_=0;
  cntrlOutIncreaseAtLimitCounter_=0;
  reasonableMoveCounter_=0;
  cycleCounter_=0;
  return 0;
}

void ecmcMonitor::errorReset()
{
  reset();
  ecmcError::errorReset();
}

int ecmcMonitor::setEnableHardwareInterlock(bool enable)
{
   if(enable){
     int error=validateEntryBit(3);
     if(error){
       return setErrorID(ERROR_MON_ENTRY_HARDWARE_INTERLOCK_NULL);
     }
   }
   enableHardwareInterlock_=enable;
   return 0;
}

int ecmcMonitor::setCntrlOutput(double output)
{
  cntrlOutputOld_=cntrlOutput_;
  cntrlOutput_=output;
  return 0;
}

int ecmcMonitor::setCntrlOutputHL(double outputHL)
{
  cntrlOutputHL_=outputHL;
  return 0;
}

int ecmcMonitor::setEnableCntrlHLMon(bool enable)
{
  enableCntrlHLMon_=enable;
  return 0;
}

bool ecmcMonitor::getEnableCntrlHLMon()
{
  return enableCntrlHLMon_;
}

int ecmcMonitor::setEnableCntrlOutIncreaseAtLimitMon(bool enable)
{
  enableCntrlOutIncreaseAtLimitMon_=enable;
  return 0;
}

bool ecmcMonitor::getEnableCntrlOutIncreaseAtLimitMon()
{
  return enableCntrlOutIncreaseAtLimitMon_;
}

int ecmcMonitor::setCntrlKff(double kff)
{
  cntrlKff_=kff;
  return 0;
}

int ecmcMonitor::setEnableHardLimitBWDAlarm(bool enable)
{
  enableAlarmAtHardlimitBwd_=enable;
  return 0;
}

int ecmcMonitor::setEnableHardLimitFWDAlarm(bool enable)
{
  enableAlarmAtHardlimitFwd_=enable;
  return 0;
}

int ecmcMonitor::setEnableSoftLimitBwd(bool enable)
{
  enableSoftLimitBwd_=enable;
  return 0;
}

int ecmcMonitor::setEnableSoftLimitFwd(bool enable)
{
  enableSoftLimitFwd_=enable;
  return 0;
}

int ecmcMonitor::setSoftLimitBwd(double limit)
{
  softLimitBwd_=limit;
  return 0;
}

int ecmcMonitor::setSoftLimitFwd(double limit)
{
  softLimitFwd_=limit;
  return 0;
}

int ecmcMonitor::checkLimits()
{
  //Both limit switches
  bothLimitsLowInterlock_=!hardBwd_ && !hardFwd_;
  if(bothLimitsLowInterlock_){
    return setErrorID(ERROR_MON_BOTH_LIMIT_INTERLOCK);
  }

  //Bwd limit switch
  if(!hardBwd_ && (setVel_<0 || currSetPos_<currSetPosOld_) ){
    bwdLimitInterlock_=true;
    if(enableAlarmAtHardlimitBwd_){
      return setErrorID(ERROR_MON_HARD_LIMIT_BWD_INTERLOCK);
    }
  }
  else {
      bwdLimitInterlock_=false;
  }

  //Fwd limit switch
  if(!hardFwd_ && (setVel_>0 || currSetPos_>currSetPosOld_)){
    fwdLimitInterlock_=true;
    if(enableAlarmAtHardlimitFwd_){
      return setErrorID(ERROR_MON_HARD_LIMIT_FWD_INTERLOCK);
    }
  }
  else{
      fwdLimitInterlock_=false;
  }

  //printf("enableSoftLimitBwd_=%d,enableSoftLimitFwd_=%d.\n",enableSoftLimitBwd_,enableSoftLimitFwd_);
  //Soft bwd limit
  bwdSoftLimitInterlock_=enableSoftLimitBwd_ && (setVel_<0) && (actPos_-softLimitBwd_<=distToStop_);
  if(bwdSoftLimitInterlock_){
    return setErrorID(ERROR_MON_SOFT_LIMIT_BWD_INTERLOCK);
  }

  //Soft fwd limit
  fwdSoftLimitInterlock_=enableSoftLimitFwd_ && (setVel_>0) && (softLimitFwd_-actPos_<=distToStop_);
  if(fwdSoftLimitInterlock_){
    return setErrorID(ERROR_MON_SOFT_LIMIT_FWD_INTERLOCK);
  }

  return 0;
}

int ecmcMonitor::checkAtTarget()
{
  atTarget_=false;
  //At target
  if(enableAtTargetMon_){
    if(std::abs(targetPos_-actPos_)<atTargetTol_){
      if (atTargetCounter_<=atTargetTime_){
        atTargetCounter_++;
      }
      if(atTargetCounter_>atTargetTime_){
        atTarget_=true;
      }
    }
    else{
      atTargetCounter_=0;
    }
  }
  else{
    atTarget_=true;
  }
  return 0;
}


int ecmcMonitor::checkPositionLag()
{
  lagErrorTraj_=false;
  lagErrorDrive_=false;

  if(enableLagMon_ && !(lagErrorDrive_)){
     lagError_=std::abs(actPos_-currSetPos_);

     if(lagError_>posLagTol_){
       if(lagMonCounter_<=posLagTime_*2){
         lagMonCounter_++;
       }
       if(lagMonCounter_>posLagTime_){
         lagErrorTraj_=true;
       }
       if(lagMonCounter_>=posLagTime_*2){  //interlock the drive in twice the time..
 	lagErrorDrive_=true;
       }
     }
     else{
       lagMonCounter_=0;
     }
   }
   if(lagErrorDrive_ || lagErrorTraj_){
     return setErrorID(ERROR_MON_MAX_POSITION_LAG_EXCEEDED);
   }
  return 0;
}

int ecmcMonitor::checkMaxVelocity()
{
  if((std::abs(actVel_)>maxVel_ || std::abs(setVel_)>maxVel_) && enableMaxVelMon_){
      if(maxVelCounterTraj_ <= maxVelTrajILDelay_){
        maxVelCounterTraj_++;
      }
   }
   else{
     maxVelCounterTraj_=0;
   }

   if(!velErrorTraj_){
     velErrorTraj_=maxVelCounterTraj_>=maxVelTrajILDelay_;
   }

   if(velErrorTraj_ &&  maxVelCounterDrive_<= maxVelDriveILDelay_){
     maxVelCounterDrive_++;
   }
   else{
     maxVelCounterDrive_=0;
   }

   velErrorDrive_=velErrorTraj_ && maxVelCounterDrive_>=maxVelDriveILDelay_;

   if(velErrorDrive_ || velErrorTraj_){
     return setErrorID(ERROR_MON_MAX_VELOCITY_EXCEEDED);
   }
   return 0;
}

int ecmcMonitor::checkCntrlMaxOutput()
{
  if(enableCntrlHLMon_ && std::abs(cntrlOutput_)>cntrlOutputHL_){
    cntrlOutputHLErrorDrive_=true;
    cntrlOutputHLErrorTraj_=true;
    return setErrorID(ERROR_MON_CNTRL_OUTPUT_EXCEED_LIMIT);
  }
  return 0;
}

int ecmcMonitor::checkCntrloutputIncreaseAtLimit()
{
  //TODO This functionality is nor working properly. JUST FOR TEST.. NEEDS rewriting
  positionErrorOld_=positionError_;
  positionError_=std::abs(targetPos_-actPos_);

  if(enableCntrlOutIncreaseAtLimitMon_ &&  std::abs(cntrlKff_)>0 &&(!hardFwd_ || !hardBwd_)){
    cycleCounter_++;
    if(positionError_>0 && positionErrorOld_>0 && ((!hardFwd_ && (cntrlOutput_>cntrlOutputOld_) && cntrlOutputOld_>0) || (!hardBwd_ && (cntrlOutput_<cntrlOutputOld_ && cntrlOutputOld_<0)))){
      cntrlOutIncreaseAtLimitCounter_++;
    }

    //speed should be cntrlOutput_/cntrlKff_
    double currentSetVelocity=cntrlOutput_/cntrlKff_;
    if(((currentSetVelocity>=0 && actVel_>=0.5*currentSetVelocity) || (currentSetVelocity<=0 && actVel_<=0.5*currentSetVelocity))){
      reasonableMoveCounter_++;
    }

    if(cycleCounter_>2000){ //TODO Not so nice..
      if(cntrlOutIncreaseAtLimitCounter_>1200 && reasonableMoveCounter_<1200){
        cntrlOutIncreaseAtLimitErrorTraj_=true;
  	cntrlOutIncreaseAtLimitErrorDrive_=true;
        return setErrorID(ERROR_MON_CNTRL_OUTPUT_INCREASE_AT_LIMIT);
      }
      cycleCounter_=0;
      cntrlOutIncreaseAtLimitCounter_=0;
      reasonableMoveCounter_=0;
    }
  }
  return 0;
}

int ecmcMonitor::setDistToStop(double distance)
{
  distToStop_=distance;
  return 0;
}

int ecmcMonitor::getEnableAlarmAtHardLimit()
{
  return enableAlarmAtHardlimitBwd_ || enableAlarmAtHardlimitFwd_;
}

double ecmcMonitor::getSoftLimitBwd()
{
  return softLimitBwd_;
}
double ecmcMonitor::getSoftLimitFwd()
{
  return softLimitFwd_;
}

bool ecmcMonitor::getEnableSoftLimitBwd()
{
  return enableSoftLimitBwd_;
}

bool ecmcMonitor::getEnableSoftLimitFwd()
{
  return enableSoftLimitFwd_;
}

bool ecmcMonitor::getAtSoftLimitBwd()
{
  return enableSoftLimitBwd_ && actPos_<=softLimitBwd_;
}

bool ecmcMonitor::getAtSoftLimitFwd()
{
  return enableSoftLimitFwd_ && actPos_>=softLimitFwd_;
}

int ecmcMonitor::setExtTrajInterlock(interlockTypes interlock)
{
  extTrajInterlock_=interlock;
  return 0;
}

int ecmcMonitor::setExtEncInterlock(interlockTypes interlock)
{
  extEncInterlock_=interlock;
  return 0;
}
