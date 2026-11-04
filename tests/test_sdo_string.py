#!/usr/bin/env python3
"""Compile the production generic SDO-string callback against fake endpoints."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'devEcmcSup/com/ecmcAsynPortDriver.cpp').read_text()
start = source.index('/** Read any VISIBLE_STRING SDO into startup environment variables. */')
callback = source[start:source.index('void ecmcAsynPortDriverRegister(void)', start)]
stubs = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <map>
struct ec_master_t {} master;
struct Ec { ec_master_t *getMaster() { return &master; } } object;
Ec *ec=&object;
struct Port { int locks=0; void lock(){++locks;} void unlock(){--locks;} } port;
Port *ecmcAsynPortObj=&port;
enum { ECMC_MODE_CONFIG, ECMC_MODE_RUNTIME };
int appModeStat=ECMC_MODE_CONFIG;
std::string payload="01.23";
int errorCode=0, reads=0; uint32_t abortValue=0;
std::map<std::string,std::string> env;
int epicsEnvSet(const char *key,const char *value) { env[key]=value; return 0; }
int ecrt_master_sdo_upload(ec_master_t *, uint16_t slave, uint16_t index, uint8_t sub,
                           uint8_t *buffer,size_t capacity,size_t *size,uint32_t *abort) {
 assert(port.locks==1 && slave==13 && index==0x1008 && sub==2); ++reads;
 *size=payload.size(); *abort=abortValue;
 if (*size<=capacity) memcpy(buffer,payload.data(),*size);
 return errorCode;
}
enum { iocshArgString, iocshArgInt };
struct iocshArg { const char *name; int type; };
struct iocshFuncDef { const char *name; int count; const iocshArg *const *args; };
struct iocshArgBuf { const char *sval=nullptr; int ival=0; };
'''
checks = r'''
int main() {
 iocshArgBuf a[5]; a[0].sval="VALUE";a[1].ival=13;a[2].ival=0x1008;a[3].ival=2;
 sdoStringCallFunc(a);
 assert(env["VALUE"]=="01.23" && env["VALUE_DB_VALUE"]=="01.23");
 assert(env["VALUE_STATUS"]=="Available" && !port.locks);
 payload=std::string(80,'x');a[4].ival=39;sdoStringCallFunc(a);
 assert(env["VALUE"].size()==39 && env["VALUE_STATUS"]=="Truncated");
 a[4].ival=0;sdoStringCallFunc(a);assert(env["VALUE"].size()==80);
 payload="01\";$(MACRO),\\ test";sdoStringCallFunc(a);
 assert(env["VALUE"]==payload && env["VALUE_DB_VALUE"]=="Unknown");
 assert(env["VALUE_STATUS"]=="Unsupported database text");
 errorCode=-1;sdoStringCallFunc(a);
 assert(env["VALUE"].empty() && env["VALUE_DB_VALUE"]=="Unknown");
 errorCode=0;abortValue=0x06020000;sdoStringCallFunc(a);
 assert(env["VALUE_STATUS"]=="Unavailable/read failed");abortValue=0;
 payload="";sdoStringCallFunc(a);assert(env["VALUE_STATUS"]=="Empty SDO value");
 payload=std::string("\1\2",2);sdoStringCallFunc(a);
 assert(env["VALUE_STATUS"]=="Non-text SDO value");
 payload=std::string("01\0ignored",10);sdoStringCallFunc(a);assert(env["VALUE"]=="01");
 payload=std::string(257,'x');sdoStringCallFunc(a);assert(env["VALUE"].empty());
 int previous=reads;a[4].ival=256;sdoStringCallFunc(a);
 assert(reads==previous && env["VALUE_STATUS"]=="Invalid arguments");
 a[4].ival=0;a[1].ival=-1;sdoStringCallFunc(a);assert(reads==previous);
 a[1].ival=13;a[2].ival=65536;sdoStringCallFunc(a);assert(reads==previous);
 a[2].ival=0x1008;a[3].ival=256;sdoStringCallFunc(a);assert(reads==previous);
 a[3].ival=2;appModeStat=ECMC_MODE_RUNTIME;sdoStringCallFunc(a);assert(reads==previous);
 appModeStat=ECMC_MODE_CONFIG;ec=nullptr;sdoStringCallFunc(a);assert(reads==previous);
 assert(!port.locks);
}
'''
with tempfile.TemporaryDirectory(prefix='ecmc-sdo-string-') as temp:
    path=Path(temp)
    (path/'check.cpp').write_text(stubs+callback+checks)
    subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror',str(path/'check.cpp'),
                    '-o',str(path/'check')],check=True)
    subprocess.run([str(path/'check')],check=True,timeout=10)
print('Generic SDO-string callback tests passed.')
