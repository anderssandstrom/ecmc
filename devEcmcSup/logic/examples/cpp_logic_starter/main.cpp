/*************************************************************************\
* Copyright (c) 2026 Paul Scherrer Institute
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  main.cpp
*
*  Starter example for the additive C++ logic interface in ecmc.
*
\*************************************************************************/

#include "ecmcCppLogic.hpp"

struct CppLogicStarter : public ecmcCpp::LogicBase {
  int32_t actual_position {0};
  int16_t drive_control {0};
  int16_t velocity_setpoint {1000};

  CppLogicStarter() {
    ecmc.input("ec.s14.positionActual01", actual_position)
        .output("ec.s14.driveControl01", drive_control)
        .output("ec.s14.velocitySetpoint01", velocity_setpoint);

    epics.readOnly("starter.actual_position", actual_position)
         .writable("starter.velocity_setpoint", velocity_setpoint);
  }

  void run() override {
    drive_control = 0x0001;
  }
};

ECMC_CPP_LOGIC_REGISTER_DEFAULT(CppLogicStarter)
