/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcAxisReal.cpp
*
*  Created on: Mar 10, 2016
*      Author: anderssandstrom
*
\*************************************************************************/

#include "ecmcAxisReal.h"
#include "ecmcRtLogger.h"

#include <cmath>

namespace {

constexpr double ECMC_CSV_MIN_RAW_VELO_AT_TARGET_TOL = 0.5;

bool axisUsesEcmcPositionController(ecmcAxisData &data) {
  return data.control_.drvMode == ECMC_DRV_MODE_CSV ||
         (data.control_.drvMode == ECMC_DRV_MODE_CSP &&
          data.control_.cspDrvEncIndex >= 0);
}

void warnIfCsvControllerOutputRoundsToZero(ecmcAxisData  &data,
                                           ecmcDriveBase *drv,
                                           const char    *controllerName,
                                           double         kp,
                                           double         ki,
                                           double         tol) {
  if (!drv || data.control_.drvMode != ECMC_DRV_MODE_CSV ||
      ki != 0 || tol <= 0) {
    return;
  }

  const double driveScaleNum = drv->getScaleNum();
  const double driveScaleDenom = drv->getScaleDenom();

  if (driveScaleNum == 0) {
    return;
  }

  const double rawVelocityAtTol = kp * driveScaleDenom / driveScaleNum * tol;
  const double rawVelocityAtTolAbs = std::abs(rawVelocityAtTol);

  if (rawVelocityAtTolAbs >= ECMC_CSV_MIN_RAW_VELO_AT_TARGET_TOL) {
    return;
  }

  ecmcLogBufferLogWarning(
    "Axis[%d]: CSV %s output %.6g raw < %.6g; velocity may round to 0 "
    "(kp=%.6g, tol=%.6g, scale=%g/%g).",
    data.status_.axisId,
    controllerName,
    rawVelocityAtTolAbs,
    ECMC_CSV_MIN_RAW_VELO_AT_TARGET_TOL,
    kp,
    tol,
    driveScaleNum,
    driveScaleDenom);

  ecmcRtLoggerLogWarning(
    "%s/%s:%d: WARNING: Axis[%d]: CSV %s output %.6g raw < %.6g; velocity may round to 0 "
    "(kp=%.6g, tol=%.6g, scale=%g/%g).\n",
    __FILE__,
    __FUNCTION__,
    __LINE__,
    data.status_.axisId,
    controllerName,
    rawVelocityAtTolAbs,
    ECMC_CSV_MIN_RAW_VELO_AT_TARGET_TOL,
    kp,
    tol,
    driveScaleNum,
    driveScaleDenom);
}

void warnIfCsvControllerOutputsRoundToZero(ecmcAxisData     &data,
                                           ecmcDriveBase    *drv,
                                           ecmcPIDController *cntrl,
                                           ecmcMonitor      *mon) {
  if (!cntrl || !mon) {
    return;
  }

  if (mon->getEnableAtTargetMon()) {
    warnIfCsvControllerOutputRoundsToZero(data,
                                          drv,
                                          "at-target",
                                          cntrl->getKp(),
                                          cntrl->getKi(),
                                          mon->getAtTargetTol());
  }

  warnIfCsvControllerOutputRoundsToZero(data,
                                        drv,
                                        "inner",
                                        cntrl->getInnerKp(),
                                        cntrl->getInnerKi(),
                                        cntrl->getInnerTol());
}

void warnIfControllerKpIsZero(ecmcAxisData     &data,
                              ecmcPIDController *cntrl) {
  if (!cntrl || !axisUsesEcmcPositionController(data)) {
    return;
  }

  if (cntrl->getKp() == 0) {
    ecmcLogBufferLogWarning(
      "Axis[%d]: controller kp is 0.",
      data.status_.axisId);

    ecmcRtLoggerLogWarning(
      "%s/%s:%d: WARNING: Axis[%d]: controller kp is 0.\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      data.status_.axisId);
  }

  if ((cntrl->getInnerTol() > 0) && (cntrl->getInnerKp() == 0)) {
    ecmcLogBufferLogWarning(
      "Axis[%d]: inner controller kp is 0.",
      data.status_.axisId);

    ecmcRtLoggerLogWarning(
      "%s/%s:%d: WARNING: Axis[%d]: inner controller kp is 0.\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      data.status_.axisId);
  }
}

void warnIfControllerParamsIgnoredInPureCsp(ecmcAxisData     &data,
                                            ecmcPIDController *cntrl) {
  if (!cntrl ||
      data.control_.drvMode != ECMC_DRV_MODE_CSP ||
      data.control_.cspDrvEncIndex >= 0 ||
      !cntrl->getSettingMade()) {
    return;
  }

  ecmcLogBufferLogWarning(
    "Axis[%d]: controller params set but ignored in pure CSP.",
    data.status_.axisId);

  ecmcRtLoggerLogWarning(
    "%s/%s:%d: WARNING: Axis[%d]: controller params set but ignored in pure CSP.\n",
    __FILE__,
    __FUNCTION__,
    __LINE__,
    data.status_.axisId);
}

void warnIfEnabledMonitorLimitIsZero(ecmcAxisData &data,
                                     const char   *monitorName,
                                     const char   *limitName,
                                     double        value) {
  if (value > 0) {
    return;
  }

  ecmcLogBufferLogWarning(
    "Axis[%d]: %s enabled with %s %.6g.",
    data.status_.axisId,
    monitorName,
    limitName,
    value);

  ecmcRtLoggerLogWarning(
    "%s/%s:%d: WARNING: Axis[%d]: %s enabled with %s %.6g.\n",
    __FILE__,
    __FUNCTION__,
    __LINE__,
    data.status_.axisId,
    monitorName,
    limitName,
    value);
}

void warnIfEnabledMonitorLimitsAreZero(ecmcAxisData &data,
                                       ecmcMonitor  *mon) {
  if (!mon) {
    return;
  }

  if (mon->getEnableAtTargetMon()) {
    warnIfEnabledMonitorLimitIsZero(data,
                                    "at-target monitor",
                                    "tolerance",
                                    mon->getAtTargetTol());
  }

  if (mon->getEnableLagMon()) {
    warnIfEnabledMonitorLimitIsZero(data,
                                    "position-lag monitor",
                                    "tolerance",
                                    mon->getPosLagTol());
  }

  if (mon->getEnableMaxVelMon()) {
    warnIfEnabledMonitorLimitIsZero(data,
                                    "max-velocity monitor",
                                    "limit",
                                    mon->getMaxVel());
  }

  if (mon->getEnableVelocityDiffMon()) {
    warnIfEnabledMonitorLimitIsZero(data,
                                    "velocity-difference monitor",
                                    "limit",
                                    mon->getVelDiffMaxDifference());
  }

  if (mon->getEnableCntrlHLMon()) {
    warnIfEnabledMonitorLimitIsZero(data,
                                    "controller-output monitor",
                                    "limit",
                                    mon->getCntrlOutputHL());
  }
}

void warnCsvVelocityOutsideScaledRange(ecmcAxisData &data,
                                       const char   *velocityName,
                                       double        velocity,
                                       double        engMin,
                                       double        engMax,
                                       double        rawMin,
                                       double        rawMax,
                                       double        rawOffset,
                                       double        driveScale) {
  if ((velocity >= engMin) && (velocity <= engMax)) {
    return;
  }

  ecmcLogBufferLogWarning(
    "Axis[%d]: CSV %s %.6g outside scaled range [%.6g, %.6g]; setpoint will saturate "
    "(raw %.0f..%.0f, offset=%.6g, scale=%.6g).",
    data.status_.axisId,
    velocityName,
    velocity,
    engMin,
    engMax,
    rawMin,
    rawMax,
    rawOffset,
    driveScale);

  ecmcRtLoggerLogWarning(
    "%s/%s:%d: WARNING: Axis[%d]: CSV %s %.6g outside scaled range [%.6g, %.6g]; setpoint will saturate "
    "(raw %.0f..%.0f, offset=%.6g, scale=%.6g).\n",
    __FILE__,
    __FUNCTION__,
    __LINE__,
    data.status_.axisId,
    velocityName,
    velocity,
    engMin,
    engMax,
    rawMin,
    rawMax,
    rawOffset,
    driveScale);
}

void warnCsvAbsVelocityExceedsDirectionalLimits(ecmcAxisData &data,
                                                const char   *velocityName,
                                                double        velocity,
                                                double        positiveLimit,
                                                double        negativeLimit,
                                                double        rawMin,
                                                double        rawMax,
                                                double        rawOffset,
                                                double        driveScale) {
  const double absVelocity = std::abs(velocity);

  if ((absVelocity <= positiveLimit) && (absVelocity <= negativeLimit)) {
    return;
  }

  ecmcLogBufferLogWarning(
    "Axis[%d]: CSV %s %.6g exceeds dir limits (+%.6g/-%.6g); setpoint may saturate "
    "(raw %.0f..%.0f, offset=%.6g, scale=%.6g).",
    data.status_.axisId,
    velocityName,
    absVelocity,
    positiveLimit,
    negativeLimit,
    rawMin,
    rawMax,
    rawOffset,
    driveScale);

  ecmcRtLoggerLogWarning(
    "%s/%s:%d: WARNING: Axis[%d]: CSV %s %.6g exceeds dir limits (+%.6g/-%.6g); setpoint may saturate "
    "(raw %.0f..%.0f, offset=%.6g, scale=%.6g).\n",
    __FILE__,
    __FUNCTION__,
    __LINE__,
    data.status_.axisId,
    velocityName,
    absVelocity,
    positiveLimit,
    negativeLimit,
    rawMin,
    rawMax,
    rawOffset,
    driveScale);
}

void warnIfCsvVelocityExceedsRawRange(ecmcAxisData  &data,
                                      ecmcDriveBase *drv,
                                      ecmcMonitor   *mon,
                                      ecmcEncoder  **encoders) {
  if (!drv || !mon || data.control_.drvMode != ECMC_DRV_MODE_CSV ||
      drv->getCsvSetpointUsesFloatingPoint()) {
    return;
  }

  const double driveScale = drv->getScale();
  if (driveScale == 0) {
    return;
  }

  const double rawOffset = drv->getCsvRawVelocityOffset();
  const double rawMin = static_cast<double>(drv->getCsvMinRawVelocitySetpoint());
  const double rawMax = static_cast<double>(drv->getCsvMaxRawVelocitySetpoint());
  const double engLimitA = (rawMin - rawOffset) * driveScale;
  const double engLimitB = (rawMax - rawOffset) * driveScale;
  const double engMin = engLimitA < engLimitB ? engLimitA : engLimitB;
  const double engMax = engLimitA > engLimitB ? engLimitA : engLimitB;
  const double positiveLimit = engMax > 0 ? engMax : 0;
  const double negativeLimit = engMin < 0 ? -engMin : 0;

  warnCsvVelocityOutsideScaledRange(data,
                                    "target velocity",
                                    data.control_.velocityTarget,
                                    engMin,
                                    engMax,
                                    rawMin,
                                    rawMax,
                                    rawOffset,
                                    driveScale);

  if (mon->getEnableMaxVelMon()) {
    warnCsvAbsVelocityExceedsDirectionalLimits(data,
                                               "max velocity",
                                               mon->getMaxVel(),
                                               positiveLimit,
                                               negativeLimit,
                                               rawMin,
                                               rawMax,
                                               rawOffset,
                                               driveScale);
  }

  for (int i = 0; encoders && i < data.status_.encoderCount; ++i) {
    ecmcEncoder *encoder = encoders[i];
    if (!encoder) {
      continue;
    }

    const double homeVelTowardsCam = encoder->getHomeVelTowardsCam();
    if (homeVelTowardsCam != 0) {
      warnCsvAbsVelocityExceedsDirectionalLimits(data,
                                                 "encoder homing velocity towards cam",
                                                 homeVelTowardsCam,
                                                 positiveLimit,
                                                 negativeLimit,
                                                 rawMin,
                                                 rawMax,
                                                 rawOffset,
                                                 driveScale);
    }

    const double homeVelOffCam = encoder->getHomeVelOffCam();
    if (homeVelOffCam != 0) {
      warnCsvAbsVelocityExceedsDirectionalLimits(data,
                                                 "encoder homing velocity off cam",
                                                 homeVelOffCam,
                                                 positiveLimit,
                                                 negativeLimit,
                                                 rawMin,
                                                 rawMax,
                                                 rawOffset,
                                                 driveScale);
    }
  }
}

}  // namespace

ecmcAxisReal::ecmcAxisReal(ecmcAsynPortDriver *asynPortDriver,
                           int                 axisID,
                           double              sampleTime,
                           ecmcDriveTypes      drvType,
                           ecmcTrajTypes       trajType) :
  ecmcAxisBase(asynPortDriver,
               axisID,
               sampleTime,
               trajType) {
  initVars();
  data_.status_.axisId     = axisID;
  data_.status_.axisType   = ECMC_AXIS_TYPE_REAL;
  data_.status_.sampleTime = sampleTime;

  if (getError()) {
    return;
  }

  // Create drive
  try {
    switch (drvType) {
    case ECMC_STEPPER:
      drv_              = new ecmcDriveStepper(asynPortDriver_, data_);
      currentDriveType_ = ECMC_STEPPER;
      break;

    case ECMC_DS402:
      drv_              = new ecmcDriveDS402(asynPortDriver_, data_);
      currentDriveType_ = ECMC_DS402;
      break;

    default:
      ecmcRtLoggerLogError("%s/%s:%d: ERROR: Axis[%d]: Drive type %d is not supported (0x%x).\n",
             __FILE__,
             __FUNCTION__,
             __LINE__,
             data_.status_.axisId,
             drvType,
             ERROR_AXIS_FUNCTION_NOT_SUPPRTED);
      setErrorID(__FILE__,
                 __FUNCTION__,
                 __LINE__,
                 ERROR_AXIS_FUNCTION_NOT_SUPPRTED);
      return;
    }

    // Create PID
    cntrl_ = new ecmcPIDController(asynPortDriver_,
                                   data_,
                                   data_.status_.sampleTime);
  } catch (std::bad_alloc& ex) {
    ecmcRtLoggerLogError("%s/%s:%d: ERROR: Axis[%d]: Memory allocation failed.\n",
           __FILE__,
           __FUNCTION__,
           __LINE__,
           data_.status_.axisId);
    setErrorID(__FILE__, __FUNCTION__, __LINE__, ERROR_MAIN_EXCEPTION);
    return;
  }

  if (!drv_) {
    setErrorID(__FILE__, __FUNCTION__, __LINE__, ERROR_AXIS_DRV_OBJECT_NULL);
    return;
  }

  int errorCode = drv_->getErrorID();
  if (errorCode) {
    setErrorID(__FILE__, __FUNCTION__, __LINE__, errorCode);
    return;
  }

  if (!cntrl_) {
    setErrorID(__FILE__, __FUNCTION__, __LINE__, ERROR_AXIS_CNTRL_OBJECT_NULL);
    return;
  }

  errorCode = cntrl_->getErrorID();
  if (errorCode) {
    setErrorID(__FILE__, __FUNCTION__, __LINE__, errorCode);
    return;
  }

  seq_.setCntrl(cntrl_);
}

ecmcAxisReal::~ecmcAxisReal() {
  delete cntrl_;
  cntrl_ = NULL;
  delete drv_;
  drv_ = NULL;
}

void ecmcAxisReal::initVars() {
  drv_                      = NULL;
  cntrl_                    = NULL;
  currentDriveType_         = ECMC_NO_DRIVE;
}

void ecmcAxisReal::execute(bool masterOK) {
  ecmcAxisBase::preExecute(masterOK);
  const bool axisEnabled = getEnabled();
  const bool axisEnableCmd = getEnable();

  drv_->readEntries(masterOK);

  // Update setpoints and actual 
  seq_.execute();
 
  if (data_.interlocks_.driveSummaryInterlock && !traj_->getBusy()) {
    cntrl_->reset();
  }

  // CSP Write raw actpos  and actpos to drv obj
  //if(data_.control_.cspDrvEncIndex < 0) {
  //
  //  // CSP without control
  //  drv_->setCspActPos(
  //    encArray_[data_.control_.primaryEncIndex]->getRawPosRegister(),
  //    data_.status_.currentPositionActual);  
  //} else {
  //  
  //  // CSP with control
  //  drv_->setCspActPos(
  //    encArray_[data_.control_.cspDrvEncIndex]->getRawPosRegister(),
  //    data_.status_.currentPositionActual);  
  //}
  //
  // Calc position error
  
  data_.status_.cntrlError = ecmcMotionUtils::getPosErrorModWithSign(
      data_.status_.currentPositionSetpoint,
      data_.statusOld_.currentPositionSetpoint,
      data_.status_.currentPositionActual,
      data_.control_.moduloRange);

  if (axisEnabled && masterOK) {
    double cntrOutput = 0;
    mon_->setEnable(true);

    if (data_.control_.drvMode == ECMC_DRV_MODE_CSV) {
      
      // ***************** CSV *****************
      // Controller deadband
      if (!data_.status_.statusWord_.busy && mon_->getCtrlInDeadband()) {
        cntrl_->reset();  // Keep now for legacy reasons...
        cntrOutput = 0;
      } else {
        cntrOutput = cntrl_->control(data_.status_.cntrlError,
                                     data_.status_.currentVelocitySetpoint);
      }
      drv_->setVelSet(cntrOutput);  // Actual control
    } else if (data_.control_.drvMode == ECMC_DRV_MODE_CSP){
      
      // ***************** CSP *****************
      if(data_.control_.cspDrvEncIndex < 0) {
       
        // CSP without controller
        // Just sending setpoint, position loop in driver 
        drv_->setCspPosSet(data_.status_.currentPositionSetpoint);
      } else {        
        // CSP with controller
        if (data_.status_.statusWord_.busy ||  !mon_->getCtrlInDeadband()) {
          data_.status_.currentCSPPositionSetpointOffset = cntrl_->control(data_.status_.cntrlError,0);
        }
        // Actual control. ecmc PID enabled on top of teh psoition loop in the drive (different encoders)        
        drv_->setCspPosSet(data_.status_.currentPositionSetpoint + data_.status_.currentCSPPositionSetpointOffset);
      }
    }
  } else {
    mon_->setEnable(false);

    if (getExecute()) {
      setExecute(false);
    }

    // Only update if enable cmd is low to avoid change of setpoint
    // during between enable and enabled
    if (!axisEnableCmd && !firstEnableDone_ && masterOK &&
        shouldSyncSetpointToActual()) {
      data_.status_.currentPositionSetpoint =
        data_.status_.currentPositionActual;
      traj_->setStartPos(data_.status_.currentPositionSetpoint);
    }

    if (data_.statusOld_.statusWord_.enabled && !data_.status_.statusWord_.enabled &&
        data_.statusOld_.statusWord_.enable && data_.control_.controlWord_.enableCmd) {
      setEnable(false);
      setErrorID(__FILE__,
                 __FUNCTION__,
                 __LINE__,
                 ERROR_AXIS_AMPLIFIER_ENABLED_LOST);
    }

    // CSV
    drv_->setVelSet(0);

    // CSP
    drv_->setCspPosSet(data_.status_.currentPositionActual);
    cntrl_->reset();
  }

  if (!masterOK) {
    if (getEnable()) {
      setEnable(false);
    }
    cntrl_->reset();
    drv_->setVelSet(0);
    if (data_.status_.statusWord_.instartup) {
      setErrorID(ERROR_AXIS_HARDWARE_STATUS_NOT_OK);
    } else {
      setErrorID(__FILE__,
                 __FUNCTION__,
                 __LINE__,
                 ERROR_AXIS_HARDWARE_STATUS_NOT_OK);
    }
  }

  // Write to hardware
  // refreshExternalOutputSources();
  drv_->writeEntries();

  const double drvScale = drv_->getScale();
  if (std::abs(drvScale) > 0) {
    data_.status_.currentvelocityFFRaw = cntrl_->getOutFFPart() * drv_->getInvScale();
  } else {
    data_.status_.currentvelocityFFRaw = 0;
  }

  ecmcAxisBase::postExecute(masterOK);
}

ecmcPIDController * ecmcAxisReal::getCntrl() {
  return cntrl_;
}

ecmcDriveBase * ecmcAxisReal::getDrv() {
  return drv_;
}

int ecmcAxisReal::validate() {
  int error = 0;

  if (data_.control_.primaryEncIndex >= data_.status_.encoderCount) {
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_AXIS_ENC_OBJECT_NULL);
  }

  for (int i = 0; i < data_.status_.encoderCount; i++) {
    if (encArray_[i] == NULL) {
      ecmcRtLoggerLogError("%s/%s:%d: ERROR: Axis[%d]: Encoder[%d] object is NULL (0x%x).\n",
             __FILE__,
             __FUNCTION__,
             __LINE__,
             data_.status_.axisId,
             i,
             ERROR_AXIS_ENC_OBJECT_NULL);

      return setErrorID(__FILE__,
                        __FUNCTION__,
                        __LINE__,
                        ERROR_AXIS_ENC_OBJECT_NULL);
    }

    error = encArray_[i]->validate();

    if (error) {
      ecmcRtLoggerLogError("%s/%s:%d: ERROR: Axis[%d]: Encoder[%d] validation failed (0x%x).\n",
             __FILE__,
             __FUNCTION__,
             __LINE__,
             data_.status_.axisId,
             i,
             error);

      return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
    }
  }

  if (traj_ == NULL) {
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_AXIS_TRAJ_OBJECT_NULL);
  }

  error = traj_->validate();

  if (error) {
    return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
  }

  if (drv_ == NULL) {
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_AXIS_DRV_OBJECT_NULL);
  }

  // Default CSP drive encoder to primary
  drv_->setCspEnc(getCSPEnc());
  error = drv_->validate();
  if (error) {
    return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
  }

  // Set drv ref to sequencer (used for CSP)
  seq_.setDrv(drv_);

  if (mon_ == NULL) {
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_AXIS_MON_OBJECT_NULL);
  }

  error = mon_->validate();

  if (error) {
    return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
  }

  if (cntrl_ == NULL) {
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_AXIS_CNTRL_OBJECT_NULL);
  }

  error = cntrl_->validate();

  if (error) {
    return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
  }

  if (!getRealTimeStarted()) {
    warnIfEnabledMonitorLimitsAreZero(data_, mon_);
    warnIfControllerKpIsZero(data_, cntrl_);
    warnIfControllerParamsIgnoredInPureCsp(data_, cntrl_);
    warnIfCsvControllerOutputsRoundToZero(data_, drv_, cntrl_, mon_);
    warnIfCsvVelocityExceedsRawRange(data_, drv_, mon_, encArray_);
  }

  error = seq_.validate();

  if (error) {
    return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
  }

  error = ecmcAxisBase::validateBase();

  if (error) {
    return setErrorID(__FILE__, __FUNCTION__, __LINE__, error);
  }

  return 0;
}
