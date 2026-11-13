#!/usr/bin/env python3
"""Diagnostic snapshots own data, allocate only unlocked, and preserve YAML values.

Compile the complete production field traversal against real motion/motor
headers (only external asyn base classes are stubbed). Exercise the production
capture/IO code with a changing synthetic object graph and an instrumented lock.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SUP = ROOT / 'devEcmcSup'
source = (SUP / 'motion/ecmcMotionDiag.cpp').read_text()
writer = source[source.index('class ecmcMotionStateYamlWriter {'):
                source.index('\nvoid ecmcMotionDiagMakeDumpFileName')]
capture = source[source.index('namespace {\n// The mutex protects'):
                 source.index('\nvoid ecmcMotionDiagBuildAxisReport')]
declarations = r'''
#include "ecmcMotionDiagSnapshot.h"
#include "ecmcAxisReal.h"
#include "ecmcMasterSlaveStateMachine.h"
#include "ecmcMotorRecordController.h"
extern ecmcAxisBase *axes[ECMC_MAX_AXES];
extern ecmcAxisGroup *axisGroups[ECMC_MAX_AXES];
extern ecmcMasterSlaveStateMachine *masterSlaveSMs[ECMC_MAX_MST_SLVS_SMS];
extern ecmcPVTController *pvtCtrl_;
extern ecmcMotorRecordController *asynPortMotorRecord;
extern int appModeCmd, appModeStat, controllerError, currentMasterSlaveSMIndex;
extern uint64_t ecmcUpdatedCounter;
'''
checks = r'''
#include "ecmcMotionDiagSnapshot.h"
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <new>
#include <stdexcept>
static bool locked, failLock, failAllocation, throwCapture, growEveryCapture;
static int locks, unlocks;
void *operator new(size_t size) {
  assert(!locked); // Includes string/vector/stream allocation.
  if (failAllocation) throw std::bad_alloc();
  if (void *p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void *p) noexcept { assert(!locked); std::free(p); }
void *operator new[](size_t size) { return ::operator new(size); }
void operator delete[](void *p) noexcept { ::operator delete(p); }
// GCC can select sized deallocation; keep it on the same instrumented free path.
void operator delete(void *p, size_t) noexcept { ::operator delete(p); }
void operator delete[](void *p, size_t) noexcept { ::operator delete[](p); }
using epicsMutexId = int *;
static int mutex;
static epicsMutexId ecmcRTMutex = &mutex;
constexpr int epicsMutexLockOK = 0;
int epicsMutexLock(epicsMutexId) {
  assert(!locked);
  if (failLock) return 1;
  locked = true; ++locks; return 0;
}
void epicsMutexUnlock(epicsMutexId) { assert(locked); locked = false; ++unlocks; }
struct DiagRequest { int level; char file[512]; };
static int writeDumpFile(const DiagRequest&) { assert(!locked); return 0; }
class ecmcMotionStateYamlWriter {
  ecmcMotionDiagSnapshot& snapshot_;
public:
  explicit ecmcMotionStateYamlWriter(ecmcMotionDiagSnapshot& s) : snapshot_(s) {}
  void dumpAll() {
    assert(locked || !ecmcRTMutex);
    if (throwCapture) throw 7;
    const int count = growEveryCapture ? 100 * locks : 100;
    for (int i = 0; i < count; ++i) {
      ecmcDiagPath path;
      path << "axes[" << size_t(i) << "].generation";
      snapshot_.value(path, locks);
    }
  }
};
''' + capture + r'''
int main() {
  ecmcMotionDiagSnapshot snapshot(8192);
  char text[] = "quote\" slash\\\n\r";
  const char bounded[] = {'a', 'b', 'c'};
  const std::string dynamic = "owned string";
  locked = true;
  snapshot.value("double", 1.23456789);
  snapshot.value("signed", -9223372036854775807LL);
  snapshot.value("unsigned", 18446744073709551615ULL);
  snapshot.value("bool", true);
  snapshot.value("text", text);
  snapshot.value("bounded", bounded);
  snapshot.value("dynamic", dynamic);
  snapshot.pointer("null", static_cast<int *>(nullptr));
  snapshot.pointer("pointer", reinterpret_cast<int *>(uintptr_t(0xabc)));
  locked = false;
  text[0] = 'X'; // Must not affect the captured output.
  std::ostringstream out;
  assert(snapshot.write(out));
  assert(out.str() == "double: 1.23457\nsigned: -9223372036854775807\n"
    "unsigned: 18446744073709551615\nbool: true\n"
    "text: \"quote\\\" slash\\\\\\n\\r\"\nbounded: \"abc\"\n"
    "dynamic: \"owned string\"\nnull: null\npointer: \"0xabc\"\n");
  std::ostringstream failed;
  failed.setstate(std::ios::badbit);
  assert(!snapshot.write(failed));

  // Overflow is explicit, not truncated output; retry captures one generation.
  ecmcMotionDiagSnapshot small(1);
  assert(captureMotionSnapshot(small));
  assert(locks == 2 && unlocks == 2 && !locked);
  std::ostringstream captured;
  assert(small.write(captured));
  for (int i = 0; i < 100; ++i)
    assert(captured.str().find("axes[" + std::to_string(i) + "].generation: 2\n")
           != std::string::npos);

  ecmcMotionDiagSnapshot overflow(1);
  locked = true;
  overflow.value("value", 1);
  locked = false;
  std::ostringstream empty;
  assert(!overflow.write(empty) && empty.str().empty());
  ecmcDiagPath longPath;
  std::string longText(600, 'x');
  longPath << longText.c_str();
  overflow.reset();
  overflow.value(longPath, 1);
  assert(!overflow.valid() && !overflow.complete());

  // Bounded retries if the shape keeps growing between lock acquisitions.
  locks = unlocks = 0;
  growEveryCapture = true;
  ecmcMotionDiagSnapshot growing(1);
  assert(!captureMotionSnapshot(growing));
  assert(locks == 3 && unlocks == 3 && !locked);
  growEveryCapture = false;

  failLock = true;
  assert(!captureMotionSnapshot(snapshot) && !locked);
  failLock = false;
  throwCapture = true;
  try { captureMotionSnapshot(snapshot); assert(false); }
  catch (int) { assert(!locked && locks == unlocks); }
  throwCapture = false;
  ecmcMotionDiagSnapshot resizeFailure(1);
  failAllocation = true;
  try { captureMotionSnapshot(resizeFailure); assert(false); }
  catch (const std::bad_alloc&) { assert(!locked && locks == unlocks); }
  assert(ecmcMotionStateWriteYaml("allocation-failure.yaml") == -1 && !locked);
  failAllocation = false;
  assert(ecmcMotionStateWriteYaml(nullptr) == -1);
  assert(ecmcMotionStateWriteYaml("") == -1);
  assert(ecmcMotionStateWriteYaml("missing-directory/dump.yaml") == -1);
  assert(ecmcMotionStateWriteYaml("state.yaml") == 0);
  assert(ecmcMotionDiagWriteDumpFile("level2.yaml", 2) == 0);
  std::ifstream file("level2.yaml");
  std::string line;
  std::getline(file, line); assert(line == "---");
  std::getline(file, line); assert(line.find("Complete raw motion-object state") != std::string::npos);
  assert(!locked && locks == unlocks);
  ecmcRTMutex = nullptr;
  assert(ecmcMotionStateWriteYaml("no-mutex.yaml") == 0);
}
'''

with tempfile.TemporaryDirectory(prefix='ecmc-diag-snapshot-') as directory:
    tmp = Path(directory)
    for name in ('asynMotorAxis', 'asynMotorController'):
        (tmp / (name + '.h')).write_text(
            '#pragma once\n#include "asynDriver.h"\n#define epicsShareClass\n'
            'typedef int epicsInt32;\nclass ' + name + ' {};\n')
    includes = [tmp, ROOT / 'tools/plc-test/offline'] + [
        SUP / part for part in ('motion', 'motor', 'main', 'com', 'misc', 'ethercat')]
    compiler = [os.environ.get('CXX', 'c++'), '-std=c++17',
                '-Wno-inconsistent-missing-override']
    (tmp / 'writer.cpp').write_text(declarations + writer)
    subprocess.run(compiler + ['-fsyntax-only'] + ['-I' + str(p) for p in includes]
                   + [str(tmp / 'writer.cpp')], check=True)
    (tmp / 'checks.cpp').write_text(checks)
    subprocess.run(compiler + ['-O1', '-g', '-fsanitize=address,undefined',
                              '-fno-omit-frame-pointer', '-I' + str(SUP / 'motion'),
                              str(tmp / 'checks.cpp'), '-o', str(tmp / 'checks')], check=True)
    subprocess.run([str(tmp / 'checks')], cwd=tmp, check=True)
print('Motion diagnostic snapshot ownership, allocation, locking and output checks passed.')
