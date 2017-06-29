/*
 * ecmcEc.h
 *
 *  Created on: Dec 1, 2015
 *      Author: anderssandstrom
 */

#ifndef ECMCEC_H_
#define ECMCEC_H_
#include "ecrt.h"
#include "stdio.h"
#include <cmath>
#include <string>
#include <time.h>

#include "ecmcDefinitions.h"
#include "ecmcEcEntry.h"
#include "ecmcEcSDO.h"
#include "ecmcEcSlave.h"
#include "ecmcError.h"
#include "cmd.h" //Logging macros
#include "ecmcAsynPortDriver.h"
#include "ecmcEcMemMap.h"

//EC ERRORS
#define ERROR_EC_MAIN_REQUEST_FAILED 0x26000
#define ERROR_EC_MAIN_CREATE_DOMAIN_FAILED 0x26001
#define ERROR_EC_MAIN_INVALID_SLAVE_INDEX 0x26002
#define ERROR_EC_MAIN_MASTER_ACTIVATE_FAILED 0x26003
#define ERROR_EC_MAIN_SLAVE_NULL 0x26004
#define ERROR_EC_MAIN_GET_SLAVE_INFO_FAILED 0x26005
#define ERROR_EC_MAIN_ENTRY_NULL 0x26006
#define ERROR_EC_MAIN_GET_ENTRY_INFO_FAILED 0x26007
#define ERROR_EC_MAIN_DOM_REG_PDO_ENTRY_LIST_FAILED 0x26008
#define ERROR_EC_MAIN_SDO_ARRAY_FULL 0x26009
#define ERROR_EC_MAIN_SDO_ENTRY_NULL 0x2600A
#define ERROR_EC_MAIN_SDO_READ_FAILED 0x2600B
#define ERROR_EC_MAIN_DOMAIN_DATA_FAILED 0x2600C
#define ERROR_EC_MAIN_SLAVE_ARRAY_FULL 0x2600D
#define ERROR_EC_AL_STATE_INIT 0x2600E
#define ERROR_EC_AL_STATE_PREOP 0x2600F
#define ERROR_EC_AL_STATE_SAFEOP 0x26010
#define ERROR_EC_LINK_DOWN 0x26011
#define ERROR_EC_RESPOND_VS_CONFIG_SLAVES_MISSMATCH 0x26012
#define ERROR_EC_STATUS_NOT_OK 0x26013
#define ERROR_EC_ALIAS_TO_LONG 0x26014
#define ERROR_EC_ASYN_PORT_OBJ_NULL 0x26015
#define ERROR_EC_ASYN_PORT_CREATE_PARAM_FAIL 0x26016
#define ERROR_EC_ASYN_SKIP_CYCLES_INVALID 0x26017
#define ERROR_EC_MEM_MAP_INDEX_OUT_OF_RANGE 0x26018
#define ERROR_EC_MEM_MAP_START_ENTRY_NULL 0x26019
#define ERROR_EC_MEM_MAP_NULL 0x2601A
#define ERROR_EC_ASYN_ALIAS_NOT_VALID 0x2601B

class ecmcEc : public ecmcError
{
public:
  ecmcEc();
  ~ecmcEc();
  int init(int nMasterIndex);
  //void setMaster(ec_master_t *master);
  int addSlave(
      uint16_t alias, /**< Slave alias. */
      uint16_t position, /**< Slave position. */
      uint32_t vendorId, /**< Expected vendor ID. */
      uint32_t productCode /**< Expected product code. */);
  ecmcEcSlave *getSlave(int slave); //NOTE: index not bus position
  ec_domain_t *getDomain();
  ec_master_t *getMaster();
  bool getInitDone();
  void receive();
  void send(timespec timeOffset);
  int compileRegInfo();
  void checkDomainState();
  int checkSlaveConfState(int slave);
  bool checkSlavesConfState();
  bool checkState();
  int activate();
  int setDiagnostics(bool diag);
  int addSDOWrite(uint16_t slavePosition,uint16_t sdoIndex,uint8_t sdoSubIndex,uint32_t value, int byteSize);
  int writeAndVerifySDOs();
  uint32_t readSDO(uint16_t slavePosition,uint16_t sdoIndex,uint8_t sdoSubIndex, int byteSize);
  int writeSDO(uint16_t slavePosition,uint16_t sdoIndex,uint8_t sdoSubIndex,uint32_t value, int byteSize);
  int addEntry(
      uint16_t       position, /**< Slave position. */
      uint32_t       vendorId, /**< Expected vendor ID. */
      uint32_t       productCode, /**< Expected product code. */
      ec_direction_t direction,
      uint8_t        syncMangerIndex,
      uint16_t       pdoIndex,
      uint16_t       entryIndex,
      uint8_t        entrySubIndex,
      uint8_t        bits,
      std::string    id
  );
  int addMemMap(uint16_t startEntryBusPosition,
		    std::string startEntryIDString,
		    int byteSize,
		    int type,
		    ec_direction_t direction,
		    std::string memMapIDString);
  ecmcEcMemMap *findMemMap(std::string id);
  ecmcEcSlave *findSlave(int busPosition);
  int findSlaveIndex(int busPosition,int *slaveIndex);
  int updateTime();
  int printTimingInformation();
  int statusOK();
  int setDomainFailedCyclesLimitInterlock(int cycles);
  void printStatus();
  int reset();
  int linkEcEntryToAsynParameter(void* asynPortObject, const char *entryIDString, int asynParType,int skipCycles);
  int linkEcMemMapToAsynParameter(void* asynPortObject, const char *memMapIDString, int asynParType,int skipCycles);
  int setEcStatusOutputEntry(ecmcEcEntry *entry);
  int setAsynPort(ecmcAsynPortDriver* asynPortDriver);
private:
  void initVars();
  int updateInputProcessImage();
  int updateOutProcessImage();
  timespec timespecAdd(timespec time1, timespec time2);

  ec_master_t *master_; /**< EtherCAT master */
  ec_domain_t *domain_;
  ec_domain_state_t domainStateOld_;
  ec_domain_state_t domainState_;
  ec_master_state_t masterStateOld_;
  ec_master_state_t masterState_;
  uint8_t *domainPd_ ;
  int slaveCounter_;
  //int sdoCounter_;
  ecmcEcSlave *slaveArray_[EC_MAX_SLAVES];
  ec_pdo_entry_reg_t slaveEntriesReg_[EC_MAX_ENTRIES];
  unsigned int pdoByteOffsetArray_[EC_MAX_ENTRIES];
  unsigned int pdoBitOffsetArray_[EC_MAX_ENTRIES];
  //ecmcEcSDO *sdoArray_[EC_MAX_ENTRIES];
  bool initDone_;
  bool diag_;
  ecmcEcSlave *simSlave_;
  int slavesOK_;
  int masterOK_;
  int domainOK_;
  int domainNotOKCounter_;
  int domainNotOKCounterMax_;
  int domainNotOKCyclesLimit_;
  bool inStartupPhase_;
  ecmcAsynPortDriver *asynPortDriver_;
  ecmcEcMemMap *ecMemMapArray_[EC_MAX_MEM_MAPS];
  int ecMemMapArrayCounter_;
  size_t domainSize_;
  ecmcEcEntry *statusOutputEntry_;

};
#endif /* ECMCEC_H_ */
