/*************************************************************************\
* Copyright (c) 2026 Paul Scherrer Institute
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcCppTrace.hpp
*
\*************************************************************************/

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace ecmcCpp {

template <typename T, std::size_t Capacity>
class TriggeredTrace {
 public:
  static_assert(Capacity > 0u, "TriggeredTrace Capacity must be > 0");

  bool Arm {false};
  bool Trigger {false};
  std::size_t PreTriggerSamples {Capacity / 4u};
  std::size_t PostTriggerSamples {Capacity / 2u};

  bool Armed {false};
  bool Capturing {false};
  bool CaptureReady {false};
  std::size_t ActiveSamples {0u};
  std::size_t TriggerIndex {0u};
  std::size_t TriggerCount {0u};
  std::array<T, Capacity> Samples {};

  void reset() {
    Arm = false;
    Trigger = false;
    Armed = false;
    Capturing = false;
    CaptureReady = false;
    ActiveSamples = 0u;
    TriggerIndex = 0u;
    TriggerCount = 0u;
    historyWriteIndex_ = 0u;
    historyValid_ = 0u;
    postSamplesRemaining_ = 0u;
    armPrev_ = false;
    triggerPrev_ = false;
    history_.fill(T {});
    Samples.fill(T {});
  }

  void run(const T& sample) {
    pushHistory(sample);

    if (Arm && !armPrev_ && !Capturing) {
      Armed = true;
      CaptureReady = false;
      ActiveSamples = 0u;
    }

    const bool triggerRising = Trigger && !triggerPrev_;
    if (Armed && triggerRising) {
      startCapture();
    } else if (Capturing) {
      appendPostSample(sample);
    }

    armPrev_ = Arm;
    triggerPrev_ = Trigger;
  }

 private:
  std::array<T, Capacity> history_ {};
  std::size_t historyWriteIndex_ {0u};
  std::size_t historyValid_ {0u};
  std::size_t postSamplesRemaining_ {0u};
  bool armPrev_ {false};
  bool triggerPrev_ {false};

  void pushHistory(const T& sample) {
    history_[historyWriteIndex_] = sample;
    historyWriteIndex_ = (historyWriteIndex_ + 1u) % Capacity;
    historyValid_ = std::min<std::size_t>(historyValid_ + 1u, Capacity);
  }

  std::size_t resolvedPreSamples() const {
    if (historyValid_ == 0u) {
      return 0u;
    }
    const std::size_t maxPre = historyValid_ > 0u ? historyValid_ - 1u : 0u;
    return std::min(PreTriggerSamples, maxPre);
  }

  std::size_t resolvedPostSamples(std::size_t preSamples) const {
    const std::size_t remaining = Capacity > (preSamples + 1u) ? Capacity - (preSamples + 1u) : 0u;
    return std::min(PostTriggerSamples, remaining);
  }

  std::size_t newestHistoryIndex() const {
    return (historyWriteIndex_ + Capacity - 1u) % Capacity;
  }

  void copyHistoryWindow(std::size_t preSamples) {
    const std::size_t newest = newestHistoryIndex();
    const std::size_t start =
      (newest + Capacity - preSamples) % Capacity;
    const std::size_t count = preSamples + 1u;
    for (std::size_t i = 0u; i < count; ++i) {
      Samples[i] = history_[(start + i) % Capacity];
    }
  }

  void startCapture() {
    const std::size_t preSamples = resolvedPreSamples();
    const std::size_t postSamples = resolvedPostSamples(preSamples);
    copyHistoryWindow(preSamples);
    TriggerIndex = preSamples;
    ActiveSamples = preSamples + 1u;
    postSamplesRemaining_ = postSamples;
    Capturing = postSamplesRemaining_ > 0u;
    CaptureReady = !Capturing;
    Armed = false;
    TriggerCount += 1u;
  }

  void appendPostSample(const T& sample) {
    if (!Capturing || ActiveSamples >= Capacity) {
      Capturing = false;
      return;
    }
    Samples[ActiveSamples] = sample;
    ActiveSamples += 1u;
    if (postSamplesRemaining_ > 0u) {
      postSamplesRemaining_ -= 1u;
    }
    if (postSamplesRemaining_ == 0u) {
      Capturing = false;
      CaptureReady = true;
    }
  }
};

}  // namespace ecmcCpp
