/*************************************************************************\
* Copyright (c) 2026 Paul Scherrer Institute
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  main.cpp
*
*  C++ logic example using the retained-value helper.
*
\*************************************************************************/

#include "ecmcCppLogic.hpp"
#include "ecmcCppPersist.hpp"
#include "ecmcCppUtils.hpp"

#include <cstdint>

struct CppLogicRetained : public ecmcCpp::LogicBase {
  double actual_position {0.0};
  double retained_setpoint {1000.0};

  uint8_t load_request {0u};
  uint8_t save_request {0u};
  uint8_t startup_load_done {0u};
  uint8_t load_ok {0u};
  uint8_t save_ok {0u};

  ecmcCpp::RetainedValue<double> retained {"cpp_logic_retained_setpoint.bin"};
  ecmcCpp::RTrig load_trig;
  ecmcCpp::RTrig save_trig;

  CppLogicRetained() {
    ecmc.input("ec.s14.positionActual01", actual_position);

    epics.readOnly("retain.actual_position", actual_position)
         .writable("retain.setpoint", retained_setpoint)
         .writable("retain.load_request", load_request)
         .writable("retain.save_request", save_request)
         .readOnly("retain.startup_load_done", startup_load_done)
         .readOnly("retain.load_ok", load_ok)
         .readOnly("retain.save_ok", save_ok);
  }

  void enterRealtime() override {
    if (retained.restore()) {
      retained_setpoint = retained.Value;
    }
    startup_load_done = 1u;
    load_ok = retained.LoadOk ? 1u : 0u;
  }

  void run() override {

    load_trig.Clk = load_request != 0u;
    load_trig.run();
    if (load_trig.Q) {
      if (retained.restore()) {
        retained_setpoint = retained.Value;
      }
      load_ok = retained.LoadOk ? 1u : 0u;
      ecmcCpp::publishDebugText(load_ok ? "cpp logic retained load ok"
                                        : "cpp logic retained load failed");
    }

    save_trig.Clk = save_request != 0u;
    save_trig.run();
    if (save_trig.Q) {
      retained.Value = retained_setpoint;
      save_ok = retained.store() ? 1u : 0u;
      ecmcCpp::publishDebugText(save_ok ? "cpp logic retained save ok"
                                        : "cpp logic retained save failed");
    }
  }
};

ECMC_CPP_LOGIC_REGISTER_DEFAULT(CppLogicRetained)
