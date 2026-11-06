#ifndef ECMC_SDO_QUEUE_H_
#define ECMC_SDO_QUEUE_H_

#include <array>
#include <cstddef>
#include <cstdint>

// No allocation or blocking transport in submit()/poll(). Calls are serialized
// by the owning ECMC asyn/RT lock. Startup appends writes to EtherLab before
// activation, then uses the runtime poller for final readback verification.
class ecmcSdoQueueTarget {
public:
  virtual ~ecmcSdoQueueTarget() = default;
  virtual int beginQueuedTransfer(bool write, uint64_t value) = 0;
  virtual int pollQueuedTransfer(bool *done) = 0;
  // Startup registration is non-RT; verification uses the normal RT poller.
  virtual int configureStartupWrite(uint64_t) { return -1; }
  virtual int beginStartupVerification(uint64_t) { return -1; }
  virtual bool queuedTransferReady() const { return true; }
  virtual uint64_t startupAddress() const {
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this));
  }
  virtual void queuedRequestAccepted() = 0;
  virtual void queuedRequestStarted(bool) {}
  virtual void queuedRequestComplete(int error) = 0;
};

class ecmcSdoQueue {
public:
  static constexpr size_t CAPACITY = 128;
  struct Request {
    ecmcSdoQueueTarget *target = nullptr;
    uint64_t value = 0;
    bool write = false;
    bool startup = false;
    uint64_t expected = 0;
  };
  explicit ecmcSdoQueue(int fullError) : fullError_(fullError) {}

  int submit(ecmcSdoQueueTarget &target, bool write, uint64_t value) {
    if (count_ == CAPACITY) return fullError_;
    items_[(head_ + count_) % CAPACITY] = {&target, value, write};
    ++count_;
    target.queuedRequestAccepted();
    return 0;
  }
  // Call after all component configuration and restore, immediately before
  // activation, under the producer lock. EtherLab executes appended writes in
  // list order. Retain requests until final hardware readback has completed.
  int prepareStartup() {
    if (running_) return 0; // Runtime reentry retains the verification/error latch.
    if (active_) return -1;
    for (size_t i=0;i<count_;++i) {
      auto &request=items_[(head_+i)%CAPACITY];
      if (request.write) {
        const int error=request.target->configureStartupWrite(request.value);
        if (error) return error;
      }
    }
    for (size_t i=0;i<count_;++i) {
      auto &request=items_[(head_+i)%CAPACITY];
      request.startup=true;
      request.expected=request.value;
      // Aliases may refer to the same address. Every startup write is checked
      // against the final value, rather than superseded intermediate values.
      if (request.write) for (size_t j=i+1;j<count_;++j) {
        const auto &later=items_[(head_+j)%CAPACITY];
        if (later.write && later.target->startupAddress()==request.target->startupAddress())
          request.expected=later.value;
      }
    }
    startupRemaining_=count_; startupError_=0;
    return 0;
  }
  bool startupHeld() const { return startupRemaining_ || startupError_; }
  int startupError() const { return startupError_; }
  size_t startupRemaining() const { return startupRemaining_; }
  size_t pending() const { return count_; }
  bool running() const { return running_; }
  void setRunning(bool running) { running_ = running; }
  int batchError() const { return batchError_; }
  void clearBatchError() { if (!count_) batchError_ = 0; }

  // Claim is shared by the non-RT startup worker and the runtime poller.
  bool claim(Request *request) {
    if (!request || active_ || !count_) return false;
    active_ = true;
    *request = items_[head_];
    request->target->queuedRequestStarted(request->write);
    return true;
  }
  void complete(int error) {
    if (!active_ || !count_) return;
    auto *target = items_[head_].target;
    if (items_[head_].startup) {
      if (startupRemaining_) --startupRemaining_;
      if (error && !startupError_) startupError_=error;
    }
    items_[head_] = {};
    head_ = (head_ + 1) % CAPACITY;
    --count_;
    active_ = false;
    if (error && !batchError_) batchError_ = error;
    target->queuedRequestComplete(error);
  }
  void poll() {
    if (active_) {
      bool done = false;
      const int error = items_[head_].target->pollQueuedTransfer(&done);
      if (!done) return;
      complete(error);
    }
    // Complete an in-flight request even if its slave leaves OP. Only starting
    // the next transaction requires readiness of that transaction's own slave.
    if (count_ && !items_[head_].target->queuedTransferReady()) return;
    Request request;
    if (claim(&request)) {
      const int error = request.startup && request.write ?
        request.target->beginStartupVerification(request.expected) :
        request.target->beginQueuedTransfer(request.write, request.value);
      if (error) complete(error);
    }
  }

private:
  std::array<Request, CAPACITY> items_{};
  size_t head_ = 0, count_ = 0;
  bool active_ = false, running_ = false;
  int batchError_ = 0, fullError_;
  size_t startupRemaining_ = 0;
  int startupError_ = 0;
};
#endif
