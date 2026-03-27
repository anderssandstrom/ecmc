/*************************************************************************\
* Copyright (c) 2026 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcMcApi.h
*
*  Created on: Mar 27, 2026
*
\*************************************************************************/

/**
\file
    @brief C API for PLCopen-style motion function block runtime
*/

#ifndef ECMC_MC_API_H_
#define ECMC_MC_API_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t  Busy;
  uint8_t  Done;
  uint8_t  Error;
  uint8_t  CommandAborted;
  uint8_t  Active;
  uint32_t ErrorID;
} ecmcMcBaseStatus;

typedef struct {
  ecmcMcBaseStatus base;
  uint8_t          Status;
  uint8_t          Valid;
} ecmcMcPowerStatus;

typedef struct {
  ecmcMcBaseStatus base;
} ecmcMcResetStatus;

typedef struct {
  ecmcMcBaseStatus base;
} ecmcMcMoveAbsoluteStatus;

typedef struct {
  ecmcMcBaseStatus base;
} ecmcMcMoveRelativeStatus;

typedef struct {
  ecmcMcBaseStatus base;
} ecmcMcHomeStatus;

typedef struct {
  ecmcMcBaseStatus base;
  uint8_t          InVelocity;
} ecmcMcMoveVelocityStatus;

typedef struct {
  ecmcMcBaseStatus base;
} ecmcMcHaltStatus;

typedef struct {
  ecmcMcBaseStatus base;
  uint8_t          Valid;
  uint8_t          ErrorStop;
  uint8_t          Disabled;
  uint8_t          Stopping;
  uint8_t          Homing;
  uint8_t          StandStill;
  uint8_t          DiscreteMotion;
  uint8_t          ContinuousMotion;
  uint8_t          SynchronizedMotion;
} ecmcMcReadStatusStatus;

typedef struct {
  ecmcMcBaseStatus base;
  uint8_t          Valid;
  double           Position;
} ecmcMcReadActualPositionStatus;

typedef struct {
  ecmcMcBaseStatus base;
  uint8_t          Valid;
  double           Velocity;
} ecmcMcReadActualVelocityStatus;

typedef struct ecmcMcPowerHandle ecmcMcPowerHandle;
typedef struct ecmcMcResetHandle ecmcMcResetHandle;
typedef struct ecmcMcMoveAbsoluteHandle ecmcMcMoveAbsoluteHandle;
typedef struct ecmcMcMoveRelativeHandle ecmcMcMoveRelativeHandle;
typedef struct ecmcMcHomeHandle ecmcMcHomeHandle;
typedef struct ecmcMcMoveVelocityHandle ecmcMcMoveVelocityHandle;
typedef struct ecmcMcHaltHandle ecmcMcHaltHandle;
typedef struct ecmcMcReadStatusHandle ecmcMcReadStatusHandle;
typedef struct ecmcMcReadActualPositionHandle ecmcMcReadActualPositionHandle;
typedef struct ecmcMcReadActualVelocityHandle ecmcMcReadActualVelocityHandle;

ecmcMcPowerHandle *ecmcMcPowerCreate(void);
void ecmcMcPowerDestroy(ecmcMcPowerHandle *handle);
int ecmcMcPowerRun(ecmcMcPowerHandle       *handle,
                   int                      axisIndex,
                   int                      enable,
                   ecmcMcPowerStatus       *status);

ecmcMcResetHandle *ecmcMcResetCreate(void);
void ecmcMcResetDestroy(ecmcMcResetHandle *handle);
int ecmcMcResetRun(ecmcMcResetHandle       *handle,
                   int                      axisIndex,
                   int                      execute,
                   ecmcMcResetStatus       *status);

ecmcMcMoveAbsoluteHandle *ecmcMcMoveAbsoluteCreate(void);
void ecmcMcMoveAbsoluteDestroy(ecmcMcMoveAbsoluteHandle *handle);
int ecmcMcMoveAbsoluteRun(ecmcMcMoveAbsoluteHandle      *handle,
                          int                            axisIndex,
                          int                            execute,
                          double                         position,
                          double                         velocity,
                          double                         acceleration,
                          double                         deceleration,
                          ecmcMcMoveAbsoluteStatus      *status);

ecmcMcMoveRelativeHandle *ecmcMcMoveRelativeCreate(void);
void ecmcMcMoveRelativeDestroy(ecmcMcMoveRelativeHandle *handle);
int ecmcMcMoveRelativeRun(ecmcMcMoveRelativeHandle      *handle,
                          int                            axisIndex,
                          int                            execute,
                          double                         distance,
                          double                         velocity,
                          double                         acceleration,
                          double                         deceleration,
                          ecmcMcMoveRelativeStatus      *status);

ecmcMcHomeHandle *ecmcMcHomeCreate(void);
void ecmcMcHomeDestroy(ecmcMcHomeHandle *handle);
int ecmcMcHomeRun(ecmcMcHomeHandle       *handle,
                  int                     axisIndex,
                  int                     execute,
                  int                     seqId,
                  double                  homePosition,
                  double                  velocityTowardsCam,
                  double                  velocityOffCam,
                  double                  acceleration,
                  double                  deceleration,
                  ecmcMcHomeStatus       *status);

ecmcMcMoveVelocityHandle *ecmcMcMoveVelocityCreate(void);
void ecmcMcMoveVelocityDestroy(ecmcMcMoveVelocityHandle *handle);
int ecmcMcMoveVelocityRun(ecmcMcMoveVelocityHandle      *handle,
                          int                            axisIndex,
                          int                            execute,
                          double                         velocity,
                          double                         acceleration,
                          double                         deceleration,
                          ecmcMcMoveVelocityStatus      *status);

ecmcMcHaltHandle *ecmcMcHaltCreate(void);
void ecmcMcHaltDestroy(ecmcMcHaltHandle *handle);
int ecmcMcHaltRun(ecmcMcHaltHandle       *handle,
                  int                     axisIndex,
                  int                     execute,
                  ecmcMcHaltStatus       *status);

ecmcMcReadStatusHandle *ecmcMcReadStatusCreate(void);
void ecmcMcReadStatusDestroy(ecmcMcReadStatusHandle *handle);
int ecmcMcReadStatusRun(ecmcMcReadStatusHandle      *handle,
                        int                          axisIndex,
                        int                          enable,
                        ecmcMcReadStatusStatus      *status);

ecmcMcReadActualPositionHandle *ecmcMcReadActualPositionCreate(void);
void ecmcMcReadActualPositionDestroy(ecmcMcReadActualPositionHandle *handle);
int ecmcMcReadActualPositionRun(ecmcMcReadActualPositionHandle      *handle,
                                int                                  axisIndex,
                                int                                  enable,
                                ecmcMcReadActualPositionStatus      *status);

ecmcMcReadActualVelocityHandle *ecmcMcReadActualVelocityCreate(void);
void ecmcMcReadActualVelocityDestroy(ecmcMcReadActualVelocityHandle *handle);
int ecmcMcReadActualVelocityRun(ecmcMcReadActualVelocityHandle      *handle,
                                int                                  axisIndex,
                                int                                  enable,
                                ecmcMcReadActualVelocityStatus      *status);

#ifdef __cplusplus
}
#endif

#endif  // ECMC_MC_API_H_
