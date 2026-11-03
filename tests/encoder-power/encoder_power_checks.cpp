// Production encoder/axis/trajectory stepping with an emulated relay and BiSS
// sample. No IOC, motor record, EtherCAT master, or realtime thread is created.
#include "ecmcAxisVirt.h"
#include "ecmcDataItem.h"
#include "ecmcErrorsList.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

asynUser *pPrintOutAsynUser = nullptr;
unsigned int debug_print_flags = 0, die_on_error_flags = 0;
void ecmcRtLoggerLogError(const char *, ...) {}

static const char *scenario = "";
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(scenario)+":"+std::to_string(__LINE__)+": " #x); } while(0)

struct Rig {
  ecmcAsynPortDriver port;
  std::unique_ptr<ecmcAxisVirt> axis;
  uint32_t raw=10000, ready=0, alarm=1, relay=0, offRaw=0;
  uint32_t rawBias=0;
  int healthy=1;
  double physical=10, supplySeconds=0;
  bool activeHigh, invertedReady, neverReady=false, lostReady=false;
  bool withReady;
  ecmcDataItem position{"position"}, status{"ready"}, alarmItem{"alarm"}, output{"relay"};
  Rig(bool high=true, bool invert=false, bool readyLink=true)
    : activeHigh(high), invertedReady(invert), withReady(readyLink) {
    relay=high ? 0 : 1;
    axis=std::make_unique<ecmcAxisVirt>(&port,1,.001,ECMC_TRAPETZ);
    bind(position,raw,ECMC_ENCODER_ENTRY_INDEX_ACTUAL_POSITION,false);
    bind(output,relay,ECMC_ENCODER_ENTRY_INDEX_POWER,true,0);
    if(withReady) bind(status,ready,ECMC_ENCODER_ENTRY_INDEX_READY,false,0);
    if(withReady) bind(alarmItem,alarm,ECMC_ENCODER_ENTRY_INDEX_ALARM_0,false,0);
    auto *enc=axis->getPrimEnc();
    CHECK(enc->setBits(32)==0); CHECK(enc->setAbsBits(26)==0);
    CHECK(enc->setType(ECMC_ENCODER_TYPE_ABSOLUTE)==0);
    CHECK(enc->setScaleNum(1)==0); CHECK(enc->setScaleDenom(1000)==0);
    CHECK(enc->setInvHwReady(invert)==0);
    CHECK(enc->setPowerPolarity(high)==0);
    CHECK(enc->setPowerDelay(.05)==0);
    CHECK(enc->setPowerTimeout(.2)==0);
    CHECK(enc->setPowerEnable(true)==0);
    CHECK(axis->setEncDataSourceType(ECMC_DATA_SOURCE_INTERNAL)==0);
    axis->setAllowMotionFunctions(true,true,true);
    axis->setAcc(1); axis->setDec(1); axis->setTargetVel(.2);
    auto *mon=axis->getMon();
    mon->setLimitSwitchBwdPLCOverride(true); mon->setLimitSwitchFwdPLCOverride(true);
    mon->setHomeSwitchPLCOverride(true);
    mon->setLimitSwitchBwdPLCOverrideValue(true); mon->setLimitSwitchFwdPLCOverrideValue(true);
    mon->setHomeSwitchPLCOverrideValue(true);
    mon->setEnableLagMon(false); mon->setEnableAtTargetMon(true);
    mon->setAtTargetTol(.005); mon->setAtTargetTime(2);
    CHECK(axis->setAutoEnableTimeout(.02)==0);
    CHECK(axis->setEnableAutoEnable(true)==0);
    CHECK(axis->validate()==0);
  }
  void bind(ecmcDataItem &item, uint32_t &value, int slot, bool write, int bit=-1) {
    item.setEcmcDataPointer(reinterpret_cast<uint8_t*>(&value),sizeof(value));
    item.setEcmcDataType(ECMC_EC_U32); item.setAllowWriteToEcmc(write);
    ecmcDataAccess access;
    CHECK(ecmcDataAccess::bind(item,&healthy,0,&access));
    access.writable = write;
    CHECK(axis->getPrimEnc()->setDataAtIndex(access,slot,bit)==0);
  }
  bool powered() const { return bool(relay&1)==activeHigh; }
  void tick() {
    if(powered()) supplySeconds+=.001; else supplySeconds=0;
    const bool valid=powered() && supplySeconds>=.012 && !neverReady && !lostReady;
    ready=(invertedReady ? !valid : valid);
    // Off-state hardware alarms must not generate false idle/startup alarms.
    alarm=powered() ? 0 : 1;
    raw=powered() ? uint32_t(std::llround(physical*1000) + rawBias) : offRaw;
    axis->execute(true);
    physical+=axis->getAxisStatusDataPtr()->currentVelocitySetpoint*.001;
  }
  void ticks(int count) { for(int i=0;i<count;++i) tick(); }
  void idle() {
    ticks(50);
    CHECK(!axis->getInStartupPhase()); CHECK(!axis->getBusy());
    CHECK(!axis->getErrorID()); CHECK(!powered());
    CHECK(axis->getPrimEnc()->getPowerState()==1);
    CHECK(!axis->getEnable());
  }
  void waitMotion() {
    for(int i=0;i<300 && !axis->getExecute();++i) tick();
    CHECK(!axis->getErrorID()); CHECK(axis->getExecute()); CHECK(axis->getEnable());
    CHECK(axis->getPrimEnc()->getPowerReady());
    CHECK(supplySeconds>=.05);
  }
  void finishMove() {
    for(int i=0;i<3000 && axis->getBusy();++i) tick();
    CHECK(!axis->getErrorID()); CHECK(!axis->getBusy());
    for(int i=0;i<20 && !axis->getMon()->getAtTarget();++i) tick();
    CHECK(axis->getMon()->getAtTarget());
  }
  void sleepAfterMove() {
    CHECK(axis->getPrimEnc()->setPowerOffDelay(.08)==0);
    CHECK(axis->moveAbsolutePosition(10.2,.2,1,1)==0);
    waitMotion(); finishMove();
    CHECK(powered()); ticks(100);
    CHECK(axis->getEnable()); CHECK(axis->getEnabled()); CHECK(!powered());
    CHECK(axis->getPrimEnc()->getPowerOff()); CHECK(!axis->getErrorID());
  }
  void disable() {
    CHECK(axis->stopMotion(1)==0);
    ticks(10);
    CHECK(!axis->getEnable()); CHECK(!axis->getBusy()); CHECK(!powered());
  }
};

int main() {
  try {
    int count=0;
    for(bool high : {false,true}) for(bool inverted : {false,true}) {
      scenario="startup/ABS/polarities";
      Rig r(high,inverted); r.idle();
      CHECK(r.axis->moveAbsolutePosition(10.2,.2,1,1)==0);
      r.ticks(30);
      CHECK(!r.axis->getEnable()); CHECK(!r.axis->getExecute());
      CHECK(!r.axis->getErrorID()); CHECK(r.powered());
      r.waitMotion();
      CHECK(std::abs(r.axis->getSeq()->getTargetPos()-10.2)<.001);
      CHECK(std::abs(r.axis->getEncVelo())<.01);
      r.disable(); CHECK(!r.axis->getErrorID()); ++count;
    }
    {
      scenario="no ready input";
      Rig r(true,false,false); r.idle(); r.neverReady=true;
      CHECK(r.axis->moveAbsolutePosition(10.2,.2,1,1)==0);
      r.waitMotion(); r.disable(); ++count;
    }
    {
      scenario="relative uses fresh position";
      Rig r; r.idle(); r.physical=10.7;
      CHECK(r.axis->moveRelativePosition(.2,.2,1,1)==0);
      r.waitMotion();
      CHECK(std::abs(r.axis->getSeq()->getTargetPos()-10.9)<.001);
      CHECK(std::abs(r.axis->getAxisStatusDataPtr()->currentPositionSetpoint-10.7)<.002);
      r.disable(); ++count;
    }
    {
      scenario="ready timeout";
      Rig r; r.idle(); r.neverReady=true;
      CHECK(r.axis->moveAbsolutePosition(10.2,.2,1,1)==0);
      r.ticks(250);
      CHECK(r.axis->getErrorID()==ERROR_ENC_POWER_TIMEOUT);
      CHECK(!r.axis->getEnable()); CHECK(!r.axis->getExecute()); CHECK(!r.powered());
      CHECK(r.axis->getPrimEnc()->getPowerState()==4); ++count;
    }
    for(bool disable : {false,true}) {
      scenario="cancel warmup";
      Rig r; r.idle();
      CHECK(r.axis->moveAbsolutePosition(10.2,.2,1,1)==0);
      r.ticks(20);
      CHECK(r.axis->stopMotion(disable)==0);
      r.ticks(300);
      CHECK(!r.axis->getEnable()); CHECK(!r.axis->getExecute()); CHECK(!r.powered());
      CHECK(!r.axis->getBusy()); CHECK(!r.axis->getErrorID()); ++count;
    }
    {
      scenario="ready lost while enabled";
      Rig r; r.idle(); CHECK(r.axis->setEnable(true)==0); r.ticks(80);
      CHECK(r.axis->getEnable()); CHECK(r.powered());
      r.lostReady=true; r.ticks(5);
      CHECK(r.axis->getErrorID()==ERROR_ENC_NOT_READY);
      CHECK(!r.axis->getEnable()); CHECK(!r.powered()); ++count;
    }
    for(bool relative : {false,true}) {
      scenario="wake with amplifier already enabled";
      Rig r; r.idle(); r.sleepAfterMove();
      CHECK(r.axis->setEnableAutoEnable(false)==0);
      r.physical=10.5; r.ticks(20); // retained position cannot observe this drift
      CHECK(std::abs(r.axis->getPrimEnc()->getActPos()-10.2)<.005);
      CHECK((relative ? r.axis->moveRelativePosition(.2,.2,1,1) :
                        r.axis->moveAbsolutePosition(10.7,.2,1,1))==0);
      r.ticks(30);
      CHECK(r.axis->getEnable()); CHECK(r.axis->getBusy());
      CHECK(!r.axis->getExecute()); CHECK(r.powered());
      CHECK(std::abs(r.axis->getAxisStatusDataPtr()->currentVelocitySetpoint)<1e-12);
      r.waitMotion();
      CHECK(std::abs(r.axis->getSeq()->getTargetPos()-10.7)<.001);
      r.finishMove(); r.ticks(100); CHECK(!r.powered());
      CHECK(r.axis->getEnable()); r.disable(); ++count;
    }
    {
      scenario="atTarget loss restarts power-off delay";
      Rig r; r.idle(); CHECK(r.axis->getPrimEnc()->setPowerOffDelay(.08)==0);
      CHECK(r.axis->moveAbsolutePosition(10.2,.2,1,1)==0);
      r.waitMotion(); r.finishMove(); r.ticks(30); CHECK(r.powered());
      const double settled=r.physical; r.physical+=.02; r.ticks(60);
      CHECK(!r.axis->getMon()->getAtTarget()); CHECK(r.powered());
      r.physical=settled; r.ticks(50); CHECK(r.powered());
      r.ticks(50); CHECK(!r.powered()); CHECK(!r.axis->getErrorID()); ++count;
    }
    {
      scenario="tighter controller deadband";
      Rig r; r.idle(); CHECK(r.axis->getPrimEnc()->setPowerOffDelay(.08)==0);
      CHECK(r.axis->getMon()->setCtrlDeadband(.001)==0);
      CHECK(r.axis->moveAbsolutePosition(10.2,.2,1,1)==0);
      r.waitMotion(); r.finishMove();
      r.physical=10.203; r.ticks(100);
      CHECK(r.axis->getMon()->getAtTarget()); CHECK(r.powered());
      r.physical=10.2; r.ticks(100); CHECK(!r.powered()); ++count;
    }
    for(bool cancel : {false,true}) {
      scenario=cancel ? "STOP during wake with holding torque" : "wake ready timeout with holding torque";
      Rig r; r.idle(); r.sleepAfterMove(); r.neverReady=true;
      CHECK(r.axis->moveRelativePosition(.2,.2,1,1)==0); r.ticks(20);
      if(cancel) {
        CHECK(r.axis->stopMotion(0)==0); r.neverReady=false; r.physical=10.5;
      }
      r.ticks(cancel ? 60 : 250);
      CHECK(!r.axis->getExecute()); CHECK(!r.axis->getBusy());
      if(cancel) { CHECK(!r.axis->getErrorID()); CHECK(r.axis->getEnable());
        CHECK(std::abs(r.axis->getAxisStatusDataPtr()->currentPositionSetpoint-10.5)<.001);
        r.disable(); }
      else { CHECK(r.axis->getErrorID()==ERROR_ENC_POWER_TIMEOUT); CHECK(!r.axis->getEnable()); }
      ++count;
    }
    {
      scenario="atTarget is mandatory";
      Rig r; r.idle(); r.axis->getMon()->setEnableAtTargetMon(false);
      CHECK(r.axis->validate()==ERROR_ENC_POWER_CONFIG); ++count;
    }
    {
      scenario="explicit wake without another drive enable edge";
      Rig r; r.idle(); r.sleepAfterMove();
      CHECK(r.axis->setEnable(true)==0); r.ticks(80);
      CHECK(r.powered()); CHECK(r.axis->getPrimEnc()->getPowerReady());
      CHECK(r.axis->getEnable()); CHECK(!r.axis->getBusy());
      CHECK(!r.axis->getErrorID()); r.disable(); ++count;
    }
    {
      scenario="external source cannot bypass powered-off feedback";
      Rig r; r.idle(); r.sleepAfterMove();
      CHECK(r.axis->getSeq()->setTrajDataSourceTypeInternal(ECMC_DATA_SOURCE_EXTERNAL,1)==ERROR_AXIS_BUSY);
      CHECK(r.axis->getTrajDataSourceType()==ECMC_DATA_SOURCE_INTERNAL);
      CHECK(!r.powered()); CHECK(!r.axis->getErrorID());
      r.disable(); ++count;
    }
    {
      scenario="retarget during warmup";
      Rig r; r.idle(); r.sleepAfterMove();
      CHECK(r.axis->moveRelativePosition(.1,.2,1,1)==0); r.ticks(20);
      CHECK(r.axis->moveAbsolutePosition(10.6,.2,1,1)==0); r.ticks(10);
      CHECK(!r.axis->getExecute()); r.waitMotion();
      CHECK(std::abs(r.axis->getSeq()->getTargetPos()-10.6)<.001);
      r.disable(); ++count;
    }
    for(bool highValidWord : {false,true}) for(bool withReady : {false,true}) {
      scenario="off-state words cannot add/remove raw counter turns";
      Rig r(true,false,withReady);
      r.rawBias=highValidWord ? 0xffff0000u : 0;
      r.offRaw=highValidWord ? 0 : 0xffffffffu;
      CHECK(r.axis->getPrimEnc()->setOffset(-double(r.rawBias)/1000)==0);
      r.idle(); r.sleepAfterMove();
      auto *enc=r.axis->getPrimEnc();
      const uint64_t valid=enc->getRawPosRegister();
      CHECK(std::abs(double(valid)-double(r.offRaw)) > .7*double(0xffffffffu));
      const int64_t multiTurn=enc->getRawPosMultiTurn();
      const double position=enc->getActPos();
      for(int cycle=0;cycle<4;++cycle) {
        for(uint32_t invalid : {0u,0xffffffffu,r.offRaw}) {
          r.offRaw=invalid;
          for(int n=0;n<20;++n) {
            r.tick();
            CHECK(enc->getRawPosRegister()==valid);
            CHECK(enc->getRawPosMultiTurn()==multiTurn);
            CHECK(std::abs(enc->getActPos()-position)<.0001);
          }
        }
        CHECK(r.axis->setEnable(true)==0); r.ticks(70);
        CHECK(enc->getPowerReady()); CHECK(r.powered());
        CHECK(enc->getRawPosRegister()==valid);
        CHECK(enc->getRawPosMultiTurn()==multiTurn);
        CHECK(std::abs(enc->getActPos()-position)<.0001);
        CHECK(std::abs(enc->getActVel())<.01);
        CHECK(!r.axis->getErrorID());
        r.ticks(100); CHECK(!r.powered());
      }
      ++count;
    }
    for(bool overflow : {false,true}) {
      scenario="power cycle preserves a legitimately accumulated turn";
      Rig r;
      const double span=4294967296.0/1000;
      r.physical=overflow ? span-.04 : .04;
      r.offRaw=overflow ? 0xffffffffu : 0;
      r.idle(); CHECK(r.axis->getPrimEnc()->setPowerOffDelay(.08)==0);
      CHECK(r.axis->setEnable(true)==0); r.ticks(80);
      const double target=r.physical+(overflow ? .08 : -.08);
      CHECK(r.axis->moveAbsolutePosition(target,.2,1,1)==0);
      r.waitMotion(); r.finishMove(); r.ticks(100);
      auto *enc=r.axis->getPrimEnc();
      CHECK(enc->getPowerOff());
      const int64_t legitimate=enc->getRawPosMultiTurn();
      CHECK(overflow ? legitimate>int64_t(0xffffffffu) : legitimate<0);
      const double position=enc->getActPos();
      for(int cycle=0;cycle<3;++cycle) {
        CHECK(r.axis->setEnable(true)==0); r.ticks(70);
        CHECK(enc->getPowerReady());
        CHECK(enc->getRawPosMultiTurn()==legitimate);
        CHECK(std::abs(enc->getActPos()-position)<.0001);
        CHECK(std::abs(enc->getActPos()-r.physical)<.002);
        CHECK(!r.axis->getErrorID()); r.ticks(100); CHECK(enc->getPowerOff());
      }
      r.disable(); ++count;
    }
    std::cout<<"Encoder relay power: "<<count<<" production axis/encoder scenarios passed.\n";
  } catch(const std::exception &error) { std::cerr<<error.what()<<'\n'; return 1; }
}
