#!/usr/bin/env python3
"""Compare old/new scalar access with a fake provider; not IOC/RT validation."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', required=True, help='git revision before the common-data migration')
args = parser.parse_args()
r=Path(__file__).resolve().parents[1]; sup=r/'devEcmcSup' 
def method(s, sig):
 a=s.index(sig); b=s.index('{',a); d=1; e=b+1
 while d:
  d+=(s[e]=='{')-(s[e]=='}'); e+=1
 return s[a:e]
entry=(sup/'ethercat/ecmcEcEntry.cpp').read_text()
oldh=subprocess.check_output(['git','-C',str(r),'show',args.baseline+':devEcmcSup/ethercat/ecmcEcEntryLink.h'],text=True).replace('ecmcEcEntryLink','LegacyLink')
olds=subprocess.check_output(['git','-C',str(r),'show',args.baseline+':devEcmcSup/ethercat/ecmcEcEntryLink.cpp'],text=True).replace('ecmcEcEntryLink','LegacyLink').replace('#include "LegacyLink.h"','#include "legacy.h"')
provider='''#pragma once
#include "ecmcDataLink.h"
struct Domain { int health=1; const int *getValidityPointer()const{return &health;} };
class ecmcEcEntry {
public:
 uint64_t buffer_=123;
 int bitLength_=64, slaveId_=2;
 ecmcEcDataType dataType_=ECMC_EC_U64;
 Domain domain; Domain *domain_=&domain;
 std::shared_ptr<ecmcDataAccess> dataAccess_;
 int8_t *int8Ptr_=reinterpret_cast<int8_t *>(&buffer_);
 int16_t *int16Ptr_=reinterpret_cast<int16_t *>(&buffer_);
 int32_t *int32Ptr_=reinterpret_cast<int32_t *>(&buffer_);
 int64_t *int64Ptr_=reinterpret_cast<int64_t *>(&buffer_);
 uint64_t *uint64Ptr_=&buffer_;
 float *float32Ptr_=reinterpret_cast<float *>(&buffer_);
 double *float64Ptr_=reinterpret_cast<double *>(&buffer_);
 unsigned publications=0;
 int updateAsyn(int) { ++publications; return 0; }
 virtual int getDomainOK(){return domain.health;}
 int getBits(){return bitLength_;}
 int getSlaveId(){return slaveId_;}
 ecmcEcDataType getDataType(){return dataType_;}
 std::shared_ptr<ecmcDataAccess> getDataAccess();
 std::shared_ptr<ecmcDataAccess> getDirectDataAccess();
 int readValue(uint64_t*); int readDouble(double*); int readBit(int,uint64_t*);
 int readBits(int,int,uint64_t*); int writeValue(uint64_t); int writeDouble(double);
 int writeBit(int,uint64_t); int writeBits(int,int,uint64_t);
};
'''
impl='\n'.join(method(entry,sig) for sig in ['std::shared_ptr<ecmcDataAccess> ecmcEcEntry::getDataAccess()', 'std::shared_ptr<ecmcDataAccess> ecmcEcEntry::getDirectDataAccess()', 'int ecmcEcEntry::readValue(', 'int ecmcEcEntry::readDouble(', 'int ecmcEcEntry::readBit(', 'int ecmcEcEntry::readBits(', 'int ecmcEcEntry::writeValue(', 'int ecmcEcEntry::writeDouble(', 'int ecmcEcEntry::writeBit(', 'int ecmcEcEntry::writeBits('])
# Original bit methods use the production definitions.
impl='#include "ecmcEcEntry.h"\n'+impl
main='''#include "legacy.h"
#include <chrono>
#include <cstdio>
#include <algorithm>
#include <array>
#include <cassert>
extern app_mode_type appModeStat;
volatile uint64_t checksum;
template<typename F> double measure(F op) {
 const unsigned n=4000000;
 auto start=std::chrono::steady_clock::now();
 uint64_t sum=0;
 for(unsigned i=0;i<n;++i) sum+=op(i);
 checksum=sum;
 return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/n;
}
int main(){
 appModeStat=ECMC_MODE_RUNTIME;
 struct CustomEntry : ecmcEcEntry { int state=0; int getDomainOK() override {return state;} };
 CustomEntry custom;
 auto customAccess=custom.getDataAccess();
 assert(!customAccess->valid());
 custom.state=1; custom.domain.health=0;
 assert(customAccess->valid());
 ecmcEcEntry provider;
 LegacyLink old; ecmcDataLink now;
 old.setEntryAtIndex(&provider,0,-1); now.setDataAtIndex(&provider,0,-1);
 std::array<double,7> or_,nr,ow,nw;
 for(unsigned run=0;run<7;++run){
   or_[run]=measure([&](unsigned){uint64_t v=0;old.readEcEntryValue(0,&v);return v;});
   nr[run]=measure([&](unsigned){uint64_t v=0;now.readDataValue(0,&v);return v;});
   ow[run]=measure([&](unsigned i){old.writeEcEntryValue(0,i);return uint64_t(i);});
   nw[run]=measure([&](unsigned i){now.writeDataValue(0,i);return uint64_t(i);});
 }
 for(auto *a:{&or_,&nr,&ow,&nw})std::sort(a->begin(),a->end());
 printf("Compact slot: %zu bytes; original: %zu bytes\\n",sizeof(ecmcDataLinkInfo),sizeof(entryInfo));
 printf("Median ns/access: read old %.2f new %.2f; write old %.2f new %.2f\\n",or_[3],nr[3],ow[3],nw[3]);
 printf("This is a fake-provider access probe, not IOC or RT performance validation.\\n");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)
 (p/'ecmcEcEntry.h').write_text(provider)
 (p/'legacy.h').write_text(oldh)
 (p/'legacy.cpp').write_text(olds)
 (p/'provider.cpp').write_text(impl)
 (p/'main.cpp').write_text(main)
 cmd=[os.environ.get('CXX', 'c++'),'-std=c++17','-O2','-Wno-macro-redefined','-I'+str(p),'-I'+str(r/'tools/plc-test/offline')]
 cmd+=['-I'+str(sup/s) for s in ['main','com','motion']]
 cmd+=[str(p/f) for f in ['main.cpp','legacy.cpp','provider.cpp']]
 cmd+=[str(sup/'com/ecmcDataLink.cpp'),str(sup/'com/ecmcDataAccess.cpp'),str(sup/'com/ecmcDataItem.cpp'),str(sup/'main/ecmcError.cpp'),str(r/'tests/monitor/support.cpp'),'-o',str(p/'probe')]
 subprocess.run(cmd,check=True)
 subprocess.run([str(p/'probe')],check=True)
