#!/usr/bin/env python3
"""Sanitizer checks for zero filters, initial memory maps and object-path bounds."""
# Expectation basis: INFERRED: bounds/data safety; COMPAT/REVIEW: filter/map defaults.
# Rationale, sources and open questions: EXPECTATIONS.md
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / 'devEcmcSup' / path).read_text()


def method(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2] + '\n'


def no_includes(source):
    return re.sub(r'^#include.*\n', '', source, flags=re.M)


common = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <new>
#include <limits>
#include <iostream>
#include <iomanip>
#include "ecmcDefinitions.h"
#include "ecmcErrorsList.h"
#define LOGERR(...) ((void)0)
#define ecmcRtLoggerLogError(...) ((void)0)
class ecmcError {
public:
  void errorReset() {}
  int setErrorID(int error) { return error; }
};
'''
utils = read('com/ecmcAsynPortDriverUtils.cpp')
# Include the shared validator and every object-path helper.
a = utils.index('static bool validObjectPath(')
b = utils.index('// Convert string to datatype', a)
path_code = common + utils[a:b] + r'''
int main() {
  struct Case { int (*parse)(char *, int *); const char *path; int expected; };
  const Case cases[] = {
    {getEcMainFuncType, ECMC_EC_STR "0." ECMC_EC_HEALTH_STR, ECMC_EC_ENTRY_INDEX_HEALTH},
    {getPVTCtrlFuncType, ECMC_PVTCTRL_STR "." ECMC_PVT_EC_TRIGGER_OUTPUT_STR, ECMC_PVT_EC_ENTRY_INDEX_TRIGGER_OUTPUT},
    {getAxMainFuncType, ECMC_AX_STR "1." ECMC_AX_HEALTH_STR, ECMC_AXIS_ENTRY_INDEX_HEALTH},
    {getAxDriveFuncType, ECMC_AX_STR "1." ECMC_DRV_STR "." ECMC_DRV_ENABLE_STR, ECMC_DRIVEBASE_ENTRY_INDEX_CONTROL_WORD},
    {getAxEncFuncType, ECMC_AX_STR "1." ECMC_ENC_STR "." ECMC_ENC_ACTPOS_STR, ECMC_ENCODER_ENTRY_INDEX_ACTUAL_POSITION},
    {getAxMonFuncType, ECMC_AX_STR "1." ECMC_MON_STR "." ECMC_MON_LOWLIM_STR, ECMC_MON_ENTRY_INDEX_LOWLIM}
  };
  for (const auto &test : cases) {
    std::string path(test.path); int function = -1;
    assert(test.parse(&path[0], &function) == 0 && function == test.expected);
    assert(test.parse(&path[0], nullptr) == ERROR_MAIN_ECMC_COMMAND_FORMAT_ERROR);
    for (const std::string bad : {std::string(), std::string(256, 'x'),
                                 path + std::string(4096, 'x'),
                                 std::string("ax1.") + std::string(300, 'x') + ".value"}) {
      std::string copy = bad; function = -1;
      assert(test.parse(&copy[0], &function) == ERROR_MAIN_ECMC_COMMAND_FORMAT_ERROR);
      assert(function == -1);
    }
    assert(test.parse(nullptr, &function) == ERROR_MAIN_ECMC_COMMAND_FORMAT_ERROR);
  }
  mainObjectType type; axisSubObjectType sub; int index = -1;
  std::string path = "ax12.enc.actpos";
  assert(getMainObjectType(&path[0], &index, &type) == 0 && index == 12 && type == ECMC_OBJ_AXIS);
  assert(getAxSubObjectType(&path[0], &sub) == 0 && sub == ECMC_AX_SUB_OBJ_ENCODER);
  // At 255 bytes the buffers still fit; at 256 bytes the path must be rejected.
  path = "ax1." + std::string(251, 'x');
  assert(getMainObjectType(&path[0], &index, &type) == 0 && index == 1);
  assert(getAxSubObjectType(&path[0], &sub) == 0 && sub == ECMC_AX_SUB_OBJ_MAIN);
  for (size_t size : {size_t(256), size_t(4095)}) {
    path = "ax1." + std::string(size - 4, 'x');
    assert(getMainObjectType(&path[0], &index, &type) == ERROR_MAIN_ECMC_COMMAND_FORMAT_ERROR);
    assert(getAxSubObjectType(&path[0], &sub) == ERROR_MAIN_ECMC_COMMAND_FORMAT_ERROR);
  }
  assert(getMainObjectType(nullptr, &index, &type) != 0);
  assert(getAxSubObjectType(nullptr, &sub) != 0);
  char valid[] = "ax1.health";
  assert(getMainObjectType(valid, nullptr, &type) != 0);
  assert(getMainObjectType(valid, &index, nullptr) != 0);
  assert(getAxSubObjectType(valid, nullptr) != 0);
}
'''

encoder = read('motion/ecmcEncoder.cpp')
filter_code = common + no_includes(read('motion/ecmcFilter.h')) + no_includes(read('motion/ecmcFilter.cpp')) + r'''
class ecmcEncoder {
public:
  ecmcFilter *positionFilter_;
  bool enablePositionFilter_ = false;
  int setPosFilterSize(size_t size);
  int setPosFilterEnable(bool enable);
};
''' + method(encoder, 'int ecmcEncoder::setPosFilterSize(') + method(encoder, 'int ecmcEncoder::setPosFilterEnable(') + r'''
int main() {
  ecmcFilter filter(0.001, 0);
  // COMPAT/REVIEW F1: size zero maps to one; this is not a documented
  // requirement to accept invalid filter sizes rather than reject them.
  assert(filter.getFilterSize() == 1);
  assert(filter.getFiltPos(12, 0) == 12);
  assert(filter.getFiltPos(15, 0) == 15);
  assert(filter.setFilterSize(0) == 0 && filter.getFilterSize() == 1);
  assert(filter.reset() == 0);
  assert(filter.getFiltVelo(0.002) == 2);
  ecmcEncoder encoder; encoder.positionFilter_ = &filter;
  assert(encoder.setPosFilterSize(0) == 0 && !encoder.enablePositionFilter_);
  assert(encoder.setPosFilterEnable(true) == 0 && encoder.enablePositionFilter_);
  assert(filter.getFiltPos(25, 0) == 25);
  assert(encoder.setPosFilterSize(3) == 0 && encoder.enablePositionFilter_);
  assert(filter.reset() == 0);
  // COMPAT/REVIEW F1: reset uses zero-filled history (not first-sample fill).
  assert(filter.getFiltPos(3, 0) == 1);
  assert(filter.getFiltPos(6, 0) == 3);
  assert(filter.getFiltPos(9, 0) == 6);
  assert(encoder.setPosFilterSize(1) == 0 && !encoder.enablePositionFilter_);
  assert(encoder.setPosFilterEnable(true) == 0);
  assert(filter.getFiltPos(42, 0) == 42);
}
'''

memmap = read('ethercat/ecmcEcMemMap.cpp')
mem_header = no_includes(read('ethercat/ecmcEcMemMap.h')).replace('private:', 'public:')
mem_code = common + r'''
enum ec_direction_t { EC_DIR_INVALID, EC_DIR_INPUT, EC_DIR_OUTPUT };
struct ecmcAsynPortDriver {};
struct ecmcEcDomain {};
struct ecmcEcEntry {};
struct ecmcAsynDataItem {};
static size_t getEcDataTypeByteSize(ecmcEcDataType) { return 1; }
''' + mem_header + r'''
int ecmcEcMemMap::initAsyn() { return 0; }
int ecmcEcMemMap::updateAsyn(bool) { return 0; }
'''
for signature in ['ecmcEcMemMap::ecmcEcMemMap(', 'void ecmcEcMemMap::initVars()',
                  'ecmcEcMemMap::~ecmcEcMemMap()', 'int ecmcEcMemMap::write(',
                  'int ecmcEcMemMap::read(', 'int ecmcEcMemMap::updateInputProcessImage()',
                  'int ecmcEcMemMap::updateOutProcessImage()']:
    mem_code += method(memmap, signature)
mem_code += r'''
int main() {
  for (auto direction : {EC_DIR_INPUT, EC_DIR_OUTPUT}) {
    ecmcEcMemMap map(nullptr, 0, 1, nullptr, nullptr, 16, direction, ECMC_EC_U8, "test");
    uint8_t domain[16]; memset(domain, 0x55, sizeof(domain)); map.adr_ = domain;
    uint8_t actual[16]; size_t bytes;
    assert(map.read(actual, sizeof(actual), &bytes) == 0 && bytes == 16);
    // INFERRED: deterministic initialized data. COMPAT: the chosen value is 0.
    for (auto value : actual) assert(value == 0);
    if (direction == EC_DIR_OUTPUT) {
      map.updateOutProcessImage();
      for (auto value : domain) assert(value == 0);
      uint8_t partial[] = {7, 8};
      assert(map.write(partial, sizeof(partial), &bytes) == 0 && bytes == 2);
      map.updateOutProcessImage();
      assert(domain[0] == 7 && domain[1] == 8);
      for (int i = 2; i < 16; ++i) assert(domain[i] == 0);
    } else {
      map.updateInputProcessImage(); map.read(actual, sizeof(actual), &bytes);
      for (auto value : actual) assert(value == 0x55);
    }
  }
}
'''

with tempfile.TemporaryDirectory(prefix='ecmc-filter-map-path-') as directory:
    tmp = Path(directory)
    for name, code in [('paths', path_code), ('filter', filter_code), ('memmap', mem_code)]:
        source = tmp / (name + '.cpp'); source.write_text(code)
        executable = tmp / name
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-g', '-O1',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-I' + str(ROOT / 'devEcmcSup/main'), str(source), '-o', str(executable)], check=True)
        # Nonzero allocation fill makes uninitialized map contents deterministic under ASan.
        env = dict(os.environ)
        env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '') + ':malloc_fill_byte=165'
        subprocess.run([str(executable)], check=True, env=env)
print('Object path, filter and memory-map regressions passed.')
