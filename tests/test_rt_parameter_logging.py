#!/usr/bin/env python3
"""Exercise the production logger with instrumented EPICS boundaries."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
motion = root / 'devEcmcSup/motion'
source = (motion / 'ecmcRtLogger.cpp').read_text()
source = re.sub(r'\bprintf\(', 'checkedPrint(', source)
checks = r'''
#include <cassert>
#include <cstdio>

static bool locked;
static unsigned lockCount, timeReads, printedLines, warnings, dropped;
int checkedPrint(const char *, ...) {
  assert(!locked);
  ++printedLines;
  return 0;
}
#include "logger.cpp"

unsigned int debug_print_flags = 0, die_on_error_flags = 0;
asynUser *pPrintOutAsynUser = nullptr;
int epicsMutexLock(epicsMutexId) {
  assert(!locked);
  locked = true;
  ++lockCount;
  return 0;
}
void epicsMutexUnlock(epicsMutexId) {
  assert(locked);
  locked = false;
}
int epicsTimeGetCurrent(epicsTimeStamp *time) {
  ++timeReads;
  time->nsec = 123000000;
  return 0;
}
int ecmcRtLoggerPortDriverStart() { return 0; }
void ecmcRtLoggerPortDriverService() {}
const char *ecmcRtLoggerPortDriverGetPortName() { return "test"; }
void ecmcRtLoggerPortDriverPublishDropped(unsigned count) { dropped += count; }
void ecmcRtLoggerPortDriverPublishMessage(int level, int source, int axis, const char *text) {
  assert(level == ECMC_RT_LOG_LEVEL_WARNING);
  assert(source == ECMC_RT_LOG_SOURCE_AXIS_BASE && axis == 3);
  assert(strstr(text, "controller.kp is 0."));
  ++warnings;
}

int main() {
  assert(ecmcRtLoggerStart() == 0);
  ecmcRtLoggerSetControlWord(ECMC_RT_LOGGER_CONTROL_WARNING_ENABLE);
  ecmcLogBufferSetCyclicProducer(true);
  ecmcLogAxisSetting(3, "controller.kp", 2.5);
  ecmcLogAxisSetting(3, "monitor.atTargetTime", 10);
  ecmcLogAxisZeroGain(3, "controller.kp", ECMC_RT_LOG_SOURCE_AXIS_BASE);
  ecmcLogAxisInnerGains(3, 1, 2, 3, 4);
  assert(lockCount == 0 && timeReads == 0 && warnings == 0);
  ecmcLogBufferSetCyclicProducer(false);
  drainAxisLogQueue();
  assert(lockCount == 4 && timeReads == 4 && warnings == 1);
  auto runtime = logBufferSnapshot(rtLogBuffer_, getRtLogBufferMutex());
  assert(runtime.entries.size() == 4);
  assert(strstr(runtime.entries[0].message, "controller.kp=2.5."));
  assert(strstr(runtime.entries[1].message, "monitor.atTargetTime=10."));
  assert(strstr(runtime.entries[3].message, "kp=1, ki=2, kd=3, tol=4"));
  assert(configLogBuffer_.count == 0);
  ecmcRtLogBufferPrint();
  assert(printedLines == 7 && !locked);

  // The producer must remain bounded even if the consumer cannot run.
  ecmcLogBufferSetCyclicProducer(true);
  const unsigned before = lockCount;
  for (unsigned i = 0; i < ECMC_RT_LOGGER_QUEUE_SIZE + 2; ++i)
    ecmcLogAxisSetting(3, "controller.ki", i * 0.5);
  assert(lockCount == before);
  ecmcLogBufferSetCyclicProducer(false);
  drainAxisLogQueue();
  reportDroppedEvents();
  assert(dropped == 3);
  assert(rtLogBuffer_.count == 4 + ECMC_RT_LOGGER_QUEUE_SIZE - 1);
}
'''

stubs = {
    'epicsMutex.h': '''
using epicsMutexId = void *;
inline epicsMutexId epicsMutexMustCreate() { return nullptr; }
int epicsMutexLock(epicsMutexId);
void epicsMutexUnlock(epicsMutexId);
''',
    'epicsThread.h': '''
using epicsThreadId = void *;
constexpr int epicsThreadPriorityLow = 0, epicsThreadStackSmall = 0;
inline unsigned epicsThreadGetStackSize(int) { return 0; }
inline epicsThreadId epicsThreadCreate(const char *, int, unsigned, void (*)(void *), void *arg) {
  static int thread;
  return &thread;
}
inline void epicsThreadSleep(double) {}
''',
    'epicsTime.h': '''
#include <stddef.h>
struct epicsTimeStamp { unsigned int nsec; };
int epicsTimeGetCurrent(epicsTimeStamp *);
inline size_t epicsTimeToStrftime(char *out, size_t, const char *, const epicsTimeStamp *) {
  *out = 0;
  return 0;
}
''',
}

with tempfile.TemporaryDirectory(prefix='ecmc-rt-param-log-') as directory:
    work = Path(directory)
    for name, contents in stubs.items():
        (work / name).write_text(contents)
    (work / 'logger.cpp').write_text(source)
    (work / 'checks.cpp').write_text(checks)
    binary = work / 'checks'
    subprocess.run([
        os.environ.get('CXX', 'c++'), '-std=c++17', '-fsanitize=address,undefined',
        '-fno-sanitize-recover=all', '-I' + str(work), '-I' + str(motion),
        '-I' + str(root / 'devEcmcSup/com'),
        '-I' + str(root / 'tools/plc-test/offline'),
        str(work / 'checks.cpp'), '-o', str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
print('Cyclic log deferral, bounded overflow and unlocked printing passed.')
