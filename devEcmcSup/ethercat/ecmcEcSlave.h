/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcEcSlave.h
*
*  Created on: Nov 30, 2015
*      Author: anderssandstrom
*
\*************************************************************************/


#ifndef ECMCECSLAVE_H_
#define ECMCECSLAVE_H_
#ifdef __cplusplus
/**
 * @file ecmcEcSlave.h
 * @brief Represents a configured EtherCAT slave with entries, memmaps, and SDO helpers.
 */
#endif

#include <string>
#include <vector>
#include "stdio.h"
#include "ecrt.h"
#include "ecmcDefinitions.h"
#include "ecmcError.h"
#include "ecmcOctetIF.h"  // Logging macros
#include "ecmcAsynPortDriver.h"
#include "ecmcAsynDataItem.h"
#include "ecmcEcEntry.h"
#include "ecmcEcMemMap.h"
#include "ecmcEcSDO.h"
#include "ecmcEcAsyncSDO.h"
#include "ecmcEcDomain.h"
#include "ecmcEcData.h"

#define SIMULATION_ENTRIES 32

// ECSLAVE ERRORS
#define ERROR_EC_SLAVE_CONFIG_FAILED 0x24000
#define ERROR_EC_SLAVE_CALL_NOT_ALLOWED_IN_SIM_MODE 0x24001
#define ERROR_EC_SLAVE_SM_ARRAY_FULL 0x24002
#define ERROR_EC_SLAVE_SM_INDEX_OUT_OF_RANGE 0x24003
#define ERROR_EC_SLAVE_ENTRY_INFO_STRUCT_NULL 0x24004
#define ERROR_EC_SLAVE_ENTRY_INDEX_OUT_OF_RANGE 0x24005
#define ERROR_EC_SLAVE_SLAVE_INFO_STRUCT_NULL 0x24006
#define ERROR_EC_SLAVE_CONFIG_PDOS_FAILED 0x24007
#define ERROR_EC_SLAVE_ENTRY_NULL 0x24008
#define ERROR_EC_SLAVE_STATE_CHANGED 0x24009
#define ERROR_EC_SLAVE_ONLINE_OFFLINE_CHANGED 0x2400A
#define ERROR_EC_SLAVE_OPERATIONAL_CHANGED 0x2400B
#define ERROR_EC_SLAVE_CONFIG_NULL 0x2400C
#define ERROR_EC_SLAVE_STATE_INIT 0x2400D
#define ERROR_EC_SLAVE_STATE_PREOP 0x2400E
#define ERROR_EC_SLAVE_STATE_SAFEOP 0x2400F
#define ERROR_EC_SLAVE_STATE_UNDEFINED 0x24010
#define ERROR_EC_SLAVE_NOT_OPERATIONAL 0x24011
#define ERROR_EC_SLAVE_NOT_ONLINE 0x24012
#define ERROR_EC_SLAVE_REG_ASYN_PAR_BUFFER_OVERFLOW 0x24013
#define ERROR_EC_SLAVE_SDO_ASYNC_CREATE_FAIL 0x24014
#define ERROR_EC_SLAVE_ADD_DATA_ITEM_FAIL 0x24015
#define ERROR_EC_SLAVE_SDO_SETTINGS_MISSING 0x24016
#define ERROR_EC_SLAVE_SDO_CH_ID_OUT_OF_RANGE 0x24017

typedef struct {
  int needSdo; 
  int sdosDone;  
} sdoVerifyChX;

#define SDO_SETTINGS_MAX_CH 256

typedef struct {
  uint16_t position;   /**< Offset of the slave in the ring. */
  uint32_t vendor_id;   /**< Vendor-ID stored on the slave. */
  uint32_t product_code;   /**< Product-Code stored on the slave. */
  uint16_t alias;   /**< The slaves alias if not equal to 0. */
} mcu_ec_slave_info_light;

/**
 * @brief Encapsulates EtherCAT slave configuration and attached data objects.
 */
class ecmcEcSlave : public ecmcError {
public:
  ecmcEcSlave(
    ecmcAsynPortDriver *asynPortDriver,  /** Asyn port driver*/
    int                 masterId,
    ec_master_t        *master, /**< EtherCAT master */
    ecmcEcDomain       *domain,
    uint16_t            alias, /**< Slave alias. */
    int32_t             position, /**< Slave position. */
    uint32_t            vendorId, /**< Expected vendor ID. */
    uint32_t            productCode /**< Expected product code. */);
  ~ecmcEcSlave();
  /** @brief Register a sync manager on this slave. */
  int                addSyncManager(ec_direction_t direction,
                                    uint8_t        syncMangerIndex);
  /** @brief Populate lightweight slave info struct. */
  int                getSlaveInfo(mcu_ec_slave_info_light *info);
  /** @brief Number of configured entries. */
  int                getEntryCount();
  /** @brief Get entry by index. */
  ecmcEcEntry*       getEntry(int entryIndex);
  /** @brief Check configuration state of the slave. */
  int                checkConfigState(void);
  /** @brief Set base address for domain data. */
  void               setDomainBaseAdr(uint8_t *domainAdr);
  /** @brief Update all input entries from process image. */
  int                updateInputProcessImage();
  /** @brief Update all output entries into process image. */
  int                updateOutProcessImage();
  /** @brief Bus position of this slave. */
  int                getSlaveBusPosition();
  int                addEntry(
    ec_direction_t direction,
    uint8_t        syncMangerIndex,
    uint16_t       pdoIndex,
    uint16_t       entryIndex,
    uint8_t        entrySubIndex,
    ecmcEcDataType dt,
    std::string    id,
    int            useInRealTime);
  int addSimEntry(std::string    id,
                  ecmcEcDataType dt,
                  uint64_t       value);
  int addDataItem(
    ecmcEcEntry   *startEntry,
    size_t         entryByteOffset,
    size_t         entryBitOffset,
    ec_direction_t direction,
    ecmcEcDataType dt,
    std::string    id);
  int configDC(

    // AssignActivate word.
    uint16_t assignActivate,

    // SYNC0 cycle time [ns].
    uint32_t sync0Cycle,

    // SYNC0 shift time [ns].
    int32_t sync0Shift,

    // SYNC1 cycle time [ns].
    uint32_t sync1Ccycle,

    // SYNC1 shift time [ns].
    int32_t sync1Shift);
  ecmcEcEntry* findEntry(std::string id);
  int          findEntryIndex(std::string id);
  int          selectAsReferenceDC();
  int          setWatchDogConfig(

    // Number of 40 ns intervals. Used as a base unit for all slave watchdogs.
    // If set to zero, the value is not written, so the default is used.
    uint16_t watchdogDivider,

    // Number of base intervals for process  data watchdog.
    // If set to zero, the value is not written, so the default is used.
    uint16_t watchdogIntervals);

  // accepts up to 4 bytes ints
  int addSDOWrite(uint16_t sdoIndex,
                  uint8_t  sdoSubIndex,
                  uint32_t writeValue,
                  int      byteSize);

  // accepts up to 8 bytes
  int addSDOWriteDT(uint16_t       sdoIndex,
                    uint8_t        sdoSubIndex,
                    const char    *value,
                    ecmcEcDataType dt);
  int addSDOWriteComplete(uint16_t    sdoIndex,
                          const char *dataBuffer,
                          int         byteSize);

  int addSDOWriteBuffer(uint16_t    sdoIndex,
                        uint8_t     sdoSubIndex,
                        const char *dataBuffer,
                        int         byteSize);
  int getSlaveState(ec_slave_config_state_t *state);
  int activate();
  int compileRegInfo();
  int validate();
  int addSDOAsync(uint16_t       sdoIndex, /**< SDO index. */
                  uint8_t        sdoSubIndex, /**< SDO subindex. */
                  ecmcEcDataType dt,
                  std::string    alias);
  int getAllowOffline();

/*
 * SDO safety guard:
 * - ecmccfg.addSlave.cmd calls setNeedSDOSettings() to mark that required SDOs must be set.
 * - configureSlave.cmd/applySlaveConfig.cmd/ecmccomp.applyComponent.cmd call setSDOSettingsDone()
 *   after successful SDO configuration for the channel.
 * - Validation before runtime enforces that required SDO settings were applied; otherwise ecmc exits.
 *   The check only applies when the drive terminal is used in an ecmc motion axis.
 */
  int setNeedSDOSettings(int chid, int need);
  int setSDOSettingsDone(int chid, int done);
  int setEnableSDOCheck(int enable);

private:
  void               initVars();
  int                initAsyn();
  struct SyncManager {
    ec_direction_t direction;
    uint8_t        index;
    std::vector<ecmcEcEntry *> entries;
    std::vector<std::string>   entryIds;
  };
  SyncManager* findSyncManNew(uint8_t syncMangerIndex);
  ecmcEcEntry* findEntryById(const std::string& id);
  ecmcEcEntry* addEntryToNew(uint8_t        syncMangerIndex,
                             uint16_t       pdoIndex,
                             uint16_t       entryIndex,
                             uint8_t        entrySubIndex,
                             ecmcEcDataType dt,
                             std::string    id,
                             int            useInRealTime,
                             int           *errorCode);
  ec_master_t *master_;     // EtherCAT master
  uint16_t alias_;          // Slave alias.
  int32_t slavePosition_;   // Slave position.
  uint32_t vendorId_;       // Expected vendor ID.
  uint32_t productCode_;    // Expected product code.
  ec_slave_config_t *slaveConfig_;
  ec_slave_config_state_t slaveState_;
  ec_slave_config_state_t slaveStateOld_;

  // used to simulate endswitches
  bool simSlave_;
  uint64_t simBuffer_[SIMULATION_ENTRIES];
  ecmcEcEntry*  simEntries_[SIMULATION_ENTRIES];
  ecmcEcDomain *domain_;
  ecmcAsynPortDriver *asynPortDriver_;
  ecmcAsynDataItem *slaveAsynParams_[ECMC_ASYN_EC_SLAVE_PAR_COUNT];
  int masterId_;
  std::vector<SyncManager> newSyncManagers_;
  std::vector<std::pair<std::string, ecmcEcEntry *>> entryLookup_;
  bool entryLookupSorted_ = false;
  std::vector<ecmcEcEntry *> flatEntries_;
  std::vector<std::string>    flatEntryIds_;
  std::vector<ecmcEcEntry *> extraEntries_;

  /**
   * Status word bits:
   *  - bit 0   online: slave detected
   *  - bit 1   init operational: slave entered OP state
   *  - bits 2..5 al_state: application-layer state
   *      - 1: INIT
   *      - 2: PREOP
   *      - 4: SAFEOP
   *      - 8: OP
   *  - bits 16..31: entry counter
   */
  uint32_t statusWord_;
  uint32_t statusWordOld_;

  std::vector<ecmcEcAsyncSDO *>asyncSDOvector_;
  int asyncSDOCounter_;
  
  // Ensure important SDO settings like max current are set.
  std::vector<sdoVerifyChX> sdoChVerify_;
  bool enableSDOCheck_;
  size_t simEntryCounter_;
};
#endif  /* ECMCECSLAVE_H_ */
