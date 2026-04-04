/*************************************************************************\
* Copyright (c) 2026 Paul Scherrer Institute
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  main.cpp
*
*  C++ logic example using the ecmc MC runtime wrappers.
*
\*************************************************************************/

#include "ecmcCppLogic.hpp"
#include "ecmcCppMotion.hpp"

struct NativeMotionLogic : public ecmcCpp::LogicBase {
  ecmcCpp::McAxisRef axis {1};
  ecmcCpp::McPower power;
  ecmcCpp::McMoveAbsolute move_absolute;
  ecmcCpp::McReadStatus read_status;
  ecmcCpp::McReadActualPosition read_position;

  int32_t cycle_counter {0};
  double actual_position {0.0};
  double target_position {12800.0};
  uint8_t power_status {0};
  uint8_t move_busy {0};
  uint8_t move_done {0};
  uint8_t status_valid {0};
  uint8_t standstill {0};

  NativeMotionLogic() {
    epics.readOnly("motion.actual_position", actual_position)
         .readOnly("motion.target_position", target_position)
         .readOnly("motion.cycle_counter", cycle_counter)
         .readOnly("motion.power_status", power_status)
         .readOnly("motion.move_busy", move_busy)
         .readOnly("motion.move_done", move_done)
         .readOnly("motion.status_valid", status_valid)
         .readOnly("motion.standstill", standstill);
  }

  void run() override {
    cycle_counter += 1;

    power.run(axis, true);
    read_status.run(axis, true);
    read_position.run(axis, true);

    actual_position = read_position.Position;
    power_status = power.Status ? 1u : 0u;
    status_valid = read_status.Valid ? 1u : 0u;
    standstill = read_status.StandStill ? 1u : 0u;

    const bool should_execute = power.Status && status_valid;
    move_absolute.run(axis, should_execute, target_position, 1000.0, 2000.0, 2000.0);

    move_busy = move_absolute.Busy ? 1u : 0u;
    move_done = move_absolute.Done ? 1u : 0u;

    if (move_absolute.Done) {
      target_position = (target_position > 0.0) ? 0.0 : 12800.0;
      ecmcCpp::publishDebugText("cpp logic motion target toggled");
    }
  }
};

ECMC_CPP_LOGIC_REGISTER_DEFAULT(NativeMotionLogic)
