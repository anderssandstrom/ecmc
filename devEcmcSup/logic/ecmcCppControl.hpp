/*************************************************************************\
* Copyright (c) 2026 Paul Scherrer Institute
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcCppControl.hpp
*
\*************************************************************************/

#pragma once

#include "ecmcCppLogic.hpp"

#include <algorithm>

namespace ecmcCpp {

class Pid {
 public:
  bool Enable {true};
  bool Reset {false};
  double Setpoint {0.0};
  double Actual {0.0};
  double FF {0.0};
  double Kp {1.0};
  double Ki {0.0};
  double Kd {0.0};
  double Kff {1.0};
  double DT {0.0};
  double DFilterTau {0.0};
  double OutMin {0.0};
  double OutMax {0.0};
  double IMin {0.0};
  double IMax {0.0};

  double Error {0.0};
  double Output {0.0};
  double PPart {0.0};
  double IPart {0.0};
  double DPart {0.0};
  double FFPart {0.0};
  bool Limited {false};

  void run() {
    Error = Setpoint - Actual;

    if (Reset) {
      clearState();
      PrevError_ = Error;
      WasEnabled_ = false;
      return;
    }

    if (!Enable) {
      Output = 0.0;
      PPart = 0.0;
      DPart = 0.0;
      FFPart = FF * Kff;
      Limited = false;
      PrevError_ = Error;
      DState_ = 0.0;
      WasEnabled_ = false;
      return;
    }

    const double dt = resolveDt();
    if (!WasEnabled_) {
      PPart = Error * Kp;
      DPart = 0.0;
      FFPart = FF * Kff;
      const double unsat = PPart + IPart + FFPart;
      Output = saturate(unsat, Limited);
      PrevError_ = Error;
      DState_ = 0.0;
      WasEnabled_ = true;
      return;
    }

    PPart = Error * Kp;
    double candidateI = IPart + Error * Ki * dt;
    limitIntegral(candidateI);

    if (dt > 0.0) {
      const double dRaw = (Error - PrevError_) / dt;
      if (DFilterTau > 0.0) {
        const double alpha = dt / (DFilterTau + dt);
        DState_ = DState_ + alpha * (dRaw - DState_);
      } else {
        DState_ = dRaw;
      }
      DPart = Kd * DState_;
    } else {
      DState_ = 0.0;
      DPart = 0.0;
    }

    FFPart = FF * Kff;
    const double unsat = PPart + candidateI + DPart + FFPart;
    Output = saturate(unsat, Limited);

    if ((!Limited) ||
        ((unsat > OutMax) && (Error < 0.0)) ||
        ((unsat < OutMin) && (Error > 0.0))) {
      IPart = candidateI;
    }

    PrevError_ = Error;
    WasEnabled_ = true;
  }

 private:
  double PrevError_ {0.0};
  double DState_ {0.0};
  bool WasEnabled_ {false};

  double resolveDt() const {
    return (DT > 0.0) ? DT : getCycleTimeS();
  }

  void clearState() {
    Output = 0.0;
    PPart = 0.0;
    IPart = 0.0;
    DPart = 0.0;
    FFPart = 0.0;
    Limited = false;
    DState_ = 0.0;
  }

  void limitIntegral(double& value) const {
    if (IMax > IMin) {
      value = std::clamp(value, IMin, IMax);
    }
  }

  double saturate(double value, bool& limited) const {
    limited = false;
    if (OutMax > OutMin) {
      if (value > OutMax) {
        limited = true;
        return OutMax;
      }
      if (value < OutMin) {
        limited = true;
        return OutMin;
      }
    }
    return value;
  }
};

}  // namespace ecmcCpp
