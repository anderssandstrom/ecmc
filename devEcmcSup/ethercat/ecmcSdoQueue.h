#ifndef ECMC_SDO_QUEUE_H_
#define ECMC_SDO_QUEUE_H_

#include <array>
#include <cstddef>
#include <cstdint>

// No allocation or blocking transport in submit()/poll(). Calls are serialized
// by the owning ECMC asyn/RT lock. Startup takes a copy of the head request and
// performs its blocking transport outside that lock before completing it.
class ecmcSdoQueueTarget {
public:
  virtual ~ecmcSdoQueueTarget() = default;
  virtual int beginQueuedTransfer(bool write, uint64_t value) = 0;
  virtual int pollQueuedTransfer(bool *done) = 0;
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
  };
  explicit ecmcSdoQueue(int fullError) : fullError_(fullError) {}

  int submit(ecmcSdoQueueTarget &target, bool write, uint64_t value) {
    if (count_ == CAPACITY) return fullError_;
    items_[(head_ + count_) % CAPACITY] = {&target, value, write};
    ++count_;
    target.queuedRequestAccepted();
    return 0;
  }
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
    Request request;
    if (claim(&request)) {
      const int error = request.target->beginQueuedTransfer(request.write, request.value);
      if (error) complete(error);
    }
  }

private:
  std::array<Request, CAPACITY> items_{};
  size_t head_ = 0, count_ = 0;
  bool active_ = false, running_ = false;
  int batchError_ = 0, fullError_;
};
#endif
