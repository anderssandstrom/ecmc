#!/usr/bin/env python3
"""Test cpp_logic loading through instance creation with real shared libraries.

Extracts the production load prefix, configuration parser and unload method.
Stops before item binding/asyn registration: success means instance creation,
not a fully registered or runnable IOC module. Host motion services are stubs.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'devEcmcSup/logic/ecmcCppLogicLib.cpp').read_text()


def method(signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2] + '\n'


config = source[source.index('struct CppLogicConfig {'):source.index('struct ResolvedItemBinding {')]
config += ''.join(method(signature) for signature in (
    'std::string trimCopy(', 'std::string stripOptionalQuotes(',
    'std::vector<std::string> splitConfigTokens(', 'std::string normalizeAsynPortName(',
    'bool parseConfigString(',
))
start = source.index('int ecmcCppLogicLib::load(')
load = source[start:source.index('  const ecmcCppLogicItemBinding* itemBindings =', start)]
load += '  return 0; // Harness boundary: instance created, before binding/asyn setup.\n}\n'
prelude = r'''
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <exception>
#include <fstream>
#include <string>
#include <vector>
#include "ecmcCppLogic.h"
#include "ecmcErrorsList.h"
#define LOGERR(...) ((void)0)
constexpr const char *kCppLogicGetApiSymbol = "ecmc_cpp_logic_get_api";
constexpr const char *kDefaultAsynPortBase = "CPP.LOGIC", *kAsynPortPrefix = "CPP.";
'''
stubs = r'''
class ecmcCppLogicLib {
public:
  struct Impl {
    int index = 3;
    void *dlHandle = nullptr, *instance = nullptr;
    const ecmcCppLogicApi *api = nullptr;
    ecmcCppLogicHostServices hostServices {};
    CppLogicConfig config;
    std::string libFilename, configString, createErrorMessage, lastErrorMessage;
    bool enteredRt = false, loaded = false, debugTextInitialized = false;
    struct Port {};
    Port *asynPort = nullptr;
    std::vector<int> builtinParams, exportedParams, itemBindings;
    std::string logicName, debugText, debugTextLastValue;
  } state;
  Impl *impl_ = &state;
  int error = 0;
  ~ecmcCppLogicLib() { unload(); }
  void errorReset() { error = 0; }
  int setErrorID(int value) { error = value; return value; }
  int load(const char *, const char *);
  void unload();
};
ecmcCppLogicLib::Impl *g_hostServiceCppLogic = nullptr, *g_activeCppLogic = nullptr;
void updateStatusWord(ecmcCppLogicLib::Impl *) {}
void getEcmcCppLogicHostServices(ecmcCppLogicHostServices *services) { *services = {}; }
double currentCycleTimeS() { return .001; }
void publishCurrentDebugText(const char *) {}
int32_t setCurrentDebugEnable(int32_t value) { return value; }
'''
implementation = (method('const char* getCurrentMacrosText()')
                  + method('int32_t setCurrentCreateErrorMessage(')
                  + load + method('void ecmcCppLogicLib::unload()'))
fixture = r'''
#include "ecmcCppLogic.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
const ecmcCppLogicHostServices *host;
int mode;
void event(char c) { FILE *f = fopen(getenv("CPP_TEST_EVENTS"), "a"); if (f) { fputc(c, f); fclose(f); } }
__attribute__((destructor)) static void closed() { event('U'); }
void setHost(const ecmcCppLogicHostServices *services) { host = services; }
int32_t create(void **instance) {
  event('C');
  if (mode == 5) { host->set_create_error_message("configuration rejected"); return 71; }
  if (mode == 6) throw std::runtime_error("constructor exception");
  if (mode == 7) throw 42;
  if (mode == 8) return 0; // Success without an instance must be rejected.
  *instance = new int(42);
  return 0;
}
void destroy(void *instance) { delete static_cast<int *>(instance); event('D'); }
void run(void *) {}
extern "C" const ecmcCppLogicApi *GETTER() {
  mode = atoi(getenv("CPP_TEST_MODE"));
  static ecmcCppLogicApi api {};
  api.abiVersion = mode == 1 ? 0 : ECMC_CPP_LOGIC_ABI_VERSION;
  api.setHostServices = setHost;
  api.createInstance = mode == 2 ? nullptr : create;
  api.runCycle = mode == 3 ? nullptr : run;
  api.destroyInstance = mode == 4 ? nullptr : destroy;
  return mode == 9 ? nullptr : &api;
}
'''
tests = r'''
std::string events(const char *path) { std::ifstream f(path); return {std::istreambuf_iterator<char>(f), {}}; }
int main(int argc, char **argv) {
  assert(argc == 3);
  std::string root = argv[1], library = root + "/logic.so";
  const char *log = argv[2];
  CppLogicConfig config;
  std::string error;
  assert(parseConfigString("asyn_port=TEST;sample_rate_ms=2;update_rate_ms=10;macros='a=1;b=2'", &config, &error));
  assert(config.asynPortName == "CPP.TEST" && config.sampleRateMs == 2 && config.updateRateMs == 10);
  assert(config.macrosText == "a=1;b=2");
  assert(parseConfigString("asyn_port=CPP.TEST", &config, &error) && config.asynPortName == "CPP.TEST");
  ecmcCppLogicLib logic;
  assert(logic.load(nullptr, "") == ERROR_MAIN_CPP_LOGIC_FILENAME_EMPTY);
  assert(logic.load("", "") == ERROR_MAIN_CPP_LOGIC_FILENAME_EMPTY);
  for (const char *bad : {"unknown=1", "broken", "sample_rate_ms=-1", "sample_rate_ms=nan", "update_rate_ms=inf", "update_rate_ms=1x"})
    assert(logic.load(library.c_str(), bad) == ERROR_MAIN_CPP_LOGIC_CONFIG_INVALID);
  assert(logic.load((root + "/missing.so").c_str(), "") == ERROR_MAIN_CPP_LOGIC_OPEN_FAIL);
  assert(logic.load((root + "/wrong.so").c_str(), "") == ERROR_MAIN_CPP_LOGIC_GET_API_FAIL);
  assert(events(log) == "U");
  std::string expected = "U";
  for (int mode = 1; mode <= 9; ++mode) {
    setenv("CPP_TEST_MODE", std::to_string(mode).c_str(), 1);
    const int result = logic.load(library.c_str(), nullptr);
    int code = mode == 1 ? ERROR_MAIN_CPP_LOGIC_ABI_MISMATCH :
               mode <= 4 || mode == 9 ? ERROR_MAIN_CPP_LOGIC_API_NULL :
               mode == 5 ? 71 : ERROR_MAIN_CPP_LOGIC_CREATE_INSTANCE_FAIL;
    assert(result == code);
    assert(!logic.state.instance && !logic.state.api && !logic.state.dlHandle);
    assert(g_hostServiceCppLogic == nullptr);
    expected += mode >= 5 && mode <= 8 ? "CU" : "U";
    assert(events(log) == expected);
    if (mode == 5) assert(logic.state.lastErrorMessage == "configuration rejected");
    if (mode == 6) assert(logic.state.lastErrorMessage == "constructor exception");
    if (mode == 7) assert(logic.state.lastErrorMessage.find("unknown exception") != std::string::npos);
  }
  setenv("CPP_TEST_MODE", "0", 1);
  assert(logic.load(library.c_str(), nullptr) == 0);
  assert(logic.state.instance && logic.state.config.asynPortName == "CPP.LOGIC3");
  assert(logic.state.lastErrorMessage.empty());
  expected += "C";
  assert(logic.load(library.c_str(), "asyn_port=OTHER") == 0);
  expected += "DUC";
  assert(events(log) == expected && logic.state.config.asynPortName == "CPP.OTHER");
  logic.unload(); expected += "DU";
  logic.unload();
  assert(events(log) == expected);
  puts("cpp_logic: config, library/symbol/ABI validation, required callbacks, creation failures/exceptions and cleanup passed.");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    flags = [os.environ.get('CXX', 'c++'), '-std=c++17',
             '-I' + str(ROOT / 'devEcmcSup/logic'), '-I' + str(ROOT / 'devEcmcSup/main')]
    (path / 'fixture.cpp').write_text(fixture)
    for name, getter in (('logic', 'ecmc_cpp_logic_get_api'), ('wrong', 'wrong_symbol')):
        subprocess.run(flags + ['-dynamiclib' if sys.platform == 'darwin' else '-shared',
                       '-fPIC', '-DGETTER=' + getter, str(path / 'fixture.cpp'),
                       '-o', str(path / (name + '.so'))], check=True)
    (path / 'test.cpp').write_text(prelude + config + stubs + implementation + tests)
    subprocess.run(flags + [str(path / 'test.cpp'), '-o', str(path / 'test')]
                   + ([] if sys.platform == 'darwin' else ['-ldl']), check=True)
    log = str(path / 'events.log')
    subprocess.run([str(path / 'test'), directory, log],
                   env=dict(os.environ, CPP_TEST_EVENTS=log, CPP_TEST_MODE='0'), check=True)
