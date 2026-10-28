#!/usr/bin/env python3
"""Test real LUT reloads and scalar SoE parser branches without EtherCAT hardware."""
# Expectation basis: INFERRED: ownership/scalar safety; COMPAT/REVIEW: reload/SoE policy.
# Rationale, sources and open questions: EXPECTATIONS.md
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
misc = (ROOT / 'devEcmcSup/misc/ecmcMisc.cpp').read_text()
encoder = (ROOT / 'devEcmcSup/motion/ecmcEncoder.cpp').read_text()
parser = (ROOT / 'devEcmcSup/com/ecmcCmdParser.c').read_text()
lookup = (ROOT / 'devEcmcSup/misc/ecmcLookupTable.h').read_text()


def method(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2] + '\n'


lookup = lookup.replace('#include "ecmcError.h"', '''
class ecmcError {
 public: int setErrorID(const char *, const char *, int, int error) { return error; }
};
''')
checks = (ROOT / 'devEcmcSup/misc/ecmcMisc.h').read_text()
checks = checks[checks.index('#define CHECK_LUT_RETURN_IF_ERROR'):checks.index('# ifdef __cplusplus')]
lut_code = r'''
#include <cassert>
#include <string>
#include "ecmcErrorsList.h"
#define LOGINFO4(...) ((void)0)
#define LOGERR(...) ((void)0)
#define ECMC_MAX_LUTS 2
''' + lookup + checks + r'''
ecmcLookupTable<double, double> *luts[ECMC_MAX_LUTS] = {};
class ecmcEncoder {
public:
  ecmcLookupTable<double, double> *lookupTable_ = nullptr;
  bool lookupTableEnable_ = false;
  ~ecmcEncoder() { delete lookupTable_; }
  int loadLookupTable(const std::string &filename);
};
''' + method(misc, 'int loadLUT(') + method(encoder, 'int ecmcEncoder::loadLookupTable(') + r'''
int main(int argc, char **argv) {
  assert(argc == 5);
  assert(loadLUT(-1, argv[1]) == ERROR_LUT_INDEX_OUT_OF_RANGE);
  assert(loadLUT(ECMC_MAX_LUTS, argv[1]) == ERROR_LUT_INDEX_OUT_OF_RANGE);
  assert(loadLUT(0, argv[3]) == ERROR_LUT_LOAD_ERROR && !luts[0]);
  ecmcEncoder enc;
  assert(enc.loadLookupTable(argv[3]) == ERROR_LOOKUP_TABLE_OPEN_FILE_FAILED);
  assert(!enc.lookupTable_ && !enc.lookupTableEnable_);
  assert(loadLUT(0, argv[1]) == 0);
  assert(enc.loadLookupTable(argv[1]) == 0 && enc.lookupTableEnable_);
  auto *oldGlobal = luts[0];
  auto *oldEncoder = enc.lookupTable_;
  // Both missing and malformed files must preserve ownership through repeated failures.
  for (int i = 0; i < 4; ++i) {
    char *bad = argv[3 + (i % 2)];
    assert(loadLUT(0, bad) == ERROR_LUT_LOAD_ERROR);
    assert(luts[0] == oldGlobal && luts[0]->getValue(5) == 5);
    assert(enc.loadLookupTable(bad) == (i % 2 ? ERROR_LOOKUP_TABLE_FILE_FORMAT_INVALID
                                             : ERROR_LOOKUP_TABLE_OPEN_FILE_FAILED));
    assert(enc.lookupTable_ == oldEncoder && enc.lookupTable_->getValue(5) == 5);
    // COMPAT/REVIEW F2: disable correction after a failed load. Retaining
    // ownership safely does not independently justify this operational policy.
    assert(!enc.lookupTableEnable_);
  }
  assert(loadLUT(0, argv[2]) == 0 && luts[0]->getValue(5) == 10);
  assert(enc.loadLookupTable(argv[2]) == 0 && enc.lookupTable_->getValue(5) == 10);
  assert(enc.lookupTableEnable_);
  // Destruction after a failed reload must also be safe.
  assert(enc.loadLookupTable(argv[3]) != 0);
  assert(loadLUT(0, argv[3]) != 0);
  delete luts[0];
}
'''

macro_start = parser.index('#define SEND_RESULT_OR_ERROR_AND_RETURN_UINT64(')
macro_end = parser.index('#define SEND_RESULT_OR_ERROR_AND_RETURN_DOUBLE(', macro_start)
read_start = parser.index('  /*EcReadSoE(')
read_end = parser.index('  /*EcGetSlaveVendorId', read_start)
write_start = parser.index('  uint64_t uint64Value = 0;')
write_end = parser.index('  /*Cfg.EcApplyConfig', write_start)
soe_code = r'''
#include <assert.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "ecmcErrorsList.h"
typedef struct { char text[128]; } ecmcOutputBufferType;
static void cmd_buf_printf(ecmcOutputBufferType *buffer, const char *format, ...) {
  va_list args; va_start(args, format);
  vsnprintf(buffer->text, sizeof(buffer->text), format, args);
  va_end(args);
}
static int calls, ioError, runtime;
static size_t lastSize;
static uint64_t payload, written;
static int ecReadSoE(uint16_t slave, uint8_t drive, uint16_t idn, size_t bytes, uint8_t *value) {
  ++calls; lastSize = bytes;
  assert(slave == 1 && drive == 2 && idn == 3);
  assert(bytes > 0 && bytes <= sizeof(payload));
  if (!ioError) memcpy(value, &payload, bytes);
  return ioError;
}
static int ecWriteSoE(uint16_t slave, uint8_t drive, uint16_t idn, size_t bytes, uint8_t *value) {
  ++calls; lastSize = bytes;
  assert(slave == 1 && drive == 2 && idn == 3);
  assert(bytes > 0 && bytes <= sizeof(written));
  written = 0; memcpy(&written, value, bytes);
  return ioError;
}
#define RETURN_ERROR_IF_RUNTIME_CFG_CMD(name) do { if (runtime) return ERROR_MAIN_AXIS_COM_BLOCKED; } while (0)
''' + parser[macro_start:macro_end] + r'''
static int readCommand(const char *myarg_1, ecmcOutputBufferType *buffer) {
  int iValue2, iValue3, iValue4, iValue5, nvals;
  uint64_t u64Value = 0;
''' + parser[read_start:read_end] + r'''
  return -1;
}
static int writeCommand(const char *myarg_1) {
  int iValue2, iValue3, iValue4, iValue5, nvals;
''' + parser[write_start:write_end] + r'''
  return -1;
}
int main(void) {
  char command[128]; ecmcOutputBufferType buffer = {{0}};
  const int invalid[] = {0, -1, INT_MIN, 9, 256, INT_MAX};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    calls = 0;
    snprintf(command, sizeof(command), "EcReadSoE(1,2,3,%d)", invalid[i]);
    assert(readCommand(command, &buffer) == ERROR_MAIN_PARSER_INVALID_FORMAT);
    snprintf(command, sizeof(command), "EcWriteSoE(1,2,3,%d,42)", invalid[i]);
    assert(writeCommand(command) == ERROR_MAIN_PARSER_INVALID_FORMAT);
    assert(calls == 0);
  }
  // Every nonzero size fitting the scalar buffer is valid, including odd sizes.
  for (int size = 1; size <= 8; ++size) {
    calls = 0; payload = 42;
    snprintf(command, sizeof(command), "EcReadSoE(1,2,3,%d)", size);
    assert(readCommand(command, &buffer) == 0 && strcmp(buffer.text, "42") == 0);
    assert(calls == 1 && lastSize == (size_t)size);
    snprintf(command, sizeof(command), "EcWriteSoE(1,2,3,%d,42)", size);
    assert(writeCommand(command) == 0 && written == 42);
    assert(calls == 2 && lastSize == (size_t)size);
  }
  payload = UINT64_C(4294967296);
  assert(readCommand("EcReadSoE(1,2,3,8)", &buffer) == 0);
  assert(strcmp(buffer.text, "4294967296") == 0);
  payload = UINT64_MAX;
  assert(readCommand("EcReadSoE(1,2,3,8)", &buffer) == 0);
  assert(strcmp(buffer.text, "18446744073709551615") == 0);
  assert(writeCommand("EcWriteSoE(1,2,3,8,18446744073709551615)") == 0);
  assert(written == UINT64_MAX);
  ioError = 123;
  assert(readCommand("EcReadSoE(1,2,3,8)", &buffer) == 0);
  assert(strcmp(buffer.text, "Error: 123") == 0);
  assert(writeCommand("EcWriteSoE(1,2,3,8,42)") == 123);
  runtime = 1; calls = 0;
  assert(writeCommand("EcWriteSoE(1,2,3,8,42)") == ERROR_MAIN_AXIS_COM_BLOCKED);
  assert(calls == 0);
}
'''

with tempfile.TemporaryDirectory(prefix='ecmc-lut-soe-') as directory:
    tmp = Path(directory)
    first, second, missing, malformed = [tmp / name for name in ('first', 'second', 'missing', 'malformed')]
    first.write_text('0 0\n10 10\n20 20\n')
    second.write_text('0 0\n10 20\n20 40\n')
    malformed.write_text('not a table\n')
    for name, code, compiler, standard, suffix in [
            ('lut', lut_code, os.environ.get('CXX', 'c++'), 'c++17', 'cpp'),
            ('soe', soe_code, os.environ.get('CC', 'cc'), 'c99', 'c')]:
        source = tmp / (name + '.' + suffix)
        source.write_text(code)
        executable = tmp / name
        subprocess.run([compiler, '-std=' + standard, '-g', '-O1',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-I' + str(ROOT / 'devEcmcSup/main'), str(source),
                        '-o', str(executable)], check=True)
        subprocess.run([str(executable), str(first), str(second), str(missing), str(malformed)], check=True)
print('LUT reload and scalar SoE regression checks passed.')
