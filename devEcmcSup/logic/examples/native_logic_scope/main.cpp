/*************************************************************************\
* Copyright (c) 2026 Paul Scherrer Institute
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  main.cpp
*
*  EL3702 / EL1252 style native logic scope example.
*
\*************************************************************************/

#include "ecmcNativeLogic.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

struct NativeScopeLogic : public ecmcNative::LogicBase {
  static constexpr size_t kMaxScopeSamples = 4096;
  static constexpr size_t kRingCapacity = 16384;

  std::vector<int16_t> ch1_samples;
  std::vector<int16_t> ch2_samples;
  uint8_t trigger_input {0};
  uint32_t sample_timestamp_next {0};
  uint64_t trigger_timestamp {0};

  std::vector<int16_t> ch1_ring;
  std::vector<int16_t> ch2_ring;
  std::vector<int16_t> ch1_scope;
  std::vector<int16_t> ch2_scope;

  uint32_t pre_trigger_samples {256};
  uint32_t post_trigger_samples {768};
  uint32_t active_samples {0};
  uint32_t max_samples {static_cast<uint32_t>(kMaxScopeSamples)};
  uint32_t frame_size {0};
  uint32_t ring_valid_samples {0};
  uint32_t trigger_count {0};
  uint32_t trigger_index {0};
  uint32_t trigger_offset_samples {0};
  uint32_t last_time_diff_ns {0};
  double sample_period_ns {0.0};
  uint8_t trigger_latched {0};
  uint8_t capture_ready {0};

  size_t ring_write_index {0};
  bool capture_pending {false};
  uint32_t samples_since_trigger {0};
  uint64_t trigger_timestamp_old {0};

  NativeScopeLogic()
      : ch1_ring(kRingCapacity, 0),
        ch2_ring(kRingCapacity, 0),
        ch1_scope(kMaxScopeSamples, 0),
        ch2_scope(kMaxScopeSamples, 0) {
    ecmc.inputAutoArray("ec0.s20.mm.analogInputArray01", ch1_samples)
        .inputAutoArray("ec0.s20.mm.analogInputArray02", ch2_samples)
        .input("ec0.s20.nextSyncTime", sample_timestamp_next)
        .input("ec0.s21.binaryInput01", trigger_input)
        .input("ec0.s21.timestampLatchPositive01", trigger_timestamp);

    epics.readOnly("scope.max_samples", max_samples)
         .readOnly("scope.frame_size", frame_size)
         .readOnly("scope.ring_valid_samples", ring_valid_samples)
         .readOnly("scope.trigger_count", trigger_count)
         .readOnly("scope.trigger_index", trigger_index)
         .readOnly("scope.trigger_input", trigger_input)
         .readOnly("scope.trigger_latched", trigger_latched)
         .readOnly("scope.capture_ready", capture_ready)
         .readOnly("scope.active_samples", active_samples)
         .readOnly("scope.sample_timestamp_next", sample_timestamp_next)
         .readOnly("scope.trigger_timestamp", trigger_timestamp)
         .readOnly("scope.trigger_offset_samples", trigger_offset_samples)
         .readOnly("scope.last_time_diff_ns", last_time_diff_ns)
         .readOnly("scope.sample_period_ns", sample_period_ns)
         .writable("scope.pre_trigger_samples", pre_trigger_samples)
         .writable("scope.post_trigger_samples", post_trigger_samples)
         .readOnlyArray("scope.ch1_scope", ch1_scope)
         .readOnlyArray("scope.ch2_scope", ch2_scope);
  }

  static uint32_t clampToMax(uint32_t requested) {
    return std::min(requested, static_cast<uint32_t>(kMaxScopeSamples));
  }

  uint32_t totalRequestedSamples() const {
    const uint32_t pre = clampToMax(pre_trigger_samples);
    const uint32_t max_post = static_cast<uint32_t>(kMaxScopeSamples) - pre;
    const uint32_t post = std::min(post_trigger_samples, max_post);
    return pre + post;
  }

  void appendFrame(const std::vector<int16_t>& source, std::vector<int16_t>& ring, size_t count) {
    for (size_t i = 0; i < count; ++i) {
      ring[(ring_write_index + i) % ring.size()] = source[i];
    }
  }

  void appendCurrentFrame(size_t sample_count) {
    appendFrame(ch1_samples, ch1_ring, sample_count);
    appendFrame(ch2_samples, ch2_ring, sample_count);
    ring_write_index = (ring_write_index + sample_count) % ch1_ring.size();
    ring_valid_samples = static_cast<uint32_t>(
      std::min<size_t>(ch1_ring.size(), static_cast<size_t>(ring_valid_samples) + sample_count));
  }

  void clearScopeTail(size_t start) {
    std::fill(ch1_scope.begin() + static_cast<ptrdiff_t>(start), ch1_scope.end(), 0);
    std::fill(ch2_scope.begin() + static_cast<ptrdiff_t>(start), ch2_scope.end(), 0);
  }

  void copyFromRing(const std::vector<int16_t>& ring,
                    std::vector<int16_t>&       output,
                    size_t                      oldest_from_end,
                    size_t                      count) {
    const size_t start_index = (ring_write_index + ring.size() - oldest_from_end) % ring.size();
    for (size_t i = 0; i < count; ++i) {
      output[i] = ring[(start_index + i) % ring.size()];
    }
  }

  void finalizeCapture(uint32_t capture_samples, uint32_t pre_samples) {
    const size_t capture_count = capture_samples;
    const size_t oldest_from_end =
      static_cast<size_t>(samples_since_trigger) + capture_count - pre_samples;
    copyFromRing(ch1_ring, ch1_scope, oldest_from_end, capture_count);
    copyFromRing(ch2_ring, ch2_scope, oldest_from_end, capture_count);
    clearScopeTail(capture_count);
    active_samples = capture_samples;
    trigger_index = pre_samples;
    capture_ready = 1u;
    capture_pending = false;
    ecmcNative::publishDebugText("native scope capture ready");
  }

  void run() override {
    const size_t sample_count = std::min(ch1_samples.size(), ch2_samples.size());
    if (sample_count == 0u || sample_count > ch1_ring.size()) {
      trigger_latched = 0u;
      capture_ready = 0u;
      active_samples = 0u;
      return;
    }

    frame_size = static_cast<uint32_t>(sample_count);
    const double cycle_time_s = ecmcNative::getCycleTimeS();
    sample_period_ns = (cycle_time_s > 0.0)
                         ? (cycle_time_s * 1.0e9 / static_cast<double>(sample_count))
                         : 0.0;

    appendCurrentFrame(sample_count);

    pre_trigger_samples = clampToMax(pre_trigger_samples);
    post_trigger_samples =
      std::min(post_trigger_samples, static_cast<uint32_t>(kMaxScopeSamples) - pre_trigger_samples);
    const uint32_t capture_samples = totalRequestedSamples();
    const bool trigger_edge =
      (trigger_timestamp != 0u) && (trigger_timestamp != trigger_timestamp_old);
    trigger_latched = trigger_edge ? 1u : 0u;

    if (trigger_edge) {
      const uint32_t trigger_l32 = static_cast<uint32_t>(trigger_timestamp & 0xFFFFFFFFu);
      last_time_diff_ns = sample_timestamp_next - trigger_l32;
      trigger_offset_samples =
        (sample_period_ns > 0.0)
          ? static_cast<uint32_t>(std::floor(static_cast<double>(last_time_diff_ns) /
                                             sample_period_ns))
          : 0u;
      samples_since_trigger = trigger_offset_samples;
      capture_pending = true;
      capture_ready = 0u;
      trigger_count += 1;
      trigger_timestamp_old = trigger_timestamp;
    } else if (capture_pending) {
      samples_since_trigger += static_cast<uint32_t>(sample_count);
    }

    if (!capture_pending) {
      return;
    }

    if (samples_since_trigger < capture_samples) {
      return;
    }

    if (ring_valid_samples < samples_since_trigger + capture_samples - pre_trigger_samples) {
      return;
    }

    finalizeCapture(capture_samples, pre_trigger_samples);
  }
};

ECMC_NATIVE_LOGIC_REGISTER_DEFAULT(NativeScopeLogic)
