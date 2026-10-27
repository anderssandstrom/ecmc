#!/usr/bin/env python3
"""Compile current command-buffer and storage code with ASan/UBSan.

Run: python3 tests/test_buffer_boundaries.py
Requires C/C++ compilers with address/undefined sanitizer support, no EPICS.
Command dispatch and asyn notifications are stand-ins; buffer operations and
the command splitter are production code, read fresh on each invocation.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
octet = (ROOT / 'devEcmcSup/com/ecmcOctetIF.c').read_text()
parser = (ROOT / 'devEcmcSup/com/ecmcCmdParser.c').read_text()
storage = (ROOT / 'devEcmcSup/misc/ecmcDataStorage.cpp').read_text()
storage_errors = '\n'.join(line for line in
    (ROOT / 'devEcmcSup/misc/ecmcDataStorage.h').read_text().splitlines()
    if line.startswith('#define ERROR_DATA_STORAGE_')) + '\n'


def method(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2] + '\n'


c_prelude = r'''
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "ecmcOctetIF.h"
#include "ecmcErrorsList.h"
asynUser *pPrintOutAsynUser = NULL;
int ecmcCmdParser(const char *, int, ecmcOutputBufferType *);
static char cOneCommand[ECMC_CMD_MAX_SINGLE_CMD_LENGTH];
static int calls = 0;
static size_t lastLength = 0;
static int motorHandleOneArg(const char *command, ecmcOutputBufferType *buffer) {
  ++calls;
  lastLength = strlen(command);
  return cmd_buf_printf(buffer, "OK");
}
'''
c_tests = r'''
int main(void) {
  die_on_error_flags = 0;
  // Exact-size allocation: old inlen+1 copy reads past this heap allocation.
  char *command = malloc(3);
  assert(command);
  memcpy(command, "abc", 3);
  assert(CMDwriteIt(command, 3) == 0 && calls == 1 && lastLength == 3);
  free(command);
  char out[8];
  assert(CMDreadIt(out, sizeof(out)) == 0 && strcmp(out, "OK") == 0);

  calls = 0;
  assert(CMDwriteIt(NULL, 3) == -1);
  assert(CMDwriteIt("", 0) == -1);
  char *large = malloc(ECMC_CMD_BUFFER_SIZE + 1);
  assert(large);
  memset(large, 'a', ECMC_CMD_BUFFER_SIZE + 1);
  assert(CMDwriteIt(large, ECMC_CMD_BUFFER_SIZE) == -1 && calls == 0);
  assert(CMDwriteIt(large, ECMC_CMD_BUFFER_SIZE - 1) == 0);
  assert(lastLength == ECMC_CMD_BUFFER_SIZE - 1);
  clearBuffer(getEpicsBuffer());

  // Largest safe segment, then exact-capacity and larger segments.
  const int limit = ECMC_CMD_MAX_SINGLE_CMD_LENGTH;
  large[limit - 1] = ';';
  large[limit] = '\0';
  calls = 0;
  assert(ecmcCmdParser(large, limit, getEpicsBuffer()) == 0 && calls == 1);
  assert(lastLength == (size_t)limit - 1);
  for (int length = limit; length <= limit + 1; ++length) {
    memset(large, 'a', length);
    large[length] = ';';
    large[length + 1] = '\0';
    clearBuffer(getEpicsBuffer());
    calls = 0;
    assert(ecmcCmdParser(large, length + 1, getEpicsBuffer()) ==
           ERROR_MAIN_PARSER_CMD_TO_LONG);
    assert(calls == 0); // Oversized segment must never reach dispatch.
  }
  free(large);
  clearBuffer(getEpicsBuffer());
  calls = 0;
  assert(CMDwriteIt("a;b\r\n", 5) == 0 && calls == 2);
  assert(CMDreadIt(out, sizeof(out)) == 0 && strcmp(out, "OK;OK\r\n") == 0);

  // Small reads retain the remainder and never write beyond outlen.
  cmd_buf_printf(getEpicsBuffer(), "%s", "abcdef");
  char tiny[1];
  assert(CMDreadIt(tiny, 1) == 0 && tiny[0] == '\0');
  assert(getEpicsBuffer()->bytesUsed == 6);
  char chunk[3];
  for (int i = 0; i < 3; ++i) {
    assert(CMDreadIt(chunk, sizeof(chunk)) == 0);
    assert(chunk[0] == 'a' + 2 * i && chunk[1] == 'b' + 2 * i);
    assert(chunk[2] == '\0');
  }
  assert(getEpicsBuffer()->bytesUsed == 0);
  assert(CMDreadIt(NULL, 1) == -1 && CMDreadIt(out, 0) == -1);

  ecmcOutputBufferType buffer = {.bufferSize = 4, .bytesUsed = 0};
  assert(cmd_buf_printf(&buffer, "%s", "abc") == 0);
  assert(cmd_buf_printf(&buffer, "%s", "d") == ERROR_MAIN_PRINT_TO_BUFFER_FAIL);
  assert(buffer.bytesUsed == 3 && strcmp(buffer.buffer, "abc") == 0);
  buffer.bytesUsed = 0;
  assert(cmd_buf_printf(&buffer, "%s", "abcdef") == ERROR_MAIN_PRINT_TO_BUFFER_FAIL);
  assert(buffer.bytesUsed == 3 && strcmp(buffer.buffer, "abc") == 0);
  assert(cmd_buf_printf(NULL, "%s", "a") == ERROR_MAIN_PARSER_BUFFER_NULL);
  puts("Command buffers: exact input lengths, segment limits, CRLF, partial reads, truncation passed.");
}
'''
cpp_prelude = r'''
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "ecmcErrorsList.h"
#define LOGINFO9(...) ((void)0)
enum { ECMC_STORAGE_NORMAL_BUFFER, ECMC_STORAGE_RING_BUFFER, ECMC_STORAGE_FIFO_BUFFER };
class ecmcDataStorage {
public:
  double values[4] = {10, 20, 30, 40};
  double *buffer_ = values;
  int bufferSize_ = 4, currentBufferIndex_ = 2, dataCountInBuffer_ = 2;
  int bufferType_ = 0, notifications = 0;
  bool isFull_ = false;
  int setErrorID(const char *, const char *, int, int code) { return code; }
  int updateAsyn(int) { ++notifications; return 0; }
  int isStorageFull();
  int getDataElement(int index, double *data);
  int getDataElementPtr(int index, double **data);
  int setDataElement(int index, double data);
  int setData(double *data, int size);
  int appendData(double *data, int size);
  int appendData(double *data, int size, bool refreshAsyn);
  int appendDataNormal(double *data, int size);
  int appendDataRing(double *data, int size);
  int appendDataFifo(double *data, int size);
};
'''
cpp_methods = ''.join(method(storage, signature) for signature in (
    'int ecmcDataStorage::isStorageFull()',
    'int ecmcDataStorage::getDataElement(int index, double *data)',
    'int ecmcDataStorage::getDataElementPtr(int index, double **data)',
    'int ecmcDataStorage::setDataElement(int index, double data)',
    'int ecmcDataStorage::setData(double *data, int size)',
    'int ecmcDataStorage::appendData(double *data, int size)',
    'int ecmcDataStorage::appendData(double *data, int size, bool refreshAsyn)',
    'int ecmcDataStorage::appendDataNormal(double *data, int size)',
    'int ecmcDataStorage::appendDataRing(double *data, int size)',
    'int ecmcDataStorage::appendDataFifo(double *data, int size)',
))
cpp_tests = r'''
int main() {
  double data[] = {1, 2, 3, 4, 5};
  for (int type : {ECMC_STORAGE_NORMAL_BUFFER, ECMC_STORAGE_RING_BUFFER, ECMC_STORAGE_FIFO_BUFFER}) {
    ecmcDataStorage store;
    store.bufferType_ = type;
    double original[4];
    memcpy(original, store.values, sizeof(original));
    for (int size : {-1, INT_MIN}) {
      assert(store.setData(data, size) == ERROR_MAIN_DATA_STORAGE_INVALID_SIZE);
      assert(store.appendData(data, size) == ERROR_MAIN_DATA_STORAGE_INVALID_SIZE);
      assert(store.appendData(data, size, false) == ERROR_MAIN_DATA_STORAGE_INVALID_SIZE);
      assert(store.currentBufferIndex_ == 2 && store.dataCountInBuffer_ == 2);
      assert(store.notifications == 0 && !store.isFull_);
      assert(memcmp(original, store.values, sizeof(original)) == 0);
    }
    assert(store.setData(data, 5) == ERROR_DATA_STORAGE_SIZE_TO_SMALL);
    assert(store.currentBufferIndex_ == 2 && store.dataCountInBuffer_ == 2);
    assert(store.notifications == 0 && !store.isFull_);
    assert(memcmp(original, store.values, sizeof(original)) == 0);
    assert(store.appendData(data, 5) == ERROR_DATA_STORAGE_SIZE_TO_SMALL);
    assert(store.currentBufferIndex_ == 2 && store.notifications == 0);
    assert(store.appendData(data, 0, false) == 0);
    assert(memcmp(original, store.values, sizeof(original)) == 0);
    assert(store.currentBufferIndex_ == 2 && store.dataCountInBuffer_ == 2);
    assert(store.setData(data, 4) == 0);
    assert(memcmp(data, store.values, sizeof(original)) == 0 && store.isFull_);
    assert(store.notifications == 1);
    assert(store.appendData(data + 4, 1, false) == 0);
    const double normal[] = {1, 2, 3, 4}, ring[] = {5, 2, 3, 4}, fifo[] = {2, 3, 4, 5};
    const double *expected = type == ECMC_STORAGE_NORMAL_BUFFER ? normal :
                             type == ECMC_STORAGE_RING_BUFFER ? ring : fifo;
    assert(memcmp(expected, store.values, sizeof(original)) == 0);
    assert(store.dataCountInBuffer_ == 4 && store.notifications == 1);
    for (int index : {-1, 4, INT_MAX}) {
      double value = -99, *pointer = nullptr;
      assert(store.getDataElement(index, &value) == ERROR_DATA_STORAGE_POSITION_OUT_OF_RANGE);
      assert(store.getDataElementPtr(index, &pointer) == ERROR_DATA_STORAGE_POSITION_OUT_OF_RANGE);
      assert(store.setDataElement(index, 9) == ERROR_DATA_STORAGE_POSITION_OUT_OF_RANGE);
      assert(value == -99 && pointer == nullptr);
      assert(memcmp(expected, store.values, sizeof(original)) == 0);
    }
    for (int index : {0, 3}) {
      double value = 0, *pointer = nullptr;
      assert(store.setDataElement(index, 99) == 0);
      assert(store.getDataElement(index, &value) == 0 && value == 99);
      assert(store.getDataElementPtr(index, &pointer) == 0 && pointer == &store.values[index]);
    }
  }
  for (int invalidType : {-1, 3, 99, INT_MAX}) {
    ecmcDataStorage store;
    store.bufferType_ = invalidType;
    const double original[] = {10, 20, 30, 40};
    assert(store.appendData(data, 1) == ERROR_DATA_STORAGE_INVALID_BUFFER_TYPE);
    assert(store.setData(data, 1) == ERROR_DATA_STORAGE_INVALID_BUFFER_TYPE);
    assert(store.currentBufferIndex_ == 2 && store.dataCountInBuffer_ == 2);
    assert(store.notifications == 0 && !store.isFull_);
    assert(memcmp(original, store.values, sizeof(original)) == 0);
  }
  ecmcDataStorage missing;
  missing.buffer_ = nullptr;
  assert(missing.setData(data, 1) == ERROR_DATA_STORAGE_NULL);
  assert(missing.currentBufferIndex_ == 2 && missing.notifications == 0);
  puts("Storage buffers: invalid sizes/indices, unchanged state on negative sizes, exact capacity, normal/ring/FIFO passed.");
}
'''

with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'asynDriver.h').write_text(
        '#include <stddef.h>\n#define ASYN_TRACE_ERROR 1\n'
        'typedef struct { int unused; } asynUser;\n'
        'static inline int asynPrint(asynUser *u, int reason, const char *fmt, ...) '
        '{ (void)u; (void)reason; (void)fmt; return 0; }\n')
    programs = (
        ('commands.c', os.environ.get('CC', 'cc'), '-std=c99', c_prelude
         + octet.replace('#include "ecmcCmdParser.h"', '')
         + method(parser, 'int ecmcCmdParser(const char') + c_tests),
        ('storage.cpp', os.environ.get('CXX', 'c++'), '-std=c++11',
         storage_errors + cpp_prelude + cpp_methods + cpp_tests),
    )
    for filename, compiler, standard, code in programs:
        src = path / filename
        exe = path / (filename + '.out')
        src.write_text(code)
        subprocess.run([compiler, standard, '-O1', '-g',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        '-fno-omit-frame-pointer', '-I' + directory,
                        '-I' + str(ROOT / 'devEcmcSup/com'),
                        '-I' + str(ROOT / 'devEcmcSup/main'), str(src), '-o', str(exe)],
                       check=True)
        subprocess.run([str(exe)], check=True)
