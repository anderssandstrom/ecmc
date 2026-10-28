#!/usr/bin/env python3
"""Test real PLC file loading/alias expansion with a fake task/asyn endpoint.

Does not compile or execute expressions through exprtk. The file reader,
index checks, alias parser, substitution and enable code are production code.
"""
# Expectation basis: INFERRED: loading/substitution correctness; COMPAT: parser/reload policy.
# Rationale, sources and open questions: EXPECTATIONS.md
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'devEcmcSup/plc/ecmcPLCMain.cpp').read_text()
header = (ROOT / 'devEcmcSup/plc/ecmcPLCMain.h').read_text()
definitions = (ROOT / 'devEcmcSup/main/ecmcDefinitions.h').read_text()


def method(signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2] + '\n'


constants = '\n'.join(line for line in definitions.splitlines() if line.startswith(
    ('#define ECMC_MAX_PLCS ', '#define ECMC_MAX_AXES ', '#define ECMC_PLC_FILE_COMMENT_CHAR ')))
checks = header[header.index('#define ERROR_PLCS_INDEX_OUT_OF_RANGE'):header.index('class ecmcPLCMain')]
stubs = r'''
#include <cassert>
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <vector>
void ecmcRtLoggerLogError(const char *, ...) {}
struct Task {
  std::string code;
  int calls = 0, failAt = 0;
  int appendRawExpr(const char *line) {
    if (++calls == failAt) return 71;
    code += line; return 0;
  }
};
struct Enable {
  double value = 0; int updates = 0;
  void setData(double v) { value = v; }
  void updateAsyn(int) { ++updates; }
};
'''
cls = r'''
class ecmcPLCMain {
public:
  Task task;
  Enable enable;
  Task *plcs_[ECMC_MAX_PLCS + ECMC_MAX_AXES] = {};
  Enable *plcEnable_[ECMC_MAX_PLCS + ECMC_MAX_AXES] = {};
  ecmcPLCMain() { plcs_[0] = &task; plcEnable_[0] = &enable; }
  int setErrorID(const char *, const char *, int, int code) { return code; }
  int loadPLCFile(int plcIndex, char *fileName);
  int appendExprLine(int plcIndex, const char *expr);
  int setEnable(int plcIndex, int enable);
  void trimInplace(std::string&);
  bool isExpandableBase(const std::string&);
  std::unordered_map<std::string, Alias> parseVarBlock(const std::string&, std::string&);
  std::string substituteWithSuffix(const std::string&, const std::unordered_map<std::string, Alias>&);
};
'''
implementation = ''.join(method(signature) for signature in (
    'int ecmcPLCMain::loadPLCFile(', 'int ecmcPLCMain::appendExprLine(',
    'int ecmcPLCMain::setEnable(', 'void ecmcPLCMain::trimInplace(',
    'bool ecmcPLCMain::isExpandableBase(',
    'std::unordered_map<std::string,Alias>\necmcPLCMain::parseVarBlock(',
    'std::string ecmcPLCMain::substituteWithSuffix(',
))
tests = r'''
int main(int argc, char **argv) {
  assert(argc == 2);
  std::string filename = std::string(argv[1]) + "/code.plc";
  auto load = [&](ecmcPLCMain &plc, int index = 0) {
    return plc.loadPLCFile(index, &filename[0]);
  };
  ecmcPLCMain missing;
  assert(load(missing) == ERROR_PLCS_FILE_NOT_FOUND);
  assert(missing.task.code.empty() && missing.enable.updates == 0);
  for (int index : {-1, ECMC_MAX_PLCS + ECMC_MAX_AXES})
    assert(load(missing, index) == ERROR_PLCS_INDEX_OUT_OF_RANGE);
  assert(load(missing, 1) == ERROR_PLCS_PLC_NULL);
  {
    std::ofstream f(filename);
    f << "VAR\n motor : ax2; # axis alias\n value : ax3.enc.actpos;\nEND_VAR\n"
         "# ignored\nmotor.drv.enable:=1; # enable\n"
         "x:=value;\nx:=motorExtra;\nprint('motor');\n";
  }
  ecmcPLCMain plc;
  assert(load(plc) == 0);
  assert(plc.enable.value == 1 && plc.enable.updates == 1);
  assert(plc.task.code.find("ax2.drv.enable:=1;") != std::string::npos);
  assert(plc.task.code.find("x:=ax3.enc.actpos;") != std::string::npos);
  assert(plc.task.code.find("x:=motorExtra;") != std::string::npos);
  assert(plc.task.code.find("print('motor');") != std::string::npos);
  assert(plc.task.code.find("# ignored") == std::string::npos);

  // Reload appends by design; it does not silently replace existing code.
  { std::ofstream f(filename); f << "x:=1;\nx:=2;"; }
  const std::string previous = plc.task.code;
  assert(load(plc) == 0 && plc.task.code == previous + "x:=1;\nx:=2;\n");
  assert(plc.enable.updates == 2);
  ecmcPLCMain failed;
  failed.task.failAt = 2;
  assert(load(failed) == 71);
  assert(failed.enable.value == 0 && failed.enable.updates == 0);
  assert(failed.task.code == "x:=1;\n"); // Loading currently keeps the accepted prefix.

  { std::ofstream f(filename); }
  ecmcPLCMain empty;
  assert(load(empty) == 0 && empty.task.code.empty() && empty.enable.value == 1);
  puts("PLC loading: files, indices, aliases, comments, append errors, enable and reload passed.");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(stubs + constants + '\n' + checks + cls + implementation + tests)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra',
                    '-Werror', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test'), directory], check=True)
