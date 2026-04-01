/*************************************************************************\
* Copyright (c) 2026 Paul Scherrer Institute
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcNativeMotion.hpp
*
\*************************************************************************/

#pragma once

#include "../motion/ecmcMcApi.h"

#include <cstdint>
#include <utility>

namespace ecmcNative {

struct McAxisRef {
  int axis_index {-1};
};

class McPower {
 public:
  McPower() : handle_(ecmcMcPowerCreate()) {}
  ~McPower() {
    if (handle_) {
      ecmcMcPowerDestroy(handle_);
    }
  }

  McPower(const McPower&) = delete;
  McPower& operator=(const McPower&) = delete;

  int run(McAxisRef axis, bool enable) {
    ecmcMcPowerStatus status {};
    const int error = ecmcMcPowerRun(handle_, axis.axis_index, enable ? 1 : 0, &status);
    updateBase(status.base);
    Status = status.Status != 0;
    Valid = status.Valid != 0;
    return error;
  }

  bool Busy {false};
  bool Done {false};
  bool Error {false};
  bool CommandAborted {false};
  bool Active {false};
  uint32_t ErrorID {0};
  bool Status {false};
  bool Valid {false};

 private:
  void updateBase(const ecmcMcBaseStatus& base) {
    Busy = base.Busy != 0;
    Done = base.Done != 0;
    Error = base.Error != 0;
    CommandAborted = base.CommandAborted != 0;
    Active = base.Active != 0;
    ErrorID = base.ErrorID;
  }

  ecmcMcPowerHandle* handle_ {nullptr};
};

class McMoveAbsolute {
 public:
  McMoveAbsolute() : handle_(ecmcMcMoveAbsoluteCreate()) {}
  ~McMoveAbsolute() {
    if (handle_) {
      ecmcMcMoveAbsoluteDestroy(handle_);
    }
  }

  McMoveAbsolute(const McMoveAbsolute&) = delete;
  McMoveAbsolute& operator=(const McMoveAbsolute&) = delete;

  int run(McAxisRef axis,
          bool execute,
          double position,
          double velocity,
          double acceleration,
          double deceleration) {
    ecmcMcMoveAbsoluteStatus status {};
    const int error = ecmcMcMoveAbsoluteRun(handle_,
                                            axis.axis_index,
                                            execute ? 1 : 0,
                                            position,
                                            velocity,
                                            acceleration,
                                            deceleration,
                                            &status);
    updateBase(status.base);
    return error;
  }

  bool Busy {false};
  bool Done {false};
  bool Error {false};
  bool CommandAborted {false};
  bool Active {false};
  uint32_t ErrorID {0};

 private:
  void updateBase(const ecmcMcBaseStatus& base) {
    Busy = base.Busy != 0;
    Done = base.Done != 0;
    Error = base.Error != 0;
    CommandAborted = base.CommandAborted != 0;
    Active = base.Active != 0;
    ErrorID = base.ErrorID;
  }

  ecmcMcMoveAbsoluteHandle* handle_ {nullptr};
};

class McMoveVelocity {
 public:
  McMoveVelocity() : handle_(ecmcMcMoveVelocityCreate()) {}
  ~McMoveVelocity() {
    if (handle_) {
      ecmcMcMoveVelocityDestroy(handle_);
    }
  }

  McMoveVelocity(const McMoveVelocity&) = delete;
  McMoveVelocity& operator=(const McMoveVelocity&) = delete;

  int run(McAxisRef axis,
          bool execute,
          double velocity,
          double acceleration,
          double deceleration) {
    ecmcMcMoveVelocityStatus status {};
    const int error = ecmcMcMoveVelocityRun(handle_,
                                            axis.axis_index,
                                            execute ? 1 : 0,
                                            velocity,
                                            acceleration,
                                            deceleration,
                                            &status);
    updateBase(status.base);
    InVelocity = status.InVelocity != 0;
    return error;
  }

  bool Busy {false};
  bool Done {false};
  bool Error {false};
  bool CommandAborted {false};
  bool Active {false};
  uint32_t ErrorID {0};
  bool InVelocity {false};

 private:
  void updateBase(const ecmcMcBaseStatus& base) {
    Busy = base.Busy != 0;
    Done = base.Done != 0;
    Error = base.Error != 0;
    CommandAborted = base.CommandAborted != 0;
    Active = base.Active != 0;
    ErrorID = base.ErrorID;
  }

  ecmcMcMoveVelocityHandle* handle_ {nullptr};
};

class McReadStatus {
 public:
  McReadStatus() : handle_(ecmcMcReadStatusCreate()) {}
  ~McReadStatus() {
    if (handle_) {
      ecmcMcReadStatusDestroy(handle_);
    }
  }

  McReadStatus(const McReadStatus&) = delete;
  McReadStatus& operator=(const McReadStatus&) = delete;

  int run(McAxisRef axis, bool enable) {
    ecmcMcReadStatusStatus status {};
    const int error = ecmcMcReadStatusRun(handle_, axis.axis_index, enable ? 1 : 0, &status);
    updateBase(status.base);
    Valid = status.Valid != 0;
    ErrorStop = status.ErrorStop != 0;
    Disabled = status.Disabled != 0;
    Stopping = status.Stopping != 0;
    Homing = status.Homing != 0;
    StandStill = status.StandStill != 0;
    DiscreteMotion = status.DiscreteMotion != 0;
    ContinuousMotion = status.ContinuousMotion != 0;
    SynchronizedMotion = status.SynchronizedMotion != 0;
    return error;
  }

  bool Busy {false};
  bool Done {false};
  bool Error {false};
  bool CommandAborted {false};
  bool Active {false};
  uint32_t ErrorID {0};
  bool Valid {false};
  bool ErrorStop {false};
  bool Disabled {false};
  bool Stopping {false};
  bool Homing {false};
  bool StandStill {false};
  bool DiscreteMotion {false};
  bool ContinuousMotion {false};
  bool SynchronizedMotion {false};

 private:
  void updateBase(const ecmcMcBaseStatus& base) {
    Busy = base.Busy != 0;
    Done = base.Done != 0;
    Error = base.Error != 0;
    CommandAborted = base.CommandAborted != 0;
    Active = base.Active != 0;
    ErrorID = base.ErrorID;
  }

  ecmcMcReadStatusHandle* handle_ {nullptr};
};

class McReadActualPosition {
 public:
  McReadActualPosition() : handle_(ecmcMcReadActualPositionCreate()) {}
  ~McReadActualPosition() {
    if (handle_) {
      ecmcMcReadActualPositionDestroy(handle_);
    }
  }

  McReadActualPosition(const McReadActualPosition&) = delete;
  McReadActualPosition& operator=(const McReadActualPosition&) = delete;

  int run(McAxisRef axis, bool enable) {
    ecmcMcReadActualPositionStatus status {};
    const int error =
      ecmcMcReadActualPositionRun(handle_, axis.axis_index, enable ? 1 : 0, &status);
    updateBase(status.base);
    Valid = status.Valid != 0;
    Position = status.Position;
    return error;
  }

  bool Busy {false};
  bool Done {false};
  bool Error {false};
  bool CommandAborted {false};
  bool Active {false};
  uint32_t ErrorID {0};
  bool Valid {false};
  double Position {0.0};

 private:
  void updateBase(const ecmcMcBaseStatus& base) {
    Busy = base.Busy != 0;
    Done = base.Done != 0;
    Error = base.Error != 0;
    CommandAborted = base.CommandAborted != 0;
    Active = base.Active != 0;
    ErrorID = base.ErrorID;
  }

  ecmcMcReadActualPositionHandle* handle_ {nullptr};
};

}  // namespace ecmcNative
