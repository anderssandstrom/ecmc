/*************************************************************************\
* Copyright (c) 2026 Paul Scherrer Institute
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcCppUtils.hpp
*
\*************************************************************************/

#pragma once

#include "ecmcCppLogic.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ecmcCpp {

inline double applyDeadband(double value, double width = 0.0, double center = 0.0) {
  const double delta = value - center;
  if (width <= 0.0) {
    return value;
  }
  if (std::abs(delta) <= width) {
    return center;
  }
  return (delta > 0.0) ? center + (delta - width) : center + (delta + width);
}

template <typename T>
inline T clampValue(T value, T min_value, T max_value) {
  if (max_value < min_value) {
    std::swap(min_value, max_value);
  }
  if (max_value > min_value) {
    return std::clamp(value, min_value, max_value);
  }
  return value;
}

inline bool inWindow(double value, double low, double high, bool inclusive = true) {
  if (high < low) {
    std::swap(low, high);
  }
  return inclusive ? ((value >= low) && (value <= high)) : ((value > low) && (value < high));
}

class RTrig {
 public:
  bool Clk {false};
  bool Q {false};

  void run() {
    Q = Clk && !PrevClk_;
    PrevClk_ = Clk;
  }

 private:
  bool PrevClk_ {false};
};

class FTrig {
 public:
  bool Clk {false};
  bool Q {false};

  void run() {
    Q = !Clk && PrevClk_;
    PrevClk_ = Clk;
  }

 private:
  bool PrevClk_ {false};
};

class Ton {
 public:
  bool In {false};
  double PT {0.0};
  bool Q {false};
  double ET {0.0};

  void run(double dt_s = getCycleTimeS()) {
    if (!In) {
      ET = 0.0;
      Q = false;
      return;
    }

    if (PT <= 0.0) {
      ET = 0.0;
      Q = true;
      return;
    }

    ET = std::min(ET + std::max(dt_s, 0.0), PT);
    Q = (ET >= PT);
  }
};

class Tof {
 public:
  bool In {false};
  double PT {0.0};
  bool Q {true};
  double ET {0.0};

  void run(double dt_s = getCycleTimeS()) {
    if (In) {
      ET = 0.0;
      Q = true;
      return;
    }

    if (PT <= 0.0) {
      ET = 0.0;
      Q = false;
      return;
    }

    ET = std::min(ET + std::max(dt_s, 0.0), PT);
    Q = (ET < PT);
  }
};

class Tp {
 public:
  bool In {false};
  double PT {0.0};
  bool Q {false};
  double ET {0.0};

  void run(double dt_s = getCycleTimeS()) {
    const bool rising = In && !PrevIn_;

    if (rising) {
      Active_ = true;
      ET = 0.0;
    }

    if (!Active_) {
      Q = false;
      PrevIn_ = In;
      return;
    }

    if (PT <= 0.0) {
      Q = true;
      Active_ = false;
      PrevIn_ = In;
      return;
    }

    Q = true;
    ET = std::min(ET + std::max(dt_s, 0.0), PT);
    if (ET >= PT) {
      Q = false;
      Active_ = false;
    }

    PrevIn_ = In;
  }

 private:
  bool PrevIn_ {false};
  bool Active_ {false};
};

class DebounceBool {
 public:
  bool In {false};
  double OnDelayS {0.0};
  double OffDelayS {0.0};

  bool Out {false};
  bool Rising {false};
  bool Falling {false};

  void run(double dt_s = getCycleTimeS()) {
    Rising = false;
    Falling = false;

    if (In) {
      OnElapsed_ += std::max(dt_s, 0.0);
      OffElapsed_ = 0.0;
      if ((OnDelayS <= 0.0) || (OnElapsed_ >= OnDelayS)) {
        if (!Out) {
          Rising = true;
        }
        Out = true;
      }
    } else {
      OffElapsed_ += std::max(dt_s, 0.0);
      OnElapsed_ = 0.0;
      if ((OffDelayS <= 0.0) || (OffElapsed_ >= OffDelayS)) {
        if (Out) {
          Falling = true;
        }
        Out = false;
      }
    }
  }

 private:
  double OnElapsed_ {0.0};
  double OffElapsed_ {0.0};
};

class StartupDelay {
 public:
  bool Enable {true};
  double DelayS {0.5};

  bool Ready {false};
  bool Rising {false};

  void run(double dt_s = getCycleTimeS()) {
    const bool previous_ready = Ready;
    if (!Enable) {
      Elapsed_ = 0.0;
      Ready = false;
    } else if (DelayS <= 0.0) {
      Ready = true;
    } else {
      Elapsed_ += std::max(dt_s, 0.0);
      Ready = (Elapsed_ >= DelayS);
    }
    Rising = Ready && !previous_ready;
  }

 private:
  double Elapsed_ {0.0};
};

class RateLimiter {
 public:
  bool Enable {true};
  bool Reset {false};
  double Input {0.0};
  double RisingRate {0.0};
  double FallingRate {0.0};
  double DT {0.0};
  bool InitToInput {true};

  double Output {0.0};
  bool Limited {false};

  void run() {
    if (Reset || !Enable) {
      if (InitToInput) {
        Output = Input;
      }
      Limited = false;
      return;
    }

    const double dt = resolveDt();
    const double delta = Input - Output;
    Limited = false;

    if (dt <= 0.0) {
      Output = Input;
      return;
    }

    if (delta > 0.0) {
      if (RisingRate <= 0.0) {
        Output = Input;
      } else {
        const double max_step = RisingRate * dt;
        if (delta > max_step) {
          Output += max_step;
          Limited = true;
        } else {
          Output = Input;
        }
      }
    } else if (delta < 0.0) {
      if (FallingRate <= 0.0) {
        Output = Input;
      } else {
        const double max_step = FallingRate * dt;
        if (-delta > max_step) {
          Output -= max_step;
          Limited = true;
        } else {
          Output = Input;
        }
      }
    }
  }

 private:
  double resolveDt() const {
    return (DT > 0.0) ? DT : getCycleTimeS();
  }
};

class FirstOrderFilter {
 public:
  bool Enable {true};
  bool Reset {false};
  double Input {0.0};
  double Tau {0.0};
  double DT {0.0};
  bool InitToInput {true};

  double Output {0.0};

  void run() {
    const double dt = resolveDt();
    if (Reset || !Enable) {
      if (InitToInput) {
        Output = Input;
      }
      return;
    }
    if ((Tau <= 0.0) || (dt <= 0.0)) {
      Output = Input;
      return;
    }
    const double alpha = dt / (Tau + dt);
    Output = Output + alpha * (Input - Output);
  }

 private:
  double resolveDt() const {
    return (DT > 0.0) ? DT : getCycleTimeS();
  }
};

class HysteresisBool {
 public:
  double In {0.0};
  double Low {0.0};
  double High {0.0};
  bool Reset {false};
  bool InitState {false};

  bool Out {false};
  bool Rising {false};
  bool Falling {false};

  void run() {
    double lo = Low;
    double hi = High;
    if (hi < lo) {
      std::swap(lo, hi);
    }

    Rising = false;
    Falling = false;

    if (Reset) {
      if (Out != InitState) {
        Rising = InitState;
        Falling = !InitState;
      }
      Out = InitState;
      return;
    }

    if (!Out) {
      if (In >= hi) {
        Out = true;
        Rising = true;
      }
    } else if (In <= lo) {
      Out = false;
      Falling = true;
    }
  }
};

class Integrator {
 public:
  bool Enable {true};
  bool Reset {false};
  bool Hold {false};
  double In {0.0};
  double K {1.0};
  double DT {0.0};
  double Min {0.0};
  double Max {0.0};
  double Init {0.0};

  double Out {0.0};
  bool Limited {false};

  void run() {
    double lo = Min;
    double hi = Max;
    if (hi < lo) {
      std::swap(lo, hi);
    }

    if (Reset) {
      Out = Init;
      Limited = false;
      return;
    }

    const double dt = resolveDt();
    if (!(Enable && !Hold && (dt > 0.0))) {
      Limited = false;
      return;
    }

    double candidate = Out + In * K * dt;
    Limited = false;
    if (hi > lo) {
      if (candidate < lo) {
        candidate = lo;
        Limited = true;
      } else if (candidate > hi) {
        candidate = hi;
        Limited = true;
      }
    }
    Out = candidate;
  }

 private:
  double resolveDt() const {
    return (DT > 0.0) ? DT : getCycleTimeS();
  }
};

class EcSlaveStatus {
 public:
  int32_t SlaveId {0};
  int32_t MasterId {-1};

  bool Valid {false};
  bool Online {false};
  bool Operational {false};
  bool Init {false};
  bool PreOp {false};
  bool SafeOp {false};
  bool Op {false};
  uint8_t StateCode {0};
  uint32_t StateWord {0};

  void run() {
    StateWord = getEcSlaveStateWord(SlaveId, MasterId);
    StateCode = static_cast<uint8_t>((StateWord >> 3u) & 0xFu);
    Valid = (StateWord & (1u << 0u)) != 0u;
    Online = (StateWord & (1u << 1u)) != 0u;
    Operational = (StateWord & (1u << 2u)) != 0u;
    Init = (StateCode == 1u);
    PreOp = (StateCode == 2u);
    SafeOp = (StateCode == 4u);
    Op = (StateCode == 8u);
  }
};

class EcMasterStatus {
 public:
  int32_t MasterId {-1};

  bool Valid {false};
  bool LinkUp {false};
  bool Init {false};
  bool PreOp {false};
  bool SafeOp {false};
  bool Op {false};
  uint8_t StateCode {0};
  uint16_t SlavesResponding {0};
  uint32_t StateWord {0};

  void run() {
    StateWord = getEcMasterStateWord(MasterId);
    StateCode = static_cast<uint8_t>((StateWord >> 2u) & 0xFu);
    Valid = (StateWord & (1u << 0u)) != 0u;
    LinkUp = (StateWord & (1u << 1u)) != 0u;
    Init = (StateCode == 1u);
    PreOp = (StateCode == 2u);
    SafeOp = (StateCode == 4u);
    Op = (StateCode == 8u);
    SlavesResponding = static_cast<uint16_t>((StateWord >> 16u) & 0xFFFFu);
  }
};

}  // namespace ecmcCpp
