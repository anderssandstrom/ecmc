#!/usr/bin/env python3
"""Compile real auxiliary-port construction/lifecycle with controlled asyn calls.

No IOC or real asyn threads: covers one ECMC initialization, independent tables,
capacity growth, collision checks, gates, sampling and callback forwarding.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'devEcmcSup/com/ecmcAsynPortDriver.cpp').read_text()

def method(signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start)+2] + '\n'

# Both real constructors (including their base initializer), and small methods.
constructors = source[source.index('ecmcAsynPortDriver::ecmcAsynPortDriver('):
                      source.index('ecmcAsynPortDriver *ecmcAsynPortDriver::createAuxiliaryPort')]
methods = ''.join(method(s) for s in (
    'ecmcAsynPortDriver *ecmcAsynPortDriver::createAuxiliaryPort(',
    'asynStatus ecmcAsynPortDriver::callParamCallbacks(',
    'void ecmcAsynPortDriver::reserveParamTable(',
    'ecmcAsynPortDriver::~ecmcAsynPortDriver()',
    'void ecmcAsynPortDriver::initVars()',
    'int ecmcAsynPortDriver::getEpicsState()',
    'void ecmcAsynPortDriver::setEpicsState(',
    'void ecmcAsynPortDriver::setAllowRtThreadCom(',
    'bool ecmcAsynPortDriver::getAllowRtThreadCom()',
    'int32_t ecmcAsynPortDriver::calcFastestUpdateRate()',
    'void ecmcAsynPortDriver::refreshAllInUseParamsRT()',
))
fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
using asynStatus = int;
constexpr int asynSuccess=0, asynError=1;
constexpr int asynInt32Mask=1, asynFloat64Mask=2, asynFloat32ArrayMask=4,
  asynFloat64ArrayMask=8, asynEnumMask=16, asynDrvUserMask=32, asynOctetMask=64,
  asynInt8ArrayMask=128, asynInt16ArrayMask=256, asynInt32ArrayMask=512,
  asynUInt32DigitalMask=1024, ASYN_CANBLOCK=1, ECMC_STACK_SIZE=1,
  asynParamNotDefined=0, ECMC_EC_NONE=0, ASYN_TRACE_ERROR=1;
constexpr const char *ECMC_ASYN_PAR_OCTET_NAME="octet";
constexpr int ECMC_TIME_BASE_ECMC=0;
const char *driverName="test";
double mcuFrequency=1000, mcuPeriod=1000000;
void asynPrint(void *, int, const char *, ...) {}
int initCalls=0;
int ecmcInit(void *) { ++initCalls; return 0; }
class asynPortDriver;
std::map<std::string, asynPortDriver *> registry;
asynPortDriver *findAsynPortDriver(const char *name) {
  auto i=registry.find(name); return i==registry.end() ? nullptr : i->second;
}
class asynPortDriver {
public:
  std::string name;
  const char *portName;
  void *pasynUserSelf=nullptr;
  int callbacks=0, result=asynSuccess;
  template<typename... Args> asynPortDriver(const char *n, Args...) : name(n),portName(name.c_str()) {
    assert(!registry.count(name)); registry[name]=this;
  }
  virtual ~asynPortDriver() { registry.erase(name); }
  virtual asynStatus callParamCallbacks(int,int) { ++callbacks; return result; }
};
class ecmcAsynDataItem {
public:
  bool linked=false, subscribed=false;
  int refreshes=0, cycles=100;
  template<typename... Args> ecmcAsynDataItem(Args...) {}
  int createParam() { return 0; }
  bool linkedToAsynClient() { return linked; }
  bool hasDataUpdatedCallbacks() { return subscribed; }
  void refreshParamRT(int) { ++refreshes; }
  int getSampleTimeCycles() { return cycles; }
};
std::vector<std::unique_ptr<ecmcAsynDataItem>> allocated;
class ecmcAsynPortDriver : public asynPortDriver {
public:
  ecmcAsynPortDriver(const char *,int,int,int,double);
  ecmcAsynPortDriver(const char *,int,int,int,double,bool);
  ~ecmcAsynPortDriver();
  ecmcAsynPortDriver *createAuxiliaryPort(const char *,int);
  void reserveParamTable(int);
  asynStatus callParamCallbacks(int,int);
  void initVars();
  int getEpicsState();
  void setEpicsState(int);
  void setAllowRtThreadCom(bool);
  bool getAllowRtThreadCom();
  int32_t calcFastestUpdateRate();
  void refreshAllInUseParamsRT();
  int appendAvailParam(ecmcAsynDataItem *p,int) {
    assert(ecmcParamAvailCount_<paramTableSize_);
    pEcmcParamAvailArray_[ecmcParamAvailCount_++]=p;
    allocated.emplace_back(p); return asynSuccess;
  }
  int appendInUseParam(ecmcAsynDataItem *p,int) {
    assert(ecmcParamInUseCount_<paramTableSize_);
    pEcmcParamInUseArray_[ecmcParamInUseCount_++]=p; return asynSuccess;
  }
  bool allowRtThreadCom_;
  ecmcAsynDataItem **pEcmcParamAvailArray_, **pEcmcParamInUseArray_;
  int ecmcParamAvailCount_,ecmcParamInUseCount_,paramTableSize_,defaultSampleTimeMS_,
      defaultMaxDelayTimeMS_,defaultTimeSource_,autoConnect_,priority_,fastestParamUpdateCycles_,epicsState_;
  std::vector<std::unique_ptr<ecmcAsynPortDriver>> auxiliaryPorts_;
};
'''
checks = r'''
int main() {
  {
    ecmcAsynPortDriver main("ECMC",8,1,7,100);
    assert(initCalls==1 && registry.size()==1);
    assert(main.auxiliaryPorts_.empty());
    main.setAllowRtThreadCom(false); main.setEpicsState(4);
    auto *sim=main.createAuxiliaryPort("ECMC_SIM0",6);
    assert(sim && initCalls==1 && main.auxiliaryPorts_.size()==1);
    assert(!sim->getAllowRtThreadCom() && sim->getEpicsState()==4);
    assert(sim->autoConnect_==1 && sim->priority_==7 && sim->defaultSampleTimeMS_==100);
    assert(main.paramTableSize_==8 && main.ecmcParamAvailCount_==1);
    auto *item=new ecmcAsynDataItem;
    item->linked=true; item->cycles=10;
    sim->appendAvailParam(item,0); sim->appendInUseParam(item,0);
    assert(main.createAuxiliaryPort("ECMC_SIM0",32)==sim);
    assert(sim->paramTableSize_==32 && sim->ecmcParamAvailCount_==2 && sim->ecmcParamInUseCount_==2);
    assert(sim->pEcmcParamAvailArray_[1]==item && sim->pEcmcParamInUseArray_[1]==item);
    assert(sim->pEcmcParamAvailArray_[31]==nullptr && sim->pEcmcParamInUseArray_[31]==nullptr);
    assert(main.createAuxiliaryPort("ECMC_SIM0",3)==sim && sim->paramTableSize_==32);
    asynPortDriver occupied("occupied");
    assert(!main.createAuxiliaryPort("occupied",20));
    assert(!main.createAuxiliaryPort(nullptr,20));
    assert(!main.createAuxiliaryPort("",20));
    assert(!main.createAuxiliaryPort("bad",0));
    main.setAllowRtThreadCom(true); main.setEpicsState(5);
    assert(sim->getAllowRtThreadCom() && sim->getEpicsState()==5);
    auto *second=main.createAuxiliaryPort("ECMC_SIM1",12);
    assert(second->getAllowRtThreadCom() && second->getEpicsState()==5);
    assert(initCalls==1 && main.calcFastestUpdateRate()==10);
    assert(main.callParamCallbacks(0,0)==asynSuccess);
    assert(main.callbacks==1 && sim->callbacks==1 && second->callbacks==1);
    sim->result=asynError;
    assert(main.callParamCallbacks(0,0)==asynError && second->callbacks==2);
    main.refreshAllInUseParamsRT(); assert(item->refreshes==1);
    main.setAllowRtThreadCom(false); main.setEpicsState(6);
    assert(!sim->getAllowRtThreadCom() && !second->getAllowRtThreadCom());
    assert(sim->getEpicsState()==6 && second->getEpicsState()==6);
  }
  assert(registry.empty() && initCalls==1);
}
'''
with tempfile.TemporaryDirectory(prefix='ecmc-aux-port-') as directory:
    cpp, exe = Path(directory)/'aux.cpp', Path(directory)/'aux'
    cpp.write_text(fixture + constructors + methods + checks)
    subprocess.run([os.environ.get('CXX','c++'), '-std=c++17','-O2','-Wall','-Wextra','-Werror',
                    str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('Auxiliary ports: single ECMC init, independent/growing tables, name collision, inherited gates, sampling and callback forwarding passed.')
