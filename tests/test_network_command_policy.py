#!/usr/bin/env python3
"""Compile production transport, origin routing and host-command policy.

Unrelated motion dispatch is omitted; hardware, PLC and IOC-shell side effects
are counters. The real command splitter, ADS/Cfg normalization, policy, startup
setter, expression handlers and runtime guards run with ASan/UBSan.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = (ROOT / 'devEcmcSup/com/ecmcCmdParser.c').read_text()
octet = (ROOT / 'devEcmcSup/com/ecmcOctetIF.c').read_text()


def method(signature):
    start = parser.index(signature)
    return parser[start:parser.index('\n}', start) + 2] + '\n'


def between(start, end):
    a = parser.index(start)
    return parser[a:parser.index(end, a)]


prelude = r'''
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "ecmcOctetIF.h"
#include "ecmcErrorsList.h"
asynUser *pPrintOutAsynUser = NULL;
#define ECMC_MODE_RUNTIME 1
#define ECMC_MAX_PLCS 100
#define TRANSFORM_EXPR_LINE_END_CHAR '#'
static int appModeStat, blockCmdParserInRt, ecmcInitDone;
static int iocState, shellCalls, plcCalls, pluginCalls, motionCalls;
enum { iocVoid, iocBuilding, iocBuilt, iocRunning, iocPaused };
static int getIocState(void) { return iocState; }
static int ecmcInitThread(void) { return 0; }
static int getControllerError(void) { return 123; }
static int iocshCmd(const char *cmd) { ++shellCalls; return 0; }
static int setPLCExpr(int id, char *expr) { ++plcCalls; return 0; }
static int appendPLCExpr(int id, char *expr) { ++plcCalls; return 0; }
static int appendAxisPLCExpr(int id, char *expr) { ++plcCalls; return 0; }
static int loadPlugin(int id, char *file, char *config) { ++pluginCalls; return 0; }
static int setAxisTargetVel(int id, double value) { ++motionCalls; return 0; }
static int motorHandleADS_ADR(const char *cmd, ecmcOutputBufferType *buffer) { return -2; }
static int handleTwincatSyntax(const char *cmd, ecmcOutputBufferType *buffer) {
  ++motionCalls; cmd_buf_printf(buffer, "OK"); return 0;
}
static const char *Main_dot_str="Main.", *ADSPORT_equals_str="ADSPORT=", *Cfg_dot_str="Cfg.";
static char cExprBuffer[ECMC_CMD_MAX_SINGLE_CMD_LENGTH];
static char cIdBuffer[ECMC_CMD_MAX_SINGLE_CMD_LENGTH];
static char cIdBuffer2[ECMC_CMD_MAX_SINGLE_CMD_LENGTH];
static char cOneCommand[ECMC_CMD_MAX_SINGLE_CMD_LENGTH];
'''

code = prelude + octet
code += between('#define SEND_OK_OR_ERROR_AND_RETURN(', '#define SEND_RESULT_OR_ERROR_AND_RETURN_INT(')
code += between('#define RETURN_ERROR_IF_RUNTIME_CFG_CMD(', '#define ECMC_COMMAND_FORMAT_ERROR')
code += 'static int allowNetworkHostCommands = 0;\n'
code += method('int ecmcSetAllowNetworkHostCommands(')
code += method('static int cfgCommandRequiresLocalAccess(')
code += between('static int handleCfgCommand(', '  // Fast path: jump directly to axis-set command region')
code += between('  /*int Cfg.SetAxisVel(int', '  /*int Cfg.SetAxisTweakDist(')
code += between('  /*int Cfg.AppendAxisPLCExpr(int axis_no', '  /*int Cfg.LoadAxisPLCFile(')
code += between('  /*int Cfg.LoadPlugin(int pluginId, char *cFilename, char *configString)', '  /*int Cfg.LoadCppLogic(')
code += between('  /*int Cfg.IocshCmd=', '\n  return ERROR_MAIN_PARSER_UNKOWN_CMD;')
code += '\n return ERROR_MAIN_PARSER_UNKOWN_CMD;\n}\n'
code += between('static int motorHandleOneArgWithOrigin(', '  // Fast path: jump directly to GetAxis parser region.')
code += between('  /* GetControllerError()*/', '  /* ControllerErrorReset()*/')
code += '\n return ERROR_MAIN_PARSER_UNKOWN_CMD;\n}\n'
code += method('int motorHandleOneArg(const char')
code += method('int ecmcCmdParser(const char')
code += r'''
static void expect(const char *cmd, int local, const char *reply) {
  ecmcOutputBufferType *out = getEpicsBuffer();
  clearBuffer(out);
  if (local) assert(motorHandleOneArg(cmd,out)==0);
  else assert(CMDwriteIt(cmd,strlen(cmd))==0);
  assert(strcmp(out->buffer,reply)==0);
}
int main(void) {
  die_on_error_flags=0;
  assert(allowNetworkHostCommands==0);
  char denied[64], runtime[64];
  snprintf(denied,sizeof(denied),"Error: %d",ERROR_MAIN_PARSER_CMD_LOCAL_ONLY);
  snprintf(runtime,sizeof(runtime),"Error: %d",ERROR_MAIN_APP_MODE_ALREADY_RUNTIME);
  const char *host[] = {
    "Cfg.IocshCmd=echo denied", "Cfg.LoadPlugin(0,/tmp/library.so,config)",
    "Cfg.LoadSafetyPlugin(/tmp/library.so,config)", "Cfg.LoadCppLogic(0,/tmp/library.so)",
    "Cfg.AppendCppLogicMacros(0)=FILE=/tmp/a", "Cfg.LoadPLCFile(0,/tmp/a)",
    "Cfg.LoadAxisPLCFile(0,/tmp/a)", "Cfg.LoadPLCLibFile(0,/tmp/a)",
    "Cfg.LoadLUTFile(0,/tmp/a)", "Cfg.LoadAxisEncLookupTable(0,/tmp/a)",
    "Cfg.SetPLCExpr(0)=open('/tmp/a','w')", "Cfg.AppendPLCExpr(0)=x:=1#",
    "Cfg.AppendAxisPLCExpr(0)=x:=1#", "Cfg.ClearPLCExpr(0)",
    "Cfg.CompilePLC(0)", "Cfg.CompileAxisPLC(0)", "Cfg.SimLoadRecords(TEST:)"
  };
  for (int mode=0; mode<=1; ++mode) {
    appModeStat=mode;
    for (unsigned i=0; i<sizeof(host)/sizeof(*host); ++i) {
      expect(host[i],0,denied);
      char ads[512]; snprintf(ads,sizeof(ads),"ADSPORT=501/%s",host[i]);
      expect(ads,0,denied);
    }
    expect("GetControllerError()",0,"123");
    expect("Cfg.SetAxisVel(1,2)",0,"OK");
    expect("Main.M1.bExecute=1",0,"OK");
  }
  assert(shellCalls==0 && plcCalls==0 && pluginCalls==0 && motionCalls==4);
  appModeStat=0;
  char batchReply[128]; snprintf(batchReply,sizeof(batchReply),"123;%s;OK",denied);
  expect("GetControllerError();ADSPORT=501/Cfg.IocshCmd=echo denied;Cfg.SetAxisVel(1,2)",0,batchReply);
  assert(shellCalls==0);
  expect("Cfg.IocshCmd=ecmcAllowNetworkHostCommands(1)",0,denied);
  assert(allowNetworkHostCommands==0);
  // Local startup works without enabling network host capabilities.
  expect("Cfg.IocshCmd=echo local",1,"OK");
  expect("Cfg.SetPLCExpr(0)=x:=1#",1,"OK");
  expect("Cfg.LoadPlugin(0,/tmp/lib.so,config)",1,"OK");
  assert(shellCalls==1 && plcCalls==1 && pluginCalls==1);
  assert(ecmcSetAllowNetworkHostCommands(2)==ERROR_MAIN_PARSER_INVALID_FORMAT);
  assert(allowNetworkHostCommands==0);
  assert(ecmcSetAllowNetworkHostCommands(1)==0);
  expect("Cfg.IocshCmd=echo enabled",0,"OK");
  expect("Cfg.SetPLCExpr(0)=x:=2#",0,"OK");
  expect("Cfg.LoadPlugin(0,/tmp/lib.so,config)",0,"OK");
  assert(shellCalls==2 && plcCalls==2 && pluginCalls==2);
  for (int state=iocBuilding; state<=iocPaused; ++state) {
    iocState=state;
    assert(ecmcSetAllowNetworkHostCommands(0)==ERROR_MAIN_PARSER_CMD_LOCAL_ONLY);
    assert(allowNetworkHostCommands==1);
  }
  appModeStat=1;
  for (unsigned i=0; i<sizeof(host)/sizeof(*host); ++i) expect(host[i],0,runtime);
  // Both empty and nonempty expressions obey local runtime guards as well.
  const char *expressions[] = {"Cfg.SetPLCExpr(0)=", "Cfg.SetPLCExpr(0)=x:=1#",
    "Cfg.AppendPLCExpr(0)=", "Cfg.AppendPLCExpr(0)=x:=1#",
    "Cfg.AppendAxisPLCExpr(0)=", "Cfg.AppendAxisPLCExpr(0)=x:=1#"};
  for (unsigned i=0; i<sizeof(expressions)/sizeof(*expressions); ++i)
    expect(expressions[i],1,runtime);
  assert(shellCalls==2 && plcCalls==2 && pluginCalls==2);
  iocState=iocVoid;
  assert(ecmcSetAllowNetworkHostCommands(0)==ERROR_MAIN_PARSER_CMD_LOCAL_ONLY);
  puts("MCU command policy: default denial, opt-in, local startup, runtime PLC guards, ADS and batches passed.");
}
'''

with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'asynDriver.h').write_text(
        '#include <stddef.h>\n#define ASYN_TRACE_ERROR 1\n'
        'typedef struct { int unused; } asynUser;\n'
        'static inline int asynPrint(asynUser *u, int reason, const char *fmt, ...) { return 0; }\n')
    source, binary = path / 'policy.c', path / 'policy'
    source.write_text(code)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-O1', '-g',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I' + directory, '-I' + str(ROOT / 'devEcmcSup/com'),
                    '-I' + str(ROOT / 'devEcmcSup/main'), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
