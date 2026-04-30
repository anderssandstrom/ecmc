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
#include <limits>
#include <type_traits>
#include <vector>

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

template <typename T,
          std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
constexpr bool readBit(T value, unsigned bit_index) {
  using UnsignedT = std::make_unsigned_t<T>;
  if (bit_index >= std::numeric_limits<UnsignedT>::digits) {
    return false;
  }
  return (static_cast<UnsignedT>(value) & (UnsignedT {1u} << bit_index)) != 0u;
}

template <typename T,
          std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
constexpr T writeBit(T value, unsigned bit_index, bool bit_value) {
  using UnsignedT = std::make_unsigned_t<T>;
  if (bit_index >= std::numeric_limits<UnsignedT>::digits) {
    return value;
  }

  const UnsignedT mask = UnsignedT {1u} << bit_index;
  UnsignedT word = static_cast<UnsignedT>(value);
  word = bit_value ? (word | mask) : (word & ~mask);
  return static_cast<T>(word);
}

template <typename T,
          std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
constexpr T setBit(T value, unsigned bit_index) {
  return writeBit(value, bit_index, true);
}

template <typename T,
          std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
constexpr T clearBit(T value, unsigned bit_index) {
  return writeBit(value, bit_index, false);
}

template <typename T,
          std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
constexpr T toggleBit(T value, unsigned bit_index) {
  using UnsignedT = std::make_unsigned_t<T>;
  if (bit_index >= std::numeric_limits<UnsignedT>::digits) {
    return value;
  }
  return static_cast<T>(static_cast<UnsignedT>(value) ^ (UnsignedT {1u} << bit_index));
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

class Sr {
 public:
  bool S {false};
  bool R {false};
  bool Q {false};

  void run() {
    if (R) {
      Q = false;
    } else if (S) {
      Q = true;
    }
  }
};

class Rs {
 public:
  bool S {false};
  bool R {false};
  bool Q {false};

  void run() {
    if (S) {
      Q = true;
    } else if (R) {
      Q = false;
    }
  }
};

class FlipFlop {
 public:
  bool Toggle {false};
  bool Reset {false};
  bool Q {false};

  void run() {
    if (Reset) {
      Q = false;
    } else if (Toggle && !PrevToggle_) {
      Q = !Q;
    }
    PrevToggle_ = Toggle;
  }

 private:
  bool PrevToggle_ {false};
};

class Blink {
 public:
  bool Enable {false};
  double OnTimeS {0.5};
  double OffTimeS {0.5};
  bool StartHigh {true};

  bool Q {false};
  bool Rising {false};
  bool Falling {false};

  void run(double dt_s = getCycleTimeS()) {
    Rising = false;
    Falling = false;

    if (!Enable) {
      setOutput(false);
      ActivePhaseHigh_ = StartHigh;
      PhaseElapsedS_ = 0.0;
      WasEnabled_ = false;
      return;
    }

    if (!WasEnabled_) {
      setOutput(StartHigh);
      ActivePhaseHigh_ = StartHigh;
      PhaseElapsedS_ = 0.0;
      WasEnabled_ = true;
      return;
    }

    const double phase_time = ActivePhaseHigh_ ? OnTimeS : OffTimeS;
    if (phase_time <= 0.0) {
      ActivePhaseHigh_ = !ActivePhaseHigh_;
      setOutput(ActivePhaseHigh_);
      PhaseElapsedS_ = 0.0;
      return;
    }

    PhaseElapsedS_ += std::max(dt_s, 0.0);
    if (PhaseElapsedS_ >= phase_time) {
      PhaseElapsedS_ = 0.0;
      ActivePhaseHigh_ = !ActivePhaseHigh_;
      setOutput(ActivePhaseHigh_);
    }
  }

 private:
  bool ActivePhaseHigh_ {true};
  bool WasEnabled_ {false};
  double PhaseElapsedS_ {0.0};

  void setOutput(bool value) {
    Rising = value && !Q;
    Falling = !value && Q;
    Q = value;
  }
};

template <typename T = int32_t>
class StateTimer {
 public:
  T State {};
  bool Reset {false};

  bool Changed {false};
  double ElapsedS {0.0};

  void run(double dt_s = getCycleTimeS()) {
    if (Reset) {
      Changed = false;
      ElapsedS = 0.0;
      PreviousState_ = State;
      Initialized_ = true;
      return;
    }

    if (!Initialized_) {
      Changed = true;
      ElapsedS = 0.0;
      PreviousState_ = State;
      Initialized_ = true;
      return;
    }

    if (State != PreviousState_) {
      Changed = true;
      ElapsedS = 0.0;
      PreviousState_ = State;
      return;
    }

    Changed = false;
    ElapsedS += std::max(dt_s, 0.0);
  }

 private:
  T PreviousState_ {};
  bool Initialized_ {false};
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

class MoveAverage {
 public:
  bool Enable {true};
  bool Reset {false};
  double In {0.0};
  uint32_t WindowSamples {8u};

  double Out {0.0};
  uint32_t ActiveSamples {0u};

  void run() {
    const size_t window = std::max<size_t>(1u, static_cast<size_t>(WindowSamples));
    if (Reset || !Enable) {
      Buffer_.clear();
      RunningSum_ = 0.0;
      WriteIndex_ = 0u;
      ActiveSamples = 0u;
      Out = In;
      return;
    }

    ensureWindow(window);

    if (ActiveSamples < window) {
      Buffer_[WriteIndex_] = In;
      RunningSum_ += In;
      ++ActiveSamples;
    } else {
      RunningSum_ -= Buffer_[WriteIndex_];
      Buffer_[WriteIndex_] = In;
      RunningSum_ += In;
    }

    WriteIndex_ = (WriteIndex_ + 1u) % window;
    const double divisor = (ActiveSamples > 0u) ? static_cast<double>(ActiveSamples) : 1.0;
    Out = RunningSum_ / divisor;
  }

 private:
  std::vector<double> Buffer_;
  double RunningSum_ {0.0};
  size_t WriteIndex_ {0u};

  void ensureWindow(size_t window) {
    if (Buffer_.size() == window) {
      return;
    }
    Buffer_.assign(window, In);
    RunningSum_ = In;
    ActiveSamples = 1u;
    WriteIndex_ = (window > 1u) ? 1u : 0u;
    Out = In;
  }
};

class MinMaxHold {
 public:
  bool Enable {true};
  bool Reset {false};
  bool TrackMin {true};
  bool TrackMax {true};
  double In {0.0};

  bool Valid {false};
  double Min {0.0};
  double Max {0.0};
  double Span {0.0};

  void run() {
    if (Reset || !Enable) {
      Valid = false;
      Min = 0.0;
      Max = 0.0;
      Span = 0.0;
      return;
    }

    if (!Valid) {
      Valid = true;
      Min = In;
      Max = In;
      Span = 0.0;
      return;
    }

    if (TrackMin && (In < Min)) {
      Min = In;
    }
    if (TrackMax && (In > Max)) {
      Max = In;
    }
    Span = Max - Min;
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
