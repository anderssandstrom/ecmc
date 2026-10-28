#!/usr/bin/env python3
"""Timing units/finite values and storage-list token boundaries, without hardware."""
# Expectation basis: DOC: timing units; INFERRED: validation; COMPAT/REVIEW: list policy.
# Rationale, sources and open questions: EXPECTATIONS.md
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
thread = (ROOT / 'devEcmcSup/main/ecmcMainThread.cpp').read_text()
parser = (ROOT / 'devEcmcSup/com/ecmcCmdParser.c').read_text()


def method(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2] + '\n'


timing_code = r'''
#include <cassert>
#include <math.h>
#include <initializer_list>
#include "ecmcDefinitions.h"
#include "ecmcErrorsList.h"
#define LOGINFO4(...) ((void)0)
#define LOGERR(...) ((void)0)
static bool sampleRateChangeAllowed = true;
static double mcuFrequency = 1000, mcuPeriod = 1000000;
''' + method(thread, 'int setSampleRate(') + method(thread, 'int setSamplePeriodMs(') + r'''
int main() {
  assert(setSamplePeriodMs(2) == 0 && mcuFrequency == 500 && mcuPeriod == 2000000);
  const double minMs = MCU_MIN_PERIOD_NS / 1e6;
  const double maxMs = MCU_MAX_PERIOD_NS / 1e6;
  for (double period : {minMs, 1.0, 2.0, maxMs}) {
    assert(setSamplePeriodMs(period) == 0);
    assert(mcuPeriod == period * 1e6);
    assert(mcuFrequency == MCU_NSEC_PER_SEC / mcuPeriod);
  }
  for (double rate : {MCU_MIN_FREQUENCY, 500.0, 1000.0, MCU_MAX_FREQUENCY}) {
    assert(setSampleRate(rate) == 0);
    assert(mcuFrequency == rate && mcuPeriod == MCU_NSEC_PER_SEC / rate);
  }
  const double oldRate = mcuFrequency, oldPeriod = mcuPeriod;
  for (double period : {0.0, -1.0, 50000.0, nextafter(minMs, 0.0),
                        nextafter(maxMs, INFINITY), double(NAN),
                        double(INFINITY), -double(INFINITY)}) {
    assert(setSamplePeriodMs(period) == ERROR_MAIN_SAMPLE_RATE_OUT_OF_RANGE);
    assert(mcuFrequency == oldRate && mcuPeriod == oldPeriod);
  }
  for (double rate : {0.0, -1.0, nextafter(MCU_MIN_FREQUENCY, 0.0),
                      nextafter(MCU_MAX_FREQUENCY, INFINITY), double(NAN),
                      double(INFINITY), -double(INFINITY)}) {
    assert(setSampleRate(rate) == ERROR_MAIN_SAMPLE_RATE_OUT_OF_RANGE);
    assert(mcuFrequency == oldRate && mcuPeriod == oldPeriod);
  }
  sampleRateChangeAllowed = false;
  assert(setSampleRate(500) == ERROR_MAIN_SAMPLE_RATE_CHANGE_NOT_ALLOWED);
  assert(setSamplePeriodMs(2) == ERROR_MAIN_SAMPLE_RATE_CHANGE_NOT_ALLOWED);
  assert(mcuFrequency == oldRate && mcuPeriod == oldPeriod);
}
'''

storage_code = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#define ECMC_COMMAND_FORMAT_ERROR 0x210000
static double values[16];
static int calls, count, failAt;
static int appendStorageBuffer(int index, double *data, int size) {
  assert(index == 7 && size == 1);
  if (++calls == failAt) return 1234;
  assert(count < 16); values[count++] = *data; return 0;
}
''' + method(parser, 'static int appendAsciiDataToStorageBuffer(') + r'''
static int parse(const char *text) {
  calls = count = failAt = 0;
  // Exact allocation lets ASan detect searching beyond the terminating NUL.
  char *copy = malloc(strlen(text) + 1);
  assert(copy); strcpy(copy, text);
  int result = appendAsciiDataToStorageBuffer(7, copy);
  free(copy); return result;
}
int main(void) {
  const char *invalid[] = {"", "AppendDataStorage(7)", "AppendDataStorage(7)=",
    "AppendDataStorage(7)=  \t", "AppendDataStorage(7)=,", "AppendDataStorage(7)=oops",
    "AppendDataStorage(7)=1junk", "AppendDataStorage(7)=1 2"};
  for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
    assert(parse(invalid[i]) == ECMC_COMMAND_FORMAT_ERROR);
    assert(count == 0 && calls == 0);
  }
  assert(appendAsciiDataToStorageBuffer(7, NULL) == ECMC_COMMAND_FORMAT_ERROR);
  // COMPAT/REVIEW F4: a malformed later token leaves the valid prefix committed.
  // Rejection/memory safety does not establish whether writes should be atomic.
  assert(parse("AppendDataStorage(7)=5,") == ECMC_COMMAND_FORMAT_ERROR);
  assert(count == 1 && calls == 1 && values[0] == 5);
  assert(parse("AppendDataStorage(7)=5, \t") == ECMC_COMMAND_FORMAT_ERROR);
  assert(count == 1 && calls == 1);
  assert(parse("AppendDataStorage(7)=5,,6") == ECMC_COMMAND_FORMAT_ERROR);
  assert(count == 1 && calls == 1);
  assert(parse("AppendDataStorage(7)=5,broken") == ECMC_COMMAND_FORMAT_ERROR);
  assert(count == 1 && calls == 1);
  assert(parse("AppendDataStorage(7)=0") == 0 && count == 1 && values[0] == 0);
  assert(parse("WriteDataStorage(7)= -2.5 , +3e2, .125 \t") == 0);
  assert(count == 3 && values[0] == -2.5 && values[1] == 300 && values[2] == .125);
  // Storage values retain existing IEEE special-value support.
  // COMPAT/REVIEW F4: acceptance of nonfinite samples needs a storage policy.
  assert(parse("AppendDataStorage(7)=nan,inf,-inf") == 0 && count == 3);
  assert(isnan(values[0]) && isinf(values[1]) && values[1] > 0);
  assert(isinf(values[2]) && values[2] < 0);
  calls = count = 0; failAt = 2;
  assert(appendAsciiDataToStorageBuffer(7, "AppendDataStorage(7)=1,2,3") == 1234);
  assert(calls == 2 && count == 1 && values[0] == 1);
}
'''

with tempfile.TemporaryDirectory(prefix='ecmc-timing-storage-') as directory:
    tmp = Path(directory)
    for name, code, compiler, standard, suffix in [
            ('timing', timing_code, os.environ.get('CXX', 'c++'), 'c++17', 'cpp'),
            ('storage', storage_code, os.environ.get('CC', 'cc'), 'c99', 'c')]:
        source = tmp / (name + '.' + suffix); source.write_text(code)
        executable = tmp / name
        subprocess.run([compiler, '-std=' + standard, '-g', '-O1',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-I' + str(ROOT / 'devEcmcSup/main'), str(source), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
print('Timing and storage-list regressions passed.')
