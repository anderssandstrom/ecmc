/*************************************************************************\
* Copyright (c) 2026 Paul Scherrer Institute
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  main.cpp
*
*  C++ logic example showing array and byte-buffer bindings.
*
\*************************************************************************/

#include "ecmcCppLogic.hpp"
#include "ecmcCppUtils.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

struct CppLogicArrays : public ecmcCpp::LogicBase {
  static constexpr size_t kPreviewCount = 256;

  std::vector<double> source_trace;
  std::array<int16_t, kPreviewCount> output_frame {};
  std::array<double, kPreviewCount> preview_trace {};
  std::array<uint8_t, 64> status_blob {};

  uint32_t active_input_samples {0};
  uint32_t preview_samples {static_cast<uint32_t>(kPreviewCount)};
  double preview_average {0.0};
  double preview_min {0.0};
  double preview_max {0.0};

  ecmcCpp::MoveAverage preview_avg_filter;
  ecmcCpp::MinMaxHold preview_min_max;

  CppLogicArrays() {
    ecmc.inputAutoArray("ds0.data", source_trace)
        .outputArray("ec0.s30.mm.outputArray01", output_frame)
        .outputBytes("ec0.s30.mm.statusBlob", status_blob.data(),
                     static_cast<uint32_t>(status_blob.size()));

    epics.writable("arrays.preview_samples", preview_samples)
         .readOnly("arrays.active_input_samples", active_input_samples)
         .readOnly("arrays.preview_average", preview_average)
         .readOnly("arrays.preview_min", preview_min)
         .readOnly("arrays.preview_max", preview_max)
         .readOnlyArray("arrays.preview_trace", preview_trace)
         .readOnlyBytes("arrays.status_blob", status_blob.data(),
                        static_cast<uint32_t>(status_blob.size()));

    preview_avg_filter.WindowSamples = 16u;
  }

  void run() override {
    active_input_samples = static_cast<uint32_t>(source_trace.size());
    const size_t requested =
      std::min<size_t>(preview_trace.size(), static_cast<size_t>(preview_samples));
    const size_t available = std::min(requested, source_trace.size());

    std::fill(preview_trace.begin(), preview_trace.end(), 0.0);
    std::fill(output_frame.begin(), output_frame.end(), 0);
    std::fill(status_blob.begin(), status_blob.end(), 0u);

    preview_min_max.Reset = true;
    preview_min_max.run();

    if (available == 0u) {
      preview_average = 0.0;
      preview_min = 0.0;
      preview_max = 0.0;
      return;
    }

    preview_avg_filter.Reset = true;
    preview_avg_filter.In = source_trace[0];
    preview_avg_filter.run();

    preview_min_max.Reset = false;

    for (size_t i = 0; i < available; ++i) {
      const double sample = source_trace[i];
      preview_trace[i] = sample;

      preview_avg_filter.In = sample;
      preview_avg_filter.run();

      preview_min_max.In = sample;
      preview_min_max.run();

      const double clamped = ecmcCpp::clampValue(sample, -32768.0, 32767.0);
      output_frame[i] = static_cast<int16_t>(std::lround(clamped));
    }

    preview_average = preview_avg_filter.Out;
    preview_min = preview_min_max.Min;
    preview_max = preview_min_max.Max;

    status_blob[0] = static_cast<uint8_t>(available & 0xFFu);
    status_blob[1] = static_cast<uint8_t>((available >> 8u) & 0xFFu);
    status_blob[2] = static_cast<uint8_t>(preview_avg_filter.ActiveSamples & 0xFFu);
    status_blob[3] = preview_min_max.Valid ? 1u : 0u;
  }
};

ECMC_CPP_LOGIC_REGISTER_DEFAULT(CppLogicArrays)
