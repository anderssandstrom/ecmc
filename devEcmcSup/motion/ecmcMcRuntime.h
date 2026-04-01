/*************************************************************************\
* Copyright (c) 2026 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcMcRuntime.h
*
*  Created on: Mar 27, 2026
*
\*************************************************************************/

/**
\file
    @brief PLCopen-style motion function block runtime scaffolding
*/

#ifndef ECMC_MC_RUNTIME_H_
#define ECMC_MC_RUNTIME_H_

#include <stdint.h>

struct ecmcMcAxisRef {
  int axisIndex = -1;
};

class ecmcMcFBBase {
public:
  virtual ~ecmcMcFBBase() = default;

  bool     Busy           = false;
  bool     Done           = false;
  bool     Error          = false;
  bool     CommandAborted = false;
  bool     Active         = false;
  uint32_t ErrorID        = 0;

protected:
  bool executeOld_ = false;

  void beginCycle();
  bool risingEdge(bool execute);
  void setError(uint32_t errorId);
  void clearError();
};

class ecmcMcPower : public ecmcMcFBBase {
public:
  bool Status = false;
  bool Valid  = false;

  int run(ecmcMcAxisRef axis, bool enable);
};

class ecmcMcReset : public ecmcMcFBBase {
public:
  int run(ecmcMcAxisRef axis, bool execute);

private:
  bool issuedReset_ = false;
};

class ecmcMcMoveAbsolute : public ecmcMcFBBase {
public:
  int run(ecmcMcAxisRef axis,
          bool          execute,
          double        position,
          double        velocity,
          double        acceleration,
          double        deceleration);

private:
  bool issuedCommand_       = false;
  bool awaitingStandstill_  = false;
};

class ecmcMcMoveRelative : public ecmcMcFBBase {
public:
  int run(ecmcMcAxisRef axis,
          bool          execute,
          double        distance,
          double        velocity,
          double        acceleration,
          double        deceleration);

private:
  bool issuedCommand_       = false;
  bool awaitingStandstill_  = false;
};

class ecmcMcHome : public ecmcMcFBBase {
public:
  int run(ecmcMcAxisRef axis,
          bool          execute,
          int           seqId,
          double        homePosition,
          double        velocityTowardsCam,
          double        velocityOffCam,
          double        acceleration,
          double        deceleration);

private:
  bool issuedCommand_ = false;
};

class ecmcMcMoveVelocity : public ecmcMcFBBase {
public:
  bool InVelocity = false;

  int run(ecmcMcAxisRef axis,
          bool          execute,
          double        velocity,
          double        acceleration,
          double        deceleration);

private:
  bool issuedCommand_ = false;
};

class ecmcMcHalt : public ecmcMcFBBase {
public:
  int run(ecmcMcAxisRef axis, bool execute);

private:
  bool haltIssued_ = false;
};

class ecmcMcReadStatus : public ecmcMcFBBase {
public:
  bool Valid              = false;
  bool ErrorStop          = false;
  bool Disabled           = false;
  bool Stopping           = false;
  bool Homing             = false;
  bool StandStill         = false;
  bool DiscreteMotion     = false;
  bool ContinuousMotion   = false;
  bool SynchronizedMotion = false;

  int run(ecmcMcAxisRef axis, bool enable);
};

class ecmcMcReadActualPosition : public ecmcMcFBBase {
public:
  bool   Valid    = false;
  double Position = 0.0;

  int run(ecmcMcAxisRef axis, bool enable);
};

class ecmcMcReadActualVelocity : public ecmcMcFBBase {
public:
  bool   Valid    = false;
  double Velocity = 0.0;

  int run(ecmcMcAxisRef axis, bool enable);
};

#endif  // ECMC_MC_RUNTIME_H_
