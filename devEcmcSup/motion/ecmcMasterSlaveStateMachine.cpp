/*************************************************************************\
* Copyright (c) 2024 Paul Scherrer Institut
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcMasterSlaveStateMachine.cpp
*
*  Created on: Jun 09, 2025
*      Author: anderssandstrom
*
\*************************************************************************/
#include "ecmcMasterSlaveStateMachine.h"

ecmcMasterSlaveStateMachine::ecmcMasterSlaveStateMachine(ecmcAsynPortDriver *asynPortDriver,
                                                         int index,
                                                         const char *name,
                                                         double sampleTimeS,
                                                         ecmcAxisGroup *masterGrp,
                                                         ecmcAxisGroup *slaveGrp,
                                                         int autoDisbleMasters,
                                                         int autoDisbleSlaves){
  asynPortDriver_           = asynPortDriver;
  asynControl_              = NULL;
  asynState_                = NULL;
  asynStatus_               = NULL;
  index_                    = index;
  name_                     = name;
  sampleTimeS_              = sampleTimeS;
  timeCounter_              = 0;
  masterGrp_                = masterGrp;
  slaveGrp_                 = slaveGrp;
  validationOK_             = false;
  asynInitOk_               = false;  
  status_                   = 0;
  state_                    = ECMC_MST_SLV_STATE_IDLE;    
  idleCounter_              = 0;
  memset(&control_,0,sizeof(control_));
  memset(&controlOld_,0,sizeof(controlOld_));

  control_.enable             = 1;
  control_.autoDisableMasters = autoDisbleMasters;
  control_.autoDisableSlaves  = autoDisbleSlaves;
  control_.enableDbgPrintouts = false;
  
  slaveGrp_->setMRIgnoreDisableStatusCheck(true);
  masterGrp_->setMRIgnoreDisableStatusCheck(true);

  LOGINFO("ecmcMasterSlaveStateMachine: %s:  Created master slave state machine [%d]\n",
          name_.c_str(),
          index_);

  initAsyn();
};

ecmcMasterSlaveStateMachine::~ecmcMasterSlaveStateMachine(){
};

const char* ecmcMasterSlaveStateMachine::getName(){
  return name_.c_str();
};

int ecmcMasterSlaveStateMachine::enterResetState(int errorCode,
                                                 const char *reason) {
  if (errorCode) {
    status_ = errorCode;
    setErrorID(errorCode);
  }

  if (reason) {
    LOGERR("ecmcMasterSlaveStateMachine: %s: %s (0x%x)\n",
           name_.c_str(),
           reason,
           errorCode);
  }

  masterGrp_->halt();
  slaveGrp_->halt();

  const int masterDisableError = masterGrp_->setEnable(0);
  const int slaveDisableError  = slaveGrp_->setEnable(0);
  masterGrp_->setMRCnen(0);
  slaveGrp_->setMRCnen(0);
  masterGrp_->setMRStop(1);
  masterGrp_->setMRSync(1);
  slaveGrp_->setMRStop(1);
  slaveGrp_->setMRSync(1);
  const int slaveTrajError = slaveGrp_->setTrajSrc(ECMC_DATA_SOURCE_INTERNAL);

  if (status_ == 0) {
    const int latchedError = masterDisableError ? masterDisableError :
                             (slaveDisableError ? slaveDisableError :
                              slaveTrajError);
    if (latchedError) {
      status_ = latchedError;
      setErrorID(latchedError);
    }
  }

  masterGrp_->setEnableAutoDisable(1);
  state_ = ECMC_MST_SLV_STATE_RESET;
  return status_;
}

int ecmcMasterSlaveStateMachine::setSlaveTrajSrcChecked(dataSource trajSource,
                                                        const char *reason) {
  const int error = slaveGrp_->setTrajSrc(trajSource);
  if (error) {
    enterResetState(error, reason);
  }
  return error;
}

void ecmcMasterSlaveStateMachine::execute(){

  //always update
  refreshAsyn();

  setMrIgnoreEnableAlarm();

  if(!control_.enable) {
    // unblock commands
    if(state_ != ECMC_MST_SLV_STATE_IDLE) {
      masterGrp_->setBlocked(false);
      slaveGrp_->setBlocked(false);
      if (setSlaveTrajSrcChecked(ECMC_DATA_SOURCE_INTERNAL,
                                 "Failed switching slave axes to internal trajectory while disabling SM")) {
        controlOld_ = control_;
        return;
      }
      state_ = ECMC_MST_SLV_STATE_IDLE;
    }
    controlOld_ = control_;
    return;
  }

  if(!validationOK_) {
    return;
  }
  
  if(timeCounter_ < MST_SLV_START_DELAY_S) {
    timeCounter_+=sampleTimeS_;
    return;
  }

  switch(state_) {
    case ECMC_MST_SLV_STATE_IDLE:
      stateIdle();
      break;
    case ECMC_MST_SLV_STATE_SLAVES:
      idleCounter_ = 0;
      stateSlave();
      break;
    case ECMC_MST_SLV_STATE_MASTERS:
      idleCounter_ = 0;
      stateMaster();
      break;
    case ECMC_MST_SLV_STATE_RESET:
      idleCounter_ = 0;
      stateReset();
      break;
  };
  controlOld_ = control_;
};

int ecmcMasterSlaveStateMachine::stateIdle(){
  
  // Slaved axis busy will stay high for 2 cycles after traj source change.
  // Needed in case stop ramp for the slaves is needed.
  // TODO: Fix.. Need better solution here
  if(idleCounter_ < 3) {
    idleCounter_++;
    return 0;
  }

  slaveGrp_->setBlocked(false);
  masterGrp_->setBlocked(false);

  //masterGrp_->setEnable(false);
  //slaveGrp_->setEnable(false);

  const ecmcAxisGroupStatusSummary slaveStatus = slaveGrp_->getStatusSummary(false);
  const ecmcAxisGroupStatusSummary masterStatus = masterGrp_->getStatusSummary(false);
  const bool anySlaveBusy       = slaveStatus.anyBusy;
  const int anySlaveErrorId     = slaveStatus.firstErrorId;
  const bool anyMasterEnabled   = masterStatus.anyEnabled;
  const bool anyMasterEnableCmd = masterStatus.anyEnableCmd;
  const bool anyMasterBusy      = masterStatus.anyBusy;
  const bool anySlaveTrajAnyExt = slaveStatus.anyTrajExternal;
  // Actual enabled state can lag the master-side request. Treat either an
  // enable command or an already-busy master as a takeover request.
  const bool masterRequestsControl = anyMasterEnableCmd ||
                                     anyMasterEnabled ||
                                     anyMasterBusy;

  // State transision to SLAVE
  if( anySlaveBusy &&
    !anySlaveTrajAnyExt &&
    !anyMasterEnabled &&
    !anyMasterEnableCmd) {
    // (un)block commands
    slaveGrp_->setBlocked(false);
    masterGrp_->setBlocked(true);
    state_ = ECMC_MST_SLV_STATE_SLAVES;
    if(control_.enableDbgPrintouts) {
      LOGINFO("ecmcMasterSlaveStateMachine: %s: State change, IDLE -> SLAVE\n",
              name_.c_str());
    }

  // State transision to MASTER
  } else if(masterRequestsControl &&
            anySlaveErrorId == 0 ) {
    const bool allSlavesEnabled = slaveStatus.allEnabled;
    if(allSlavesEnabled && !anySlaveBusy){
      if(anyMasterBusy) {
        if (setSlaveTrajSrcChecked(ECMC_DATA_SOURCE_EXTERNAL,
                                   "Failed switching slave axes to external trajectory")) {
          return 0;
        }
        state_ = ECMC_MST_SLV_STATE_MASTERS;
        // (un)block commands
        masterGrp_->setBlocked(false);
        slaveGrp_->setBlocked(true);
        if(control_.enableDbgPrintouts) {
          LOGINFO("ecmcMasterSlaveStateMachine: %s: State change, IDLE -> MASTER\n",
                  name_.c_str());
        }
      }

    } else {
      int errorSlave = slaveGrp_->setEnable(1);
      int errorMaster = masterGrp_->setEnable(1);
      if(errorSlave || errorMaster) {
        const int transitionError = errorSlave ? errorSlave : errorMaster;
        status_ = transitionError;
        setErrorID(transitionError);
        slaveGrp_->setEnable(0);
        masterGrp_->setEnable(0);
        slaveGrp_->setMRCnen(0);
        masterGrp_->setMRCnen(0);
        slaveGrp_->setMRSync(1);
        masterGrp_->setMRSync(1);
        if (setSlaveTrajSrcChecked(ECMC_DATA_SOURCE_INTERNAL,
                                   "Failed restoring slave axes to internal trajectory after enable error")) {
          return 0;
        }
        if(errorSlave) {
          masterGrp_->setSlavedAxisInError();
        }
        if(control_.enableDbgPrintouts) {
          LOGINFO("ecmcMasterSlaveStateMachine: %s: Error enabling axes\n",
                  name_.c_str());
          LOGINFO("ecmcMasterSlaveStateMachine: %s: State change, IDLE -> IDLE\n",
                  name_.c_str());
        }
        state_ = ECMC_MST_SLV_STATE_IDLE;
        
      }
      if(anySlaveBusy) {
        slaveGrp_->halt();
      }
    }
  } else if(anySlaveErrorId > 0){
    masterGrp_->setSlavedAxisInError();
  }

  return 0;
}

int ecmcMasterSlaveStateMachine::stateSlave(){

  const ecmcAxisGroupStatusSummary slaveStatus = slaveGrp_->getStatusSummary(false);
  const ecmcAxisGroupStatusSummary masterStatus = masterGrp_->getStatusSummary(false);
  const bool anySlaveBusy = slaveStatus.anyBusy;

  // Maybe add atTarget here?! 
  // Keep like this since this can be handled by adding diableTimout in slave axes and autoDisableSlaves=false
  if(!anySlaveBusy) {
    // Auto disable also if the axis has no cfg to auto disable
    if(control_.autoDisableSlaves || (!slaveGrp_->getAxisAutoDisableEnabled())) {
      const int error = slaveGrp_->setEnable(0);
      if (error) {
        enterResetState(error, "Failed disabling slave group in SLAVE state");
        return 0;
      }
      //slaveGrp_->setMRCnen(0);     
    }
    
    if(control_.autoDisableMasters) {      
      const int error = masterGrp_->setEnable(0);
      if (error) {
        enterResetState(error, "Failed disabling master group in SLAVE state");
        return 0;
      }
      masterGrp_->setMRCnen(0);
    }

    if (setSlaveTrajSrcChecked(ECMC_DATA_SOURCE_INTERNAL,
                               "Failed switching slave axes to internal trajectory in SLAVE state")) {
      return 0;
    }

    // Sync the master axes
    masterGrp_->setMRSync(1);
    masterGrp_->setMRStop(1);

    state_ = ECMC_MST_SLV_STATE_IDLE;
    if(control_.enableDbgPrintouts) {
      LOGINFO("ecmcMasterSlaveStateMachine: %s: State change, SLAVE -> IDLE\n",
              name_.c_str());
    }
  }

  const bool anyMasterEnabled = masterStatus.anyEnabled;
  const bool anyMasterEnableCmd = masterStatus.anyEnableCmd;
  if(anyMasterEnabled || anyMasterEnableCmd) {
    const int error = masterGrp_->setEnable(0);
    if (error) {
      enterResetState(error, "Failed forcing master group disabled in SLAVE state");
      return 0;
    }
    masterGrp_->setMRCnen(0);
  }
  return 0;
}

int ecmcMasterSlaveStateMachine::stateMaster(){

  const ecmcAxisGroupStatusSummary slaveStatus = slaveGrp_->getStatusSummary(false);
  const ecmcAxisGroupStatusSummary masterStatus = masterGrp_->getStatusSummary(false);
  const bool slaveAnyError = slaveStatus.firstErrorId > 0;
  const bool masterAnyEnabled = masterStatus.anyEnabled;
  const bool masterAnyBusy = masterStatus.anyBusy;
  const bool masterAnyError = masterStatus.firstErrorId > 0;

  if(slaveAnyError) {
    masterGrp_->setSlavedAxisInError();
  }

  if(masterAnyEnabled && !masterAnyBusy) {
    if(control_.autoDisableMasters) {
      int error = slaveGrp_->setEnable(0);
      if (!error) {
        error = masterGrp_->setEnable(0);
      }
      if (error) {
        enterResetState(error, "Failed auto-disabling groups in MASTER state");
        return 0;
      }
      slaveGrp_->setMRCnen(0);
      masterGrp_->setMRCnen(0);
    }
  }
  
  bool lostEnableCmd = false;
  if(masterAnyBusy && !masterAnyError) {
    lostEnableCmd = !slaveStatus.allEnableCmd || !masterStatus.allEnableCmd;
  }
  
  // One master or slave axis gets killed during motion then kill all and goto IDLE
  if((masterAnyBusy && lostEnableCmd) || masterAnyError) {
    bool lostEnabled = masterAnyEnabled && !masterStatus.allEnabled;
    lostEnabled = lostEnabled || (slaveStatus.anyEnabled && !slaveStatus.allEnabled);
    if(lostEnabled || masterAnyError) {
      state_ = ECMC_MST_SLV_STATE_RESET;
      LOGERR("ecmcMasterSlaveStateMachine: %s: At least one axis lost enable during motion. Disable all axes..\n",
             name_.c_str());
      LOGERR("ecmcMasterSlaveStateMachine: %s: State change, MASTER -> RESET\n",
             name_.c_str());
      masterGrp_->halt();
      masterGrp_->setEnable(0);
      masterGrp_->setMRStop(1);
      masterGrp_->setMRSync(1);
      slaveGrp_->halt();
      const int slaveDisableError = slaveGrp_->setEnable(0);
      if (slaveDisableError) {
        status_ = slaveDisableError;
        setErrorID(slaveDisableError);
      }
      slaveGrp_->setMRStop(1);
      slaveGrp_->setMRSync(1);
      if (setSlaveTrajSrcChecked(ECMC_DATA_SOURCE_INTERNAL,
                                 "Failed switching slave axes to internal trajectory during MASTER fault handling")) {
        return 0;
      }
      if(!masterAnyError) { // Dont overwrite error if master error
        masterGrp_->setSlavedAxisIlocked();
      }
      masterGrp_->setEnableAutoDisable(1);
      //stateReset(); // A bit nasty but ....
      return 0;
    }
  }

  // Refresh once here so decisions below are based on the latest state after
  // potential enable/disable actions above.
  const ecmcAxisGroupStatusSummary slaveStatusNow = slaveGrp_->getStatusSummary();
  const ecmcAxisGroupStatusSummary masterStatusNow = masterGrp_->getStatusSummary();

  // postpone disable until all master axes are done
  //masterGrp_->setEnableAutoDisable(masterGrp_->getAnyBusy() == 0);
  const bool masterAtTarget = masterStatusNow.allAtTarget;
  masterGrp_->setEnableAutoDisable(masterAtTarget && !masterStatusNow.anyBusy);
  // ensure attarget/reduced current of slave axes
  const bool masterWithinCtrlDb = masterStatusNow.allWithinCtrlDb;
  slaveGrp_->setAxisIsWithinCtrlDBExtTraj(masterWithinCtrlDb);
  
  // Ilock or if any slaved axis is changing to internal source
  const bool anySlaveIlocked = slaveStatusNow.anyIlocked;
  const bool allSlaveTrajExternal = slaveStatusNow.allTrajExternal;
  if(anySlaveIlocked || !allSlaveTrajExternal){
    if (setSlaveTrajSrcChecked(ECMC_DATA_SOURCE_INTERNAL,
                               "Failed switching slave axes to internal trajectory after slave interlock")) {
      return 0;
    }
    slaveGrp_->setMRSync(1);
    slaveGrp_->setMRStop(1);
    slaveGrp_->halt();
    masterGrp_->setSlavedAxisIlocked();
    masterGrp_->setEnableAutoDisable(1);
    state_ = ECMC_MST_SLV_STATE_SLAVES;
    if(control_.enableDbgPrintouts) {
      LOGINFO("ecmcMasterSlaveStateMachine: %s: Slaved axis interlock (or traj source change)\n",
              name_.c_str());
      LOGINFO("ecmcMasterSlaveStateMachine: %s: State change, MASTER -> SLAVE\n",
              name_.c_str());
    }
    return 0;
  }
  
  // Done?
  if(!masterStatusNow.anyEnabled) {
    const int error = slaveGrp_->setEnable(0);
    if (error) {
      enterResetState(error, "Failed disabling slave group when leaving MASTER state");
      return 0;
    }
    slaveGrp_->setMRCnen(0);
    if (setSlaveTrajSrcChecked(ECMC_DATA_SOURCE_INTERNAL,
                               "Failed switching slave axes to internal trajectory when leaving MASTER state")) {
      return 0;
    }
    masterGrp_->setEnableAutoDisable(1);
    state_ = ECMC_MST_SLV_STATE_IDLE;
    if(control_.enableDbgPrintouts) {
      LOGINFO("ecmcMasterSlaveStateMachine: %s: State change, MASTER -> IDLE\n",
              name_.c_str());
    }
  }
  return 0;
}

int ecmcMasterSlaveStateMachine::stateReset() {
  int error = slaveGrp_->setEnable(0);
  if (!error) {
    error = masterGrp_->setEnable(0);
  }
  if (error) {
    status_ = error;
    setErrorID(error);
    return error;
  }
  slaveGrp_->setMRCnen(0);
  masterGrp_->setMRCnen(0);
  error = slaveGrp_->setTrajSrc(ECMC_DATA_SOURCE_INTERNAL);
  if (error) {
    status_ = error;
    setErrorID(error);
    return error;
  }
  masterGrp_->setEnableAutoDisable(1);
  masterGrp_->setBlocked(false);
  slaveGrp_->setBlocked(false);
  state_ = ECMC_MST_SLV_STATE_IDLE;
  if(control_.enableDbgPrintouts) {
    LOGINFO("ecmcMasterSlaveStateMachine: %s: State change, RESET -> IDLE\n",
            name_.c_str());
  }
  return  0;
}

int ecmcMasterSlaveStateMachine::validate(){
  if( masterGrp_ == NULL || slaveGrp_ == NULL){
    return ERROR_MST_SLV_SM_GRP_NULL;
  };

  if (masterGrp_ == slaveGrp_) {
    return ERROR_MST_SLV_SM_GRP_SAME;
  }

  if ((masterGrp_->size() == 0) || (slaveGrp_->size() == 0)) {
    return ERROR_MST_SLV_SM_GRP_EMPTY;
  }

  if( !asynInitOk_){
    return ERROR_MST_SLV_SM_GRP_INIT_ASYN_FAILED;
  };

  validationOK_ = true;
  return 0;
};



int ecmcMasterSlaveStateMachine::initAsyn() {
  if (asynPortDriver_ == NULL) {
    LOGERR("%s/%s:%d: ERROR (master/slave state-machine %d): AsynPortDriver object NULL (0x%x).\n",
           __FILE__,
           __FUNCTION__,
           __LINE__,
           index_,
           ERROR_AXIS_ASYN_PORT_OBJ_NULL);
    return ERROR_AXIS_ASYN_PORT_OBJ_NULL;
  }

  ecmcAsynDataItem *paramTemp = NULL;
  int errorCode               = 0;

  // Control
  errorCode = createAsynParam(ECMC_MST_SLV_OBJ_STR "%d.control",
                              asynParamInt32,
                              ECMC_EC_U32,
                              (uint8_t *)&(control_),
                              sizeof(control_),
                              &paramTemp);

  if (errorCode) {
    LOGERR("Error creating asyn parameter for control word\n");
    return errorCode;
  }

  paramTemp->setAllowWriteToEcmc(true);
  paramTemp->addSupportedAsynType(asynParamUInt32Digital);
  paramTemp->refreshParam(1);
  asynControl_ = paramTemp;

  // State
  errorCode = createAsynParam(ECMC_MST_SLV_OBJ_STR "%d." ECMC_MST_SLVS_STR_STATE,
                              asynParamInt32,
                              ECMC_EC_U32,
                              (uint8_t *)&(state_),
                              sizeof(state_),
                              &paramTemp);

  if (errorCode) {
    LOGERR("Error creating asyn parameter for state\n");
    return errorCode;
  }
  paramTemp->setAllowWriteToEcmc(true);
  paramTemp->refreshParam(1);
  asynState_ = paramTemp;

  // Status
  errorCode = createAsynParam(ECMC_MST_SLV_OBJ_STR "%d." ECMC_MST_SLVS_STR_STATUS,
                              asynParamInt32,
                              ECMC_EC_U32,
                              (uint8_t *)&(status_),
                              sizeof(status_),
                              &paramTemp);

  if (errorCode) {
    LOGERR("Error creating asyn parameter for status\n");
    return errorCode;
  }

  paramTemp->setAllowWriteToEcmc(false);
  paramTemp->refreshParam(1);
  asynStatus_ = paramTemp;

  // asyn init fine!
  asynInitOk_ = true;
  return 0;
}

int ecmcMasterSlaveStateMachine::createAsynParam(const char        *nameFormat,
                                  asynParamType      asynType,
                                  ecmcEcDataType     ecmcType,
                                  uint8_t           *data,
                                  size_t             bytes,
                                  ecmcAsynDataItem **asynParamOut) {
  if (asynPortDriver_ == NULL) {
    LOGERR(
      "%s/%s:%d: ERROR (master/slave state-machine %d): AsynPortDriver object NULL (%s) (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      index_,
      nameFormat,
      ERROR_AXIS_ASYN_PORT_OBJ_NULL);
    return ERROR_AXIS_ASYN_PORT_OBJ_NULL;
  }
  *asynParamOut = NULL;
  char  buffer[EC_MAX_OBJECT_PATH_CHAR_LENGTH];
  char *name                  = NULL;
  unsigned int charCount      = 0;
  ecmcAsynDataItem *paramTemp = NULL;

  charCount = snprintf(buffer,
                       sizeof(buffer),
                       nameFormat,
                       index_);

  if (charCount >= sizeof(buffer) - 1) {
    LOGERR(
      "%s/%s:%d: ERROR (master/slave state-machine %d): Failed to generate (%s). Buffer to small (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      index_,
      nameFormat,
      ERROR_AXIS_ASYN_PRINT_TO_BUFFER_FAIL);
    return ERROR_AXIS_ASYN_PRINT_TO_BUFFER_FAIL;
  }
  name      = buffer;
  paramTemp = asynPortDriver_->addNewAvailParam(name,
                                                asynType,
                                                data,
                                                bytes,
                                                ecmcType,
                                                0);

  if (!paramTemp) {
    LOGERR(
      "%s/%s:%d: ERROR (master/slave state-machine %d): Add create default parameter for %s failed.\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      index_,
      name);
    return ERROR_MAIN_ASYN_CREATE_PARAM_FAIL;
  }
  paramTemp->setAllowWriteToEcmc(false);  
  paramTemp->refreshParam(1);
  *asynParamOut = paramTemp;
  return 0;
}

void ecmcMasterSlaveStateMachine::refreshAsyn() {
  asynStatus_->refreshParamRT(0);
  asynControl_->refreshParamRT(0);
  asynState_->refreshParamRT(0);
}

void ecmcMasterSlaveStateMachine::setMrIgnoreEnableAlarm() {
  // Change check MR status check on edge of enable
  if(!control_.enable && controlOld_.enable) {
    slaveGrp_->setMRIgnoreDisableStatusCheck(false);
    masterGrp_->setMRIgnoreDisableStatusCheck(false);
  }

  if(control_.enable && !controlOld_.enable) {
    slaveGrp_->setMRIgnoreDisableStatusCheck(true);
    masterGrp_->setMRIgnoreDisableStatusCheck(true);
  }
}
