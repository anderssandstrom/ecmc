#!/usr/bin/env python3
"""Storage creation validation and diagnostic stream failure propagation."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def method(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2] + '\n'


misc = (ROOT / 'devEcmcSup/misc/ecmcMisc.cpp').read_text()
diag = (ROOT / 'devEcmcSup/motion/ecmcMotionDiag.cpp').read_text()
header = (ROOT / 'devEcmcSup/misc/ecmcDataStorage.h').read_text()
errors = '\n'.join(line for line in header.splitlines() if line.startswith('#define ERROR_DATA_STORAGE_'))
enum_start = header.index('enum ecmcDSBufferType')
enum_end = header.index('\n};', enum_start) + 3
common = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <new>
#include <initializer_list>
#include "ecmcErrorsList.h"
#define LOGINFO4(...) ((void)0)
#define LOGERR(...) ((void)0)
'''
creation_code = common + errors + '\n' + header[enum_start:enum_end] + r'''
#define ECMC_MAX_DATA_STORAGE_OBJECTS 2
static int constructions, destructions;
struct ecmcDataStorage {
  int type;
  ecmcDataStorage(void *, int, int, ecmcDSBufferType t) : type(t) { ++constructions; }
  ~ecmcDataStorage() { ++destructions; }
  int getErrorID() { return 0; }
};
static ecmcDataStorage *dataStorages[ECMC_MAX_DATA_STORAGE_OBJECTS] = {};
static void *asynPort = nullptr;
static int sampleRateChangeAllowed = 1;
''' + method(misc, 'int createDataStorage(') + r'''
int main() {
  assert(createDataStorage(0, 4, ECMC_STORAGE_NORMAL_BUFFER) == 0);
  ecmcDataStorage *original = dataStorages[0];
  for (int type : {-1, 3, 99}) {
    sampleRateChangeAllowed = 1;
    assert(createDataStorage(0, 4, type) == ERROR_DATA_STORAGE_INVALID_BUFFER_TYPE);
    assert(dataStorages[0] == original && constructions == 1 && destructions == 0);
    assert(sampleRateChangeAllowed == 1);
    assert(createDataStorage(1, 4, type) == ERROR_DATA_STORAGE_INVALID_BUFFER_TYPE);
    assert(dataStorages[1] == nullptr && constructions == 1);
  }
  assert(createDataStorage(-1, 4, 0) == ERROR_MAIN_DATA_STORAGE_INDEX_OUT_OF_RANGE);
  assert(createDataStorage(2, 4, 0) == ERROR_MAIN_DATA_STORAGE_INDEX_OUT_OF_RANGE);
  assert(createDataStorage(0, 0, 0) == ERROR_MAIN_DATA_STORAGE_INVALID_SIZE);
  assert(dataStorages[0] == original && destructions == 0);
  for (int type : {ECMC_STORAGE_NORMAL_BUFFER, ECMC_STORAGE_RING_BUFFER, ECMC_STORAGE_FIFO_BUFFER}) {
    sampleRateChangeAllowed = 1;
    assert(createDataStorage(0, 4, type) == 0 && dataStorages[0]->type == type);
    assert(sampleRateChangeAllowed == 0);
  }
  delete dataStorages[0];
  assert(constructions == destructions);
}
'''

# Model stream state, including an earlier write error with successful close.
# The production writer and its close/error decisions are compiled unchanged.
diag_code = common + r'''
struct TestFile { bool error = false; bool closed = false; } stream;
static int opens, writes, checks, closes, failWrite, closeResult;
static bool failOpen;
static TestFile *testOpen(const char *, const char *) {
  ++opens; return failOpen ? nullptr : &stream;
}
static int testPuts(const char *, TestFile *fp) {
  assert(!fp->closed);
  if (++writes == failWrite) { fp->error = true; return EOF; }
  return 0;
}
static int testError(TestFile *fp) { assert(!fp->closed); ++checks; return fp->error; }
static int testClose(TestFile *fp) { assert(!fp->closed); fp->closed = true; ++closes; return closeResult; }
#define FILE TestFile
#define fopen testOpen
#define fputs testPuts
#define ferror testError
#define fclose testClose
struct DiagRequest { const char *file; int level; };
static int appModeCmd, appModeStat, controllerError;
static const char *controllerErrorMsg = "";
static unsigned long ecmcUpdatedCounter;
static int countAxes() { return 0; }
static int countAxisGroups() { return 0; }
static int countMasterSlaveSMs() { return 0; }
static void writeJsonInt(FILE *fp, int, const char *, long long, bool = true) { fputs("int", fp); }
static void writeJsonString(FILE *fp, int, const char *, const char *) { fputs("string", fp); }
static void writeTimestamp(FILE *fp) { fputs("time", fp); }
static void writeDiagnosis(FILE *fp) { fputs("diagnosis", fp); }
static void writeAxes(FILE *fp) { fputs("axes", fp); }
static void writeAxisGroups(FILE *fp) { fputs("groups", fp); }
static void writeMasterSlaveSMs(FILE *fp) { fputs("state", fp); }
static void ecmcLogBuffersWriteJson(FILE *fp) { fputs("logs", fp); }
''' + method(diag, 'int writeDumpFile(') + r'''
static void reset() {
  stream = TestFile(); opens = writes = checks = closes = 0;
  failWrite = closeResult = 0; failOpen = false;
}
int main() {
  for (int level : {0, 1}) {
    DiagRequest request = {"test.json", level};
    reset(); assert(writeDumpFile(request) == 0);
    assert(opens == 1 && closes == 1 && stream.closed);
    const int totalWrites = writes;
    for (int failure = 1; failure <= totalWrites; ++failure) {
      reset(); failWrite = failure;
      assert(writeDumpFile(request) != 0);
      assert(stream.error && closes == 1 && checks == 1 && stream.closed);
    }
    reset(); closeResult = EOF;
    assert(writeDumpFile(request) != 0 && closes == 1);
    reset(); failWrite = 2; closeResult = EOF;
    assert(writeDumpFile(request) != 0 && closes == 1);
    reset(); failOpen = true;
    assert(writeDumpFile(request) != 0 && closes == 0 && checks == 0 && writes == 0);
  }
}
'''
with tempfile.TemporaryDirectory(prefix='ecmc-storage-diag-') as directory:
    tmp = Path(directory)
    for name, code in [('creation', creation_code), ('diag', diag_code)]:
        source = tmp / (name + '.cpp'); source.write_text(code)
        executable = tmp / name
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-g', '-O1',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-I' + str(ROOT / 'devEcmcSup/main'), str(source), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
print('Storage creation and diagnostic I/O regressions passed.')
