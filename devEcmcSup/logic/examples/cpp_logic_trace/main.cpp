/*************************************************************************\
* Copyright (c) 2026 Paul Scherrer Institute
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  main.cpp
*
*  C++ logic example using the reusable triggered trace helper.
*
\*************************************************************************/

#include "ecmcCppLogic.hpp"
#include "ecmcCppTrace.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

struct CppLogicTrace : public ecmcCpp::LogicBase {
  static constexpr std::size_t kTraceCapacity = 256u;

  double actual_position {0.0};
  uint8_t trigger_input {0u};

  uint8_t arm_request {1u};
  uint32_t pre_trigger_samples {64u};
  uint32_t post_trigger_samples {128u};
  uint8_t capture_ready {0u};
  uint8_t armed {0u};
  uint8_t capturing {0u};
  uint32_t active_samples {0u};
  uint32_t trigger_index {0u};
  uint32_t trigger_count {0u};

  std::array<double, kTraceCapacity> position_trace {};
  ecmcCpp::TriggeredTrace<double, kTraceCapacity> trace;

  CppLogicTrace() {
    ecmc.input("ec.s14.positionActual01", actual_position)
        .input("ec.s15.binaryInput01", trigger_input);

    epics.writable("trace.arm", arm_request)
         .writable("trace.pre_trigger_samples", pre_trigger_samples)
         .writable("trace.post_trigger_samples", post_trigger_samples)
         .readOnly("trace.capture_ready", capture_ready)
         .readOnly("trace.armed", armed)
         .readOnly("trace.capturing", capturing)
         .readOnly("trace.active_samples", active_samples)
         .readOnly("trace.trigger_index", trigger_index)
         .readOnly("trace.trigger_count", trigger_count)
         .readOnlyArray("trace.position", position_trace);
  }

  void run() override {
    trace.PreTriggerSamples =
      std::min<std::size_t>(pre_trigger_samples, kTraceCapacity - 1u);
    trace.PostTriggerSamples =
      std::min<std::size_t>(post_trigger_samples,
                            kTraceCapacity - 1u - trace.PreTriggerSamples);
    trace.Arm = arm_request != 0u;
    trace.Trigger = trigger_input != 0u;
    trace.run(actual_position);

    capture_ready = trace.CaptureReady ? 1u : 0u;
    armed = trace.Armed ? 1u : 0u;
    capturing = trace.Capturing ? 1u : 0u;
    active_samples = static_cast<uint32_t>(trace.ActiveSamples);
    trigger_index = static_cast<uint32_t>(trace.TriggerIndex);
    trigger_count = static_cast<uint32_t>(trace.TriggerCount);
    position_trace = trace.Samples;
  }
};

ECMC_CPP_LOGIC_REGISTER_DEFAULT(CppLogicTrace)
