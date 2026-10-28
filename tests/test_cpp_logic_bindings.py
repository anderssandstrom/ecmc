#!/usr/bin/env python3
"""Exercise production binding validation with fake data-item endpoints.

Covers validation decisions, not real asyn registration or runtime data copies.
Type helpers are stand-ins for a single F64 item; the real binding ABI is used.
"""
# Expectation basis: INFERRED: binding validation; DOC: ABI fields; COMPAT: preparation order.
# Rationale, sources and open questions: EXPECTATIONS.md
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'devEcmcSup/logic/ecmcCppLogicLib.cpp').read_text()
start = source.index('    if (!resolved.item) {', source.index('int ecmcCppLogicLib::load('))
end = source.index('    impl_->itemBindings.push_back(resolved);', start)
validation = source[start:end]
stubs = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include "ecmcCppLogic.h"
#include "ecmcErrorsList.h"
#define LOGERR(...) ((void)0)
enum { ECMC_DIR_READ, ECMC_DIR_WRITE };
struct Info {
  bool dataPointerValid = true;
  void *data = nullptr;
  uint32_t dataSize = 8, dataElementSize = 8;
  int dataType = ECMC_CPP_TYPE_F64, dataDirection = ECMC_DIR_READ;
};
struct Item {
  Info info;
  bool nullInfo = false;
  Info *getDataItemInfo() { return nullInfo ? nullptr : &info; }
};
using ecmcDataItemInfo = Info;
uint32_t cppLogicTypeSize(uint32_t type) { return type == ECMC_CPP_TYPE_F64 ? 8 : 0; }
bool cppLogicTypeMatchesItemType(uint32_t type, int itemType) { return type == static_cast<uint32_t>(itemType); }
struct Resolved {
  Item *item;
  Info info;
  ecmcCppLogicItemBinding binding {};
  std::string itemName = "test";
};
struct Loader {
  int cleanups = 0;
  void unload() { ++cleanups; }
  int setErrorID(int code) { return code; }
  int validate(Resolved &resolved) {
    const std::string requestedItemName = "test", fallbackItemName = "test";
'''
tests = r'''
int32_t prepareFail(ecmcCppLogicItemBinding *, uint32_t) { return 1; }
int32_t prepareOK(ecmcCppLogicItemBinding *binding, uint32_t bytes) { binding->bytes = bytes; return 0; }
int main() {
  double value = 42;
  for (int scenario = 0; scenario < 16; ++scenario) {
    Item item;
    item.info.data = &value;
    Resolved resolved;
    resolved.item = &item;
    resolved.binding.type = ECMC_CPP_TYPE_F64;
    resolved.binding.bytes = sizeof(value);
    resolved.binding.data = &value;
    int expected = 0;
    switch (scenario) {
      case 0: break;
      case 1: resolved.item = nullptr; expected = ERROR_MAIN_CPP_LOGIC_BIND_ITEM_MISSING; break;
      case 2: item.nullInfo = true; expected = ERROR_MAIN_CPP_LOGIC_BIND_ITEM_INVALID; break;
      case 3: item.info.dataPointerValid = false; expected = ERROR_MAIN_CPP_LOGIC_BIND_ITEM_INVALID; break;
      case 4: item.info.data = nullptr; expected = ERROR_MAIN_CPP_LOGIC_BIND_ITEM_INVALID; break;
      case 5: resolved.binding.data = nullptr; expected = ERROR_MAIN_CPP_LOGIC_BIND_ITEM_INVALID; break;
      case 6: resolved.binding.bytes = 0; expected = ERROR_MAIN_CPP_LOGIC_BIND_ITEM_INVALID; break;
      case 7: resolved.binding.type = 999; expected = ERROR_MAIN_CPP_LOGIC_BIND_ITEM_INVALID; break;
      case 8: resolved.binding.bytes = 16; expected = ERROR_MAIN_CPP_LOGIC_BIND_ITEM_TYPE_MISMATCH; break;
      case 9: item.info.dataElementSize = 4; expected = ERROR_MAIN_CPP_LOGIC_BIND_ITEM_TYPE_MISMATCH; break;
      case 10: item.info.dataType = ECMC_CPP_TYPE_S32; expected = ERROR_MAIN_CPP_LOGIC_BIND_ITEM_TYPE_MISMATCH; break;
      case 11: resolved.binding.writable = 1; expected = ERROR_MAIN_CPP_LOGIC_BIND_DIRECTION_MISMATCH; break;
      case 12: item.info.dataDirection = ECMC_DIR_WRITE; expected = ERROR_MAIN_CPP_LOGIC_BIND_DIRECTION_MISMATCH; break;
      case 13: item.info.dataDirection = ECMC_DIR_WRITE; resolved.binding.writable = 1; break;
      case 14: resolved.binding.flags = ECMC_CPP_BIND_FLAG_AUTO_SIZE; expected = ERROR_MAIN_CPP_LOGIC_BIND_PREPARE_FAIL; break;
      case 15: resolved.binding.flags = ECMC_CPP_BIND_FLAG_AUTO_SIZE; resolved.binding.prepare = prepareFail; expected = ERROR_MAIN_CPP_LOGIC_BIND_PREPARE_FAIL; break;
    }
    Loader loader;
    assert(loader.validate(resolved) == expected);
    assert(loader.cleanups == (expected ? 1 : 0));
  }
  Item item;
  item.info.data = &value;
  Resolved resolved;
  resolved.item = &item;
  resolved.binding.type = ECMC_CPP_TYPE_F64;
  resolved.binding.data = &value;
  resolved.binding.flags = ECMC_CPP_BIND_FLAG_AUTO_SIZE;
  resolved.binding.prepare = prepareOK;
  Loader loader;
  assert(loader.validate(resolved) == 0);
  assert(resolved.binding.bytes == 8 && loader.cleanups == 0);
  puts("cpp_logic bindings: missing/invalid items, sizes/types, directions, auto-size preparation and cleanup passed.");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(stubs + validation + 'return 0;\n}\n};\n' + tests)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(ROOT / 'devEcmcSup/logic'), '-I' + str(ROOT / 'devEcmcSup/main'),
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
