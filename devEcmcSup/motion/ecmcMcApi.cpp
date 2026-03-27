/*************************************************************************\
* Copyright (c) 2026 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcMcApi.cpp
*
*  Created on: Mar 27, 2026
*
\*************************************************************************/

#include "ecmcMcApi.h"

#include <new>

#include "ecmcMcRuntime.h"

struct ecmcMcPowerHandle {
  ecmcMcPower fb;
};

struct ecmcMcResetHandle {
  ecmcMcReset fb;
};

struct ecmcMcMoveAbsoluteHandle {
  ecmcMcMoveAbsolute fb;
};

struct ecmcMcMoveRelativeHandle {
  ecmcMcMoveRelative fb;
};

struct ecmcMcHomeHandle {
  ecmcMcHome fb;
};

struct ecmcMcMoveVelocityHandle {
  ecmcMcMoveVelocity fb;
};

struct ecmcMcHaltHandle {
  ecmcMcHalt fb;
};

struct ecmcMcReadStatusHandle {
  ecmcMcReadStatus fb;
};

struct ecmcMcReadActualPositionHandle {
  ecmcMcReadActualPosition fb;
};

struct ecmcMcReadActualVelocityHandle {
  ecmcMcReadActualVelocity fb;
};

namespace {

ecmcMcAxisRef axisRef(int axisIndex) {
  return ecmcMcAxisRef{axisIndex};
}

void fillBaseStatus(const ecmcMcFBBase &fb, ecmcMcBaseStatus *status) {
  if (!status) {
    return;
  }
  status->Busy           = fb.Busy;
  status->Done           = fb.Done;
  status->Error          = fb.Error;
  status->CommandAborted = fb.CommandAborted;
  status->Active         = fb.Active;
  status->ErrorID        = fb.ErrorID;
}

template <typename HandleT>
HandleT *createHandle() {
  return new (std::nothrow) HandleT();
}

template <typename HandleT>
void destroyHandle(HandleT *handle) {
  delete handle;
}

}  // namespace

extern "C" {

ecmcMcPowerHandle *ecmcMcPowerCreate(void) {
  return createHandle<ecmcMcPowerHandle>();
}

void ecmcMcPowerDestroy(ecmcMcPowerHandle *handle) {
  destroyHandle(handle);
}

int ecmcMcPowerRun(ecmcMcPowerHandle *handle,
                   int                axisIndex,
                   int                enable,
                   ecmcMcPowerStatus *status) {
  if (!handle) {
    return -1;
  }
  const int error = handle->fb.run(axisRef(axisIndex), enable != 0);
  if (status) {
    fillBaseStatus(handle->fb, &status->base);
    status->Status = handle->fb.Status;
    status->Valid  = handle->fb.Valid;
  }
  return error;
}

ecmcMcResetHandle *ecmcMcResetCreate(void) {
  return createHandle<ecmcMcResetHandle>();
}

void ecmcMcResetDestroy(ecmcMcResetHandle *handle) {
  destroyHandle(handle);
}

int ecmcMcResetRun(ecmcMcResetHandle *handle,
                   int                axisIndex,
                   int                execute,
                   ecmcMcResetStatus *status) {
  if (!handle) {
    return -1;
  }
  const int error = handle->fb.run(axisRef(axisIndex), execute != 0);
  if (status) {
    fillBaseStatus(handle->fb, &status->base);
  }
  return error;
}

ecmcMcMoveAbsoluteHandle *ecmcMcMoveAbsoluteCreate(void) {
  return createHandle<ecmcMcMoveAbsoluteHandle>();
}

void ecmcMcMoveAbsoluteDestroy(ecmcMcMoveAbsoluteHandle *handle) {
  destroyHandle(handle);
}

int ecmcMcMoveAbsoluteRun(ecmcMcMoveAbsoluteHandle *handle,
                          int                       axisIndex,
                          int                       execute,
                          double                    position,
                          double                    velocity,
                          double                    acceleration,
                          double                    deceleration,
                          ecmcMcMoveAbsoluteStatus *status) {
  if (!handle) {
    return -1;
  }
  const int error = handle->fb.run(axisRef(axisIndex),
                                   execute != 0,
                                   position,
                                   velocity,
                                   acceleration,
                                   deceleration);
  if (status) {
    fillBaseStatus(handle->fb, &status->base);
  }
  return error;
}

ecmcMcMoveRelativeHandle *ecmcMcMoveRelativeCreate(void) {
  return createHandle<ecmcMcMoveRelativeHandle>();
}

void ecmcMcMoveRelativeDestroy(ecmcMcMoveRelativeHandle *handle) {
  destroyHandle(handle);
}

int ecmcMcMoveRelativeRun(ecmcMcMoveRelativeHandle *handle,
                          int                       axisIndex,
                          int                       execute,
                          double                    distance,
                          double                    velocity,
                          double                    acceleration,
                          double                    deceleration,
                          ecmcMcMoveRelativeStatus *status) {
  if (!handle) {
    return -1;
  }
  const int error = handle->fb.run(axisRef(axisIndex),
                                   execute != 0,
                                   distance,
                                   velocity,
                                   acceleration,
                                   deceleration);
  if (status) {
    fillBaseStatus(handle->fb, &status->base);
  }
  return error;
}

ecmcMcHomeHandle *ecmcMcHomeCreate(void) {
  return createHandle<ecmcMcHomeHandle>();
}

void ecmcMcHomeDestroy(ecmcMcHomeHandle *handle) {
  destroyHandle(handle);
}

int ecmcMcHomeRun(ecmcMcHomeHandle *handle,
                  int               axisIndex,
                  int               execute,
                  int               seqId,
                  double            homePosition,
                  double            velocityTowardsCam,
                  double            velocityOffCam,
                  double            acceleration,
                  double            deceleration,
                  ecmcMcHomeStatus *status) {
  if (!handle) {
    return -1;
  }
  const int error = handle->fb.run(axisRef(axisIndex),
                                   execute != 0,
                                   seqId,
                                   homePosition,
                                   velocityTowardsCam,
                                   velocityOffCam,
                                   acceleration,
                                   deceleration);
  if (status) {
    fillBaseStatus(handle->fb, &status->base);
  }
  return error;
}

ecmcMcMoveVelocityHandle *ecmcMcMoveVelocityCreate(void) {
  return createHandle<ecmcMcMoveVelocityHandle>();
}

void ecmcMcMoveVelocityDestroy(ecmcMcMoveVelocityHandle *handle) {
  destroyHandle(handle);
}

int ecmcMcMoveVelocityRun(ecmcMcMoveVelocityHandle *handle,
                          int                       axisIndex,
                          int                       execute,
                          double                    velocity,
                          double                    acceleration,
                          double                    deceleration,
                          ecmcMcMoveVelocityStatus *status) {
  if (!handle) {
    return -1;
  }
  const int error = handle->fb.run(axisRef(axisIndex),
                                   execute != 0,
                                   velocity,
                                   acceleration,
                                   deceleration);
  if (status) {
    fillBaseStatus(handle->fb, &status->base);
    status->InVelocity = handle->fb.InVelocity;
  }
  return error;
}

ecmcMcHaltHandle *ecmcMcHaltCreate(void) {
  return createHandle<ecmcMcHaltHandle>();
}

void ecmcMcHaltDestroy(ecmcMcHaltHandle *handle) {
  destroyHandle(handle);
}

int ecmcMcHaltRun(ecmcMcHaltHandle *handle,
                  int               axisIndex,
                  int               execute,
                  ecmcMcHaltStatus *status) {
  if (!handle) {
    return -1;
  }
  const int error = handle->fb.run(axisRef(axisIndex), execute != 0);
  if (status) {
    fillBaseStatus(handle->fb, &status->base);
  }
  return error;
}

ecmcMcReadStatusHandle *ecmcMcReadStatusCreate(void) {
  return createHandle<ecmcMcReadStatusHandle>();
}

void ecmcMcReadStatusDestroy(ecmcMcReadStatusHandle *handle) {
  destroyHandle(handle);
}

int ecmcMcReadStatusRun(ecmcMcReadStatusHandle *handle,
                        int                     axisIndex,
                        int                     enable,
                        ecmcMcReadStatusStatus *status) {
  if (!handle) {
    return -1;
  }
  const int error = handle->fb.run(axisRef(axisIndex), enable != 0);
  if (status) {
    fillBaseStatus(handle->fb, &status->base);
    status->Valid              = handle->fb.Valid;
    status->ErrorStop          = handle->fb.ErrorStop;
    status->Disabled           = handle->fb.Disabled;
    status->Stopping           = handle->fb.Stopping;
    status->Homing             = handle->fb.Homing;
    status->StandStill         = handle->fb.StandStill;
    status->DiscreteMotion     = handle->fb.DiscreteMotion;
    status->ContinuousMotion   = handle->fb.ContinuousMotion;
    status->SynchronizedMotion = handle->fb.SynchronizedMotion;
  }
  return error;
}

ecmcMcReadActualPositionHandle *ecmcMcReadActualPositionCreate(void) {
  return createHandle<ecmcMcReadActualPositionHandle>();
}

void ecmcMcReadActualPositionDestroy(ecmcMcReadActualPositionHandle *handle) {
  destroyHandle(handle);
}

int ecmcMcReadActualPositionRun(ecmcMcReadActualPositionHandle *handle,
                                int                             axisIndex,
                                int                             enable,
                                ecmcMcReadActualPositionStatus *status) {
  if (!handle) {
    return -1;
  }
  const int error = handle->fb.run(axisRef(axisIndex), enable != 0);
  if (status) {
    fillBaseStatus(handle->fb, &status->base);
    status->Valid    = handle->fb.Valid;
    status->Position = handle->fb.Position;
  }
  return error;
}

ecmcMcReadActualVelocityHandle *ecmcMcReadActualVelocityCreate(void) {
  return createHandle<ecmcMcReadActualVelocityHandle>();
}

void ecmcMcReadActualVelocityDestroy(ecmcMcReadActualVelocityHandle *handle) {
  destroyHandle(handle);
}

int ecmcMcReadActualVelocityRun(ecmcMcReadActualVelocityHandle *handle,
                                int                             axisIndex,
                                int                             enable,
                                ecmcMcReadActualVelocityStatus *status) {
  if (!handle) {
    return -1;
  }
  const int error = handle->fb.run(axisRef(axisIndex), enable != 0);
  if (status) {
    fillBaseStatus(handle->fb, &status->base);
    status->Valid    = handle->fb.Valid;
    status->Velocity = handle->fb.Velocity;
  }
  return error;
}

}  // extern "C"
