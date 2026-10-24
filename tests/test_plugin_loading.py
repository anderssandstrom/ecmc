#!/usr/bin/env python3
"""Exercise the production legacy plugin loader with real temporary libraries.

No EPICS needed. Uses the real plugin ABI and dlopen/dlsym/dlclose; only error
reporting/logging is substituted. This does not cover the cpp_logic loader.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
plugin_dir = ROOT / 'devEcmcSup/plugin'
header = (plugin_dir / 'ecmcPluginLib.h').read_text().replace('#include "ecmcError.h"', '')
source = (plugin_dir / 'ecmcPluginLib.cpp').read_text().replace('#include "ecmcPluginLib.h"', '')
stubs = r'''
#include <cstring>
#include <cassert>
#include <fstream>
#include <string>
#define LOGERR(...) ((void)0)
#define LOGINFO4(...) ((void)0)
struct ecmcError {
  int error = 0;
  void errorReset() { error = 0; }
  int setErrorID(int code) { error = code; return code; }
};
'''
fixture = r'''
#include "ecmcPluginDefs.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
void event(char c) {
  FILE *f = fopen(getenv("PLUGIN_TEST_EVENTS"), "a");
  if (f) { fputc(c, f); fclose(f); }
}
__attribute__((destructor)) static void libraryClosed() { event('U'); }
int construct(char *config) { event('C'); return strcmp(config, "fail") == 0 ? 73 : 0; }
void destruct() { event('D'); }
int enter() { event('E'); return 17; }
int run(int error) { event('R'); return error + 1; }
int exitRT() { event('X'); return 19; }
extern "C" ecmcPluginData *GETTER() {
  static ecmcPluginData data = {};
  data.ifVersion = MODE == 2 ? 0 : ECMC_PLUG_VERSION_MAGIC;
  data.name = MODE == 3 ? "" : "fixture";
  data.constructFnc = MODE == 4 ? nullptr : construct;
  data.destructFnc = MODE == 4 ? nullptr : destruct;
  data.realtimeEnterFnc = MODE == 4 ? nullptr : enter;
  data.realtimeFnc = MODE == 4 ? nullptr : run;
  data.realtimeExitFnc = MODE == 4 ? nullptr : exitRT;
  return MODE == 1 ? nullptr : &data;
}
'''
tests = r'''
std::string events(const char *path) {
  std::ifstream f(path);
  return std::string(std::istreambuf_iterator<char>(f), {});
}
int main(int argc, char **argv) {
  assert(argc == 4);
  const std::string root = argv[1];
  const char *log = argv[2];
  const std::string scenario = argv[3];
  auto lib = [&](int mode) { return root + "/" + std::to_string(mode) + "/sample.so"; };
  ecmcPluginLib plugin(0);
  assert(plugin.exeRTFunc(5) == 0 && plugin.exeEnterRTFunc() == 0 && plugin.exeExitRTFunc() == 0);
  if (scenario == "lifecycle") {
    assert(plugin.load(lib(0).c_str(), "settings=42") == 0);
    assert(plugin.getData() && plugin.exeEnterRTFunc() == 17);
    assert(plugin.exeRTFunc(41) == 42 && plugin.exeExitRTFunc() == 19);
    plugin.unload();
    assert(events(log) == "CERXDU"); // U proves dlclose actually unloaded the library.
    assert(plugin.getData() == nullptr && plugin.exeRTFunc(5) == 0);
    plugin.unload(); // Safe when already unloaded.
  } else if (scenario == "optional") {
    assert(plugin.load(lib(4).c_str(), "") == 0);
    assert(plugin.exeRTFunc(5) == 0 && plugin.exeEnterRTFunc() == 0 && plugin.exeExitRTFunc() == 0);
    plugin.unload();
    assert(events(log) == "U");
  } else if (scenario == "reload") {
    assert(plugin.load(lib(0).c_str(), "") == 0);
    assert(plugin.load(lib(0).c_str(), "") == 0);
    assert(events(log) == "CDUC");
    plugin.unload();
    assert(events(log) == "CDUCDU");
  } else if (scenario == "constructor_failure") {
    assert(plugin.load(lib(0).c_str(), "fail") == 73);
    assert(plugin.getData() == nullptr && events(log) == "CDU");
    assert(plugin.load(lib(0).c_str(), "") == 0);
    plugin.unload();
    assert(events(log) == "CDUCDU");
  } else if (scenario == "destructor") {
    { ecmcPluginLib scoped(1); assert(scoped.load(lib(0).c_str(), "") == 0); }
    assert(events(log) == "CDU");
  } else {
    assert(plugin.load((root + "/missing.so").c_str(), "") == ERROR_PLUGIN_FLIE_NOT_FOUND);
    assert(plugin.load((root + "/text.so").c_str(), "") == ERROR_PLUGIN_OPEN_FAIL);
    const int errors[] = {ERROR_PLUGIN_GET_DATA_FAIL, ERROR_PLUGIN_VERSION_MISSMATCH,
                          ERROR_PLUGIN_LIB_NAME_UNDEFINED, ERROR_PLUGIN_GET_DATA_FUNC_FAIL};
    const int modes[] = {1, 2, 3, 5};
    for (int i = 0; i < 4; ++i) {
      assert(plugin.load(lib(modes[i]).c_str(), "") == errors[i]);
      assert(plugin.getData() == nullptr);
      assert(plugin.exeRTFunc(4) == 0);
      assert(events(log) == std::string(i + 1, 'U'));
    }
    assert(plugin.load(lib(0).c_str(), "") == 0); // Recover after failed loads.
    plugin.unload();
    assert(events(log) == "UUUUCDU");
  }
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    compiler = os.environ.get('CXX', 'c++')
    flags = [compiler, '-std=c++17', '-I' + str(plugin_dir)]
    (path / 'fixture.cpp').write_text(fixture)
    for mode in range(6):
        (path / str(mode)).mkdir()
        subprocess.run(flags + ['-dynamiclib' if sys.platform == 'darwin' else '-shared',
                       '-fPIC', '-DMODE=' + str(mode),
                       '-DGETTER=' + ('wrong_symbol' if mode == 5 else '_plugin_get_data_sample'),
                       str(path / 'fixture.cpp'), '-o', str(path / str(mode) / 'sample.so')], check=True)
    (path / 'text.so').write_text('not a shared library')
    (path / 'test.cpp').write_text(stubs + header + source + tests)
    subprocess.run(flags + [str(path / 'test.cpp'), '-o', str(path / 'test')]
                   + ([] if sys.platform == 'darwin' else ['-ldl']), check=True)
    for scenario in ('lifecycle', 'optional', 'reload', 'constructor_failure', 'destructor', 'errors'):
        log = path / (scenario + '.log')
        subprocess.run([str(path / 'test'), directory, str(log), scenario],
                       env=dict(os.environ, PLUGIN_TEST_EVENTS=str(log)), check=True)
print('Plugin loading: real libraries, ABI failures, optional callbacks, lifecycle, unload and reload passed.')
