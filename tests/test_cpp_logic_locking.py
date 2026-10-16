#!/usr/bin/env python3
"""Compile production callback handoff/lock code with fake asyn endpoints.

Run explicitly when builds are allowed. No EPICS or EtherCAT installation needed.
Set CXXFLAGS=-fsanitize=thread to additionally check for data races.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'devEcmcSup/logic/ecmcCppLogicLib.cpp').read_text()


def definition(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


# Live-data leaf methods must take the main lock; wrappers only dispatch by type.
for name in ('writeInt32', 'writeFloat64', 'writeInt64', 'writeTypedArray',
             'readTypedArray', 'readInt64BackedAsFloat64Array',
             'writeFloat64AsInt64BackedArray', 'drvUserCreate'):
    assert 'MainPortGuard mainLock;' in definition('asynStatus CppLogicAsynPort::' + name + '(')
for name in ('syncExportedParams', 'syncOctetParam', 'syncOneParam'):
    body = definition('bool CppLogicAsynPort::' + name + '(')
    assert 'callParamCallbacks(' not in body and 'fireArrayCallback(' not in body
worker = definition('void CppLogicAsynPort::callbackWorker()')
assert 'MainPortGuard' not in worker and 'impl_' not in worker
rt = definition('int ecmcCppLogicLib::exeRTFunc(')
assert 'RtPublicationScope publicationScope;' in rt
assert rt.index('flushSnapshots()') < rt.index('impl_->executeDividerCounter++')
assert rt.index('flushSnapshots()') < rt.index('impl_->executionEnabled == 0u')
flush = definition('void CppLogicAsynPort::flushSnapshots(')
assert 'std::defer_lock' in flush and 'if (!lock.try_lock()) return;' in flush
assert 'callbackMutex_' not in definition('void CppLogicAsynPort::queueSnapshot(')
assert 'flushSnapshots()' in definition('void CppLogicAsynPort::scheduleCallbacks(')
command = (ROOT / 'devEcmcSup/logic/ecmcCppLogicCmd.cpp').read_text()
assert command.index('if (allowCallbackEpicsState)') < command.index('delete cppLogics[logicId]')

stubs = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
using namespace std::chrono_literals;
enum asynStatus { asynSuccess, asynError };
enum asynParamType { asynParamNotDefined, asynParamOctet };
struct ecmcAsynPortDriver {
  std::recursive_mutex mutex;
  asynStatus lock() { mutex.lock(); return asynSuccess; }
  asynStatus unlock() { mutex.unlock(); return asynSuccess; }
};
ecmcAsynPortDriver mainPort;
ecmcAsynPortDriver* asynPort = &mainPort;
bool epicsStarted = true;
bool isEpicsStarted() { return epicsStarted; }
'''
binding = source[source.index('struct CallbackSnapshot;'):
                 source.index('class CppLogicAsynPort;')]
fixture = r'''
class CppLogicAsynPort : public ecmcAsynPortDriver {
 public:
  std::mutex callbackMutex_;
  std::condition_variable callbackCv_;
  bool callbackPending_ = false, stopCallbackThread_ = false;
  std::map<int, std::unique_ptr<CallbackSnapshot>> snapshots_;
  CallbackSnapshot *pendingHead_ = nullptr, *pendingTail_ = nullptr;
  CallbackSnapshot *dirtyHead_ = nullptr, *dirtyTail_ = nullptr;
  bool wakePending_ = false;
  std::mutex observedMutex;
  std::condition_variable observedCv;
  std::vector<std::vector<uint8_t>> observed;
  std::promise<void> firstEntered, releaseFirst;
  std::shared_future<void> release = releaseFirst.get_future().share();
  bool holdFirst = true;
  int cachedScalar = 0;
  void lowerCurrentThreadPriority() {}
  void registerSnapshot(ExportedParamBinding&);
  void flushSnapshots();
  void queueSnapshot(const ExportedParamBinding&, const void*);
  void scheduleCallbacks(bool);
  void callbackWorker();
  void setStringParam(int, const char*) {}
  bool publishScalar(ExportedParamBinding* b) {
    memcpy(&cachedScalar, b->data, sizeof(cachedScalar)); return true;
  }
  void callParamCallbacks() {}
  void fireArrayCallback(ExportedParamBinding* b) {
    auto* bytes = static_cast<uint8_t*>(b->data);
    std::vector<uint8_t> before(bytes, bytes + b->bytes);
    if (holdFirst) {
      holdFirst = false;
      firstEntered.set_value();
      assert(release.wait_for(3s) == std::future_status::ready);
    }
    // A producer can replace the pending value while delivery is blocked.
    assert(std::equal(before.begin(), before.end(), bytes));
    {
      std::lock_guard<std::mutex> lock(observedMutex);
      observed.push_back(before);
    }
    observedCv.notify_all();
  }
  void stop() {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    stopCallbackThread_ = true;
    callbackCv_.notify_one();
  }
};
'''
implementation = ''.join(definition('void CppLogicAsynPort::' + name + '(')
                         for name in ('registerSnapshot', 'queueSnapshot',
                                      'flushSnapshots', 'scheduleCallbacks', 'callbackWorker'))
checks = r'''
int main() {
  // Failure and recursive use of the same main lock.
  asynPort = nullptr;
  { MainPortGuard guard; assert(!guard); }
  asynPort = &mainPort;
  {
    MainPortGuard outer; assert(outer);
    MainPortGuard inner; assert(inner);
    auto blocked = std::async(std::launch::async, [] {
      bool got = mainPort.mutex.try_lock();
      if (got) mainPort.mutex.unlock();
      return got;
    });
    assert(!blocked.get());
  }
  assert(mainPort.mutex.try_lock()); mainPort.mutex.unlock();

  CppLogicAsynPort port;
  std::vector<uint8_t> live(4096, 1);
  ExportedParamBinding array;
  array.paramId = 1; array.bytes = live.size(); array.isArray = true;
  array.data = live.data();
  port.registerSnapshot(array);
  int scalar = 17;
  ExportedParamBinding scalarBinding;
  scalarBinding.paramId = 2; scalarBinding.bytes = sizeof(scalar);
  scalarBinding.data = &scalar;
  port.registerSnapshot(scalarBinding);
  // Deliberately hold one module's queue while the simulated RT thread stages
  // and flushes updates for several independent modules. No worker runs yet.
  CppLogicAsynPort other;
  ExportedParamBinding otherBinding = scalarBinding;
  other.registerSnapshot(otherBinding);
  {
    std::unique_lock<std::mutex> heldQueue(port.callbackMutex_);
    auto contended = std::async(std::launch::async, [&] {
      MainPortGuard guard;
      RtPublicationScope rt;
      for (int i = 10; i <= 30; ++i) {
        port.queueSnapshot(scalarBinding, &i);
        port.scheduleCallbacks(true);
        other.queueSnapshot(otherBinding, &i);
        other.scheduleCallbacks(true);
      }
    });
    assert(contended.wait_for(3s) == std::future_status::ready);
    contended.get();
    assert(port.dirtyHead_ && !port.pendingHead_);
    { MainPortGuard guard; other.flushSnapshots(); }
    assert(!other.dirtyHead_ && other.pendingHead_);
    assert(cppLogicRtPublication == false); // Scope is thread-local.
  }
  // Retry without changing/staging anything again: the final value survives.
  {
    MainPortGuard guard;
    port.flushSnapshots();
    assert(!port.dirtyHead_ && port.pendingHead_);
    int retained = 0;
    memcpy(&retained, scalarBinding.snapshot->pendingData.get(), sizeof(retained));
    assert(retained == 30);
  }
  // Startup pending snapshots must acquire a wakeup after IOC startup even
  // when no parameter changes. A failed RT try-lock must retain that wakeup.
  {
    MainPortGuard guard;
    epicsStarted = false;
    other.queueSnapshot(otherBinding, &scalar);
    other.flushSnapshots();
    assert(other.wakePending_);
    epicsStarted = true;
  }
  {
    std::unique_lock<std::mutex> heldQueue(other.callbackMutex_);
    auto wakeRetry = std::async(std::launch::async, [&] {
      MainPortGuard guard;
      RtPublicationScope rt;
      other.flushSnapshots();
    });
    assert(wakeRetry.wait_for(3s) == std::future_status::ready);
    wakeRetry.get();
    assert(other.wakePending_);
  }
  {
    MainPortGuard guard;
    other.flushSnapshots();
    assert(!other.wakePending_ && other.callbackPending_);
  }
  // Overwrite the retained scalar before starting the slow-callback scenario.
  {
    MainPortGuard guard;
    port.queueSnapshot(scalarBinding, &scalar);
    port.flushSnapshots();
  }
  std::thread worker([&] { port.callbackWorker(); });
  {
    MainPortGuard guard;
    port.queueSnapshot(array, live.data());
    port.queueSnapshot(scalarBinding, &scalar);
    port.scheduleCallbacks(true);
  }
  assert(port.firstEntered.get_future().wait_for(3s) == std::future_status::ready);
  // Worker now holds CPP lock in a slow callback. RT must still make progress.
  auto producer = std::async(std::launch::async, [&] {
    MainPortGuard guard;
    RtPublicationScope rt;
    for (int i = 2; i <= 20; ++i) {
      std::fill(live.begin(), live.end(), i);
      port.queueSnapshot(array, live.data());
      scalar = i;
      port.queueSnapshot(scalarBinding, &scalar);
      port.scheduleCallbacks(true);
    }
  });
  assert(producer.wait_for(3s) == std::future_status::ready);
  producer.get();
  { MainPortGuard guard; port.flushSnapshots(); }
  port.releaseFirst.set_value();
  {
    std::unique_lock<std::mutex> lock(port.observedMutex);
    assert(port.observedCv.wait_for(lock, 3s, [&] { return port.observed.size() >= 2; }));
    assert(port.observed[0] == std::vector<uint8_t>(4096, 1));
    assert(port.observed[1] == std::vector<uint8_t>(4096, 20));
  }
  // Worker shutdown must not depend on acquiring the main ECMC lock.
  {
    MainPortGuard guard;
    port.stop();
    worker.join();
  }
  assert(port.cachedScalar == 20);
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / 'locking.cpp'
    binary = Path(directory) / 'locking'
    cpp.write_text(stubs + binding + fixture + implementation + checks)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-pthread',
                    '-Wall', '-Wextra', '-Werror',
                    *shlex.split(os.environ.get('CXXFLAGS', '')),
                    str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=15)
print('C++ logic lock and callback snapshot checks passed.')
