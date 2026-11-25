#ifndef ECMC_RT_TIMING_CAPTURE_H
#define ECMC_RT_TIMING_CAPTURE_H

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <inttypes.h>
#include <memory>
#include <new>

// One cyclic producer. IOC shell commands arm/export a finite capture.
// A completed capture is immutable until the next successful arm.
class ecmcRtTimingCapture {
public:
  struct Sample {
    uint64_t scheduled;
    uint64_t awake;
    uint64_t asynAcquired;
    uint64_t motionAcquired;
    uint64_t beforeSend;
    uint64_t afterEthercat;
    uint64_t cycleEnd;
  };
  static constexpr int capacity = 65536;

  bool arm(int cycles) {
    if (cycles < 1 || cycles > capacity) return false;
    State state = state_.load(std::memory_order_acquire);
    if ((state != State::Idle && state != State::Complete) ||
        !state_.compare_exchange_strong(state, State::Arming, std::memory_order_acq_rel))
      return false;
    // Allocate and initialize from the IOC shell thread before publishing the
    // capture to RT. Preserve the previous capture if allocation fails.
    std::unique_ptr<Sample[]> samples(new (std::nothrow) Sample[cycles]());
    if (!samples) {
      state_.store(state, std::memory_order_release);
      return false;
    }
    samples_.swap(samples);
    count_ = 0;
    limit_ = cycles;
    state_.store(State::Collecting, std::memory_order_release);
    return true;
  }

  bool collecting() const {
    return state_.load(std::memory_order_acquire) == State::Collecting;
  }

  // Only call from the cyclic thread after collecting() returned true.
  void record(const Sample &sample) {
    samples_[count_++] = sample;
    if (count_ == limit_) finish();
  }

  void finish() {
    State collecting = State::Collecting;
    state_.compare_exchange_strong(collecting, State::Complete, std::memory_order_release);
  }

  // Returns a row count on success, -1 if unavailable, -2 for an I/O error.
  int writeCsv(const char *path) {
    if (!path || !*path) return -2;
    State complete = State::Complete;
    if (!state_.compare_exchange_strong(complete, State::Exporting, std::memory_order_acq_rel))
      return -1;

    int result = -2;
    if (FILE *file = fopen(path, "w")) {
      bool ok = fputs("scheduled_ns,wake_ns,after_asyn_ns,after_motor_mutex_ns,"
                      "before_send_ns,after_ethercat_ns,cycle_end_ns\n", file) >= 0;
      for (int i = 0; ok && i < count_; ++i) {
        const auto &s = samples_[i];
        ok = fprintf(file, "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                           ",%" PRIu64 ",%" PRIu64 "\n",
                     s.scheduled, s.awake, s.asynAcquired, s.motionAcquired, s.beforeSend,
                     s.afterEthercat, s.cycleEnd) >= 0;
      }
      if (fclose(file) == 0 && ok) result = count_;
    }
    state_.store(State::Complete, std::memory_order_release);
    return result;
  }

private:
  enum class State : unsigned int { Idle, Arming, Collecting, Complete, Exporting };
  static_assert(std::atomic<State>::is_always_lock_free,
                "RT timing capture state must be lock-free");
  std::atomic<State> state_{State::Idle};
  int count_ = 0;
  int limit_ = 0;
  std::unique_ptr<Sample[]> samples_;
};

#endif
