/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcEcSlave.cpp
*
*  Created on: Nov 30, 2015
*      Author: anderssandstrom
*
\*************************************************************************/

#include "ecmcEcSlave.h"
#include "ecmcErrorsList.h"

ecmcEcSlave::ecmcEcSlave(
  ecmcAsynPortDriver *asynPortDriver,  /** Asyn port driver*/
  int                 masterId,
  ec_master_t        *master, /**< EtherCAT master */
  ecmcEcDomain       *domain, /** <Domain> */
  uint16_t            alias, /**< Slave alias. */
  int32_t             position, /**< Slave position. */
  uint32_t            vendorId, /**< Expected vendor ID. */
  uint32_t            productCode /**< Expected product code. */) {
  initVars();

  asynPortDriver_ = asynPortDriver;
  masterId_       = masterId;
  master_         = master;
  alias_          = alias; /**< Slave alias. */
  slavePosition_  = position;  /**< Slave position. */
  vendorId_       = vendorId; /**< Expected vendor ID. */
  productCode_    = productCode; /**< Expected product code. */
  
  // Simulation entries, two 32 bit entries
  addSimEntry("ZERO",ECMC_EC_U32,0);
  addSimEntry("ONE",ECMC_EC_U32,0xFFFFFFFF);

  if ((alias == 0) && (position == -1) && (vendorId == 0) &&
      (productCode == 0)) {
    simSlave_ = true;
    initAsyn();
    return;
  }

  domain_ = domain;

  if (!(slaveConfig_ =
          ecrt_master_slave_config(master_, alias_, slavePosition_, vendorId_,
                                   productCode_))) {
    LOGERR(
      "%s/%s:%d: ERROR: Slave %d (0x%x,0x%x): Failed to get slave configuration (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      slavePosition_,
      vendorId_,
      productCode_,
      ERROR_EC_SLAVE_CONFIG_FAILED);
    setErrorID(__FILE__, __FUNCTION__, __LINE__, ERROR_EC_SLAVE_CONFIG_FAILED);
  }
  LOGINFO5(
    "%s/%s:%d: INFO: Slave %d created: alias %d, vendorId 0x%x, productCode 0x%x.\n",
    __FILE__,
    __FUNCTION__,
    __LINE__,
    slavePosition_,
    alias_,
    vendorId_,
    productCode_);

  initAsyn();
}

void ecmcEcSlave::initVars() {
  errorReset();
  masterId_          = -1;
  simSlave_          = false;
  master_            = NULL;
  alias_             = 0;  // Slave alias.
  slavePosition_     = 0;  // Slave position.
  vendorId_          = 0;  // Expected vendor ID.
  productCode_       = 0;  // Expected product code.
  slaveConfig_       = NULL;
  statusWord_        = 0;
  statusWordOld_     = 0;
  asyncSDOCounter_   = 0;
  enableSDOCheck_    = 0;
  simEntryCounter_   = 0;
  entryLookupSorted_ = false;
  for (int i = 0; i < SIMULATION_ENTRIES; i++) {
    simEntries_[i] = NULL;
    simBuffer_[i] = 0;
  }

  for (int i = 0; i < ECMC_ASYN_EC_SLAVE_PAR_COUNT; i++) {
    slaveAsynParams_[i] = NULL;
  }

  domain_ = NULL;
  memset(&slaveState_,    0, sizeof(slaveState_));
  memset(&slaveStateOld_, 0, sizeof(slaveStateOld_));
  asynPortDriver_ = NULL;
}

ecmcEcSlave::~ecmcEcSlave() {
  for (size_t i = 0; i < newSyncManagers_.size(); i++) {
    newSyncManagers_[i].entries.clear();
  }
  newSyncManagers_.clear();
  entryLookup_.clear();
  entryLookupSorted_ = false;
  extraEntries_.clear();

  for (uint i = 0; i < simEntryCounter_; i++) {
    if (simEntries_[i] != NULL) {
      delete simEntries_[i];
    }
    simEntries_[i] = NULL;
  }

  for (int i = 0; i < ECMC_ASYN_EC_SLAVE_PAR_COUNT; i++) {
    delete slaveAsynParams_[i];
    slaveAsynParams_[i] = NULL;
  }

  for (int i = 0; i < asyncSDOCounter_; i++) {
    delete asyncSDOvector_[i];
  }
}

int ecmcEcSlave::getEntryCount() {
  size_t count = 0;
  for (size_t sm = 0; sm < newSyncManagers_.size(); sm++) {
    count += newSyncManagers_[sm].entries.size();
  }
  count += extraEntries_.size();
  for (size_t i = 0; i < SIMULATION_ENTRIES; i++) {
    if (simEntries_[i]) {
      count++;
    }
  }
  return static_cast<int>(count);
}

int ecmcEcSlave::addSyncManager(ec_direction_t direction,
                                uint8_t        syncMangerIndex) {
  if (simSlave_) {
    LOGERR(
      "%s/%s:%d: ERROR: Slave %d (0x%x,0x%x): Simulation slave: Functionality not supported (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      slavePosition_,
      vendorId_,
      productCode_,
      ERROR_EC_SLAVE_CALL_NOT_ALLOWED_IN_SIM_MODE);
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SLAVE_CALL_NOT_ALLOWED_IN_SIM_MODE);
  }

  // Ensure new storage has a sync manager entry
  if (!findSyncMan(syncMangerIndex)) {
    newSyncManagers_.push_back(SyncManager());
    SyncManager &sm = newSyncManagers_.back();
    sm.index        = syncMangerIndex;
    sm.direction    = direction;
  } else {
    SyncManager *sm = findSyncMan(syncMangerIndex);
    if (sm && sm->direction == EC_DIR_INVALID) {
      sm->direction = direction;
    }
  }
  return 0;
}

ecmcEcSyncManager * ecmcEcSlave::getSyncManager(int) {
  return NULL;
}

int ecmcEcSlave::getSlaveInfo(mcu_ec_slave_info_light *info) {
  if (info == NULL) {
    LOGERR(
      "%s/%s:%d: ERROR: Slave %d (0x%x,0x%x): Slave Info structure NULL (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      slavePosition_,
      vendorId_,
      productCode_,
      ERROR_EC_SLAVE_SLAVE_INFO_STRUCT_NULL);
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SLAVE_SLAVE_INFO_STRUCT_NULL);
  }
  info->alias        = alias_;
  info->position     = slavePosition_;
  info->product_code = productCode_;
  info->vendor_id    = vendorId_;
  return 0;
}

int ecmcEcSlave::checkConfigState(void) {
  if (simSlave_) {
    LOGERR(
      "%s/%s:%d: ERROR: Slave %d (0x%x,0x%x): Simulation slave: Functionality not supported (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      slavePosition_,
      vendorId_,
      productCode_,
      ERROR_EC_SLAVE_CALL_NOT_ALLOWED_IN_SIM_MODE);
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SLAVE_CALL_NOT_ALLOWED_IN_SIM_MODE);
  }

  memset(&slaveState_, 0, sizeof(slaveState_));
  ecrt_slave_config_state(slaveConfig_, &slaveState_);

  // Update status word
  //  lower 16  : status bits
  //  higher 16 : entrycounter

  statusWord_ = 0;
  statusWord_ = statusWord_ + (slaveState_.online);
  statusWord_ = statusWord_ + (slaveState_.operational << 1);
  statusWord_ = statusWord_ + (slaveState_.al_state << 2);
  statusWord_ = statusWord_ + (getEntryCount() << 16);

  if (statusWord_ != statusWordOld_) {
    slaveAsynParams_[ECMC_ASYN_EC_SLAVE_PAR_STATUS_ID]->refreshParamRT(1);
  }
  statusWordOld_ = statusWord_;

  if (slaveState_.al_state != slaveStateOld_.al_state) {
    LOGINFO5("%s/%s:%d: INFO: Slave position: %d. State 0x%x.\n",
             __FILE__,
             __FUNCTION__,
             __LINE__,
             slavePosition_,
             slaveState_.al_state);
  }

  bool updateAlarmState = false;

  if (slaveState_.online != slaveStateOld_.online) {
    LOGINFO5("%s/%s:%d: INFO: Slave position: %d %s.\n",
             __FILE__,
             __FUNCTION__,
             __LINE__,
             slavePosition_,
             slaveState_.online ? "Online" : "Offline");

    // Status changed.. Update alarm status
    updateAlarmState = true;
  }

  if (slaveState_.operational != slaveStateOld_.operational) {
    LOGINFO5("%s/%s:%d: INFO: Slave position: %d %s operational.\n",
             __FILE__,
             __FUNCTION__,
             __LINE__,
             slavePosition_,
             slaveState_.operational ? "" : "Not ");

    // Status changed.. Update alarm status
    updateAlarmState = true;
  }

  // Alarm state
  if (updateAlarmState) {
    for (size_t sm = 0; sm < newSyncManagers_.size(); sm++) {
      for (size_t j = 0; j < newSyncManagers_[sm].entries.size(); j++) {
        ecmcEcEntry *entry = newSyncManagers_[sm].entries[j];
        if (entry) {
          entry->setComAlarm((!slaveState_.online || !slaveState_.operational));
        }
      }
    }
  }

  slaveStateOld_ = slaveState_;

  if (!slaveState_.online) {
    if (getErrorID() != ERROR_EC_SLAVE_NOT_ONLINE) {
      LOGERR("%s/%s:%d: ERROR: Slave %d: Not online (0x%x).\n",
             __FILE__,
             __FUNCTION__,
             __LINE__,
             slavePosition_,
             ERROR_EC_SLAVE_NOT_ONLINE);
    }

    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SLAVE_NOT_ONLINE);
  }

  if (!slaveState_.operational) {
    if (getErrorID() != ERROR_EC_SLAVE_NOT_OPERATIONAL) {
      LOGERR("%s/%s:%d: ERROR: Slave %d: Not operational (0x%x).\n",
             __FILE__,
             __FUNCTION__,
             __LINE__,
             slavePosition_,
             ERROR_EC_SLAVE_NOT_OPERATIONAL);
    }

    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SLAVE_NOT_OPERATIONAL);
  }


  switch (slaveState_.al_state) {
  case 1:

    if (getErrorID() != ERROR_EC_SLAVE_STATE_INIT) {
      LOGERR("%s/%s:%d: ERROR: Slave %d: State INIT (0x%x).\n",
             __FILE__,
             __FUNCTION__,
             __LINE__,
             slavePosition_,
             ERROR_EC_SLAVE_STATE_INIT);
    }
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SLAVE_STATE_INIT);

    break;

  case 2:

    if (getErrorID() != ERROR_EC_SLAVE_STATE_PREOP) {
      LOGERR("%s/%s:%d: ERROR: Slave %d: State PREOP (0x%x).\n",
             __FILE__,
             __FUNCTION__,
             __LINE__,
             slavePosition_,
             ERROR_EC_SLAVE_STATE_PREOP);
    }
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SLAVE_STATE_PREOP);

    break;

  case 4:

    if (getErrorID() != ERROR_EC_SLAVE_STATE_SAFEOP) {
      LOGERR("%s/%s:%d: ERROR: Slave %d: State SAFEOP (0x%x).\n",
             __FILE__,
             __FUNCTION__,
             __LINE__,
             slavePosition_,
             ERROR_EC_SLAVE_STATE_SAFEOP);
    }
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SLAVE_STATE_SAFEOP);

    break;

  case 8:

    // OK
    return 0;

    break;

  default:

    if (getErrorID() != ERROR_EC_SLAVE_STATE_UNDEFINED) {
      LOGERR("%s/%s:%d: ERROR: Slave %d: State UNDEFINED (0x%x).\n",
             __FILE__,
             __FUNCTION__,
             __LINE__,
             slavePosition_,
             ERROR_EC_SLAVE_STATE_UNDEFINED);
    }
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SLAVE_STATE_UNDEFINED);

    break;
  }

  return 0;
}

ecmcEcEntry * ecmcEcSlave::getEntry(int entryIndex) {
  if (simSlave_) {
    if (entryIndex >= SIMULATION_ENTRIES) {
      LOGERR(
        "%s/%s:%d: ERROR: Slave %d (0x%x,0x%x): Entry index out of range (0x%x).\n",
        __FILE__,
        __FUNCTION__,
        __LINE__,
        slavePosition_,
        vendorId_,
        productCode_,
        ERROR_EC_SLAVE_ENTRY_INDEX_OUT_OF_RANGE);
      setErrorID(__FILE__,
                 __FUNCTION__,
                 __LINE__,
                 ERROR_EC_SLAVE_ENTRY_INDEX_OUT_OF_RANGE);
      return NULL;
    }

    if (simEntries_[entryIndex] == NULL) {
      LOGERR("%s/%s:%d: ERROR: Slave %d (0x%x,0x%x): Entry NULL (0x%x).\n",
             __FILE__,
             __FUNCTION__,
             __LINE__,
             slavePosition_,
             vendorId_,
             productCode_,
             ERROR_EC_SLAVE_ENTRY_NULL);
      setErrorID(__FILE__, __FUNCTION__, __LINE__, ERROR_EC_SLAVE_ENTRY_NULL);
      return NULL;
    }
    return simEntries_[entryIndex];
  }

  // New storage: flatten entries across sync managers
  size_t flatIndex = 0;
  for (size_t sm = 0; sm < newSyncManagers_.size(); sm++) {
    for (size_t j = 0; j < newSyncManagers_[sm].entries.size(); j++) {
      if (flatIndex == static_cast<size_t>(entryIndex)) {
        return newSyncManagers_[sm].entries[j];
      }
      flatIndex++;
    }
  }

  LOGERR("%s/%s:%d: ERROR: Slave %d (0x%x,0x%x): Entry index out of range (0x%x).\n",
         __FILE__,
         __FUNCTION__,
         __LINE__,
         slavePosition_,
         vendorId_,
         productCode_,
         ERROR_EC_SLAVE_ENTRY_INDEX_OUT_OF_RANGE);
  setErrorID(__FILE__, __FUNCTION__, __LINE__,
             ERROR_EC_SLAVE_ENTRY_INDEX_OUT_OF_RANGE);
  return NULL;
}

int ecmcEcSlave::updateInputProcessImage() {
  if (newSyncManagers_.empty()) {
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SLAVE_ENTRY_NULL);
  }
  for (size_t sm = 0; sm < newSyncManagers_.size(); sm++) {
    for (size_t j = 0; j < newSyncManagers_[sm].entries.size(); j++) {
      ecmcEcEntry *entry = newSyncManagers_[sm].entries[j];
      if (entry) {
        entry->updateInputProcessImage();
      }
    }
  }

  // Execute async SDOs
  for (int i = 0; i < asyncSDOCounter_; i++) {
    asyncSDOvector_[i]->execute();
  }

  return 0;
}

int ecmcEcSlave::updateOutProcessImage() {
  if (newSyncManagers_.empty()) {
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SLAVE_ENTRY_NULL);
  }
  for (size_t sm = 0; sm < newSyncManagers_.size(); sm++) {
    for (size_t j = 0; j < newSyncManagers_[sm].entries.size(); j++) {
      ecmcEcEntry *entry = newSyncManagers_[sm].entries[j];
      if (entry) {
        entry->updateOutProcessImage();
      }
    }
  }

  return 0;
}

int ecmcEcSlave::getSlaveBusPosition() {
  return slavePosition_;
}

int ecmcEcSlave::addEntry(
  ec_direction_t direction,
  uint8_t        syncMangerIndex,
  uint16_t       pdoIndex,
  uint16_t       entryIndex,
  uint8_t        entrySubIndex,
  ecmcEcDataType dt,
  std::string    id,
  int            useInRealTime) {
  if (getEntryCount() >= EC_MAX_ENTRIES) {
    return ERROR_EC_SLAVE_ENTRY_INDEX_OUT_OF_RANGE;
  }

  int err = 0;
  // Ensure sync manager exists in new storage
  addSyncManager(direction, syncMangerIndex);

  ecmcEcEntry *entry = addEntryToNew(syncMangerIndex,
                                     pdoIndex,
                                     entryIndex,
                                     entrySubIndex,
                                     dt,
                                     id,
                                     useInRealTime,
                                     &err);

  if (!entry || err) {
    return err ? err : ERROR_EC_SLAVE_ENTRY_NULL;
  }

  if (entry->getError()) {
    return entry->getErrorID();
  }

  return 0;
}

int ecmcEcSlave::addDataItem(ecmcEcEntry   *startEntry,
                             size_t         entryByteOffset,
                             size_t         entryBitOffset,
                             ec_direction_t direction,
                             ecmcEcDataType dt,
                             std::string    id) {
  if (getEntryCount() >= EC_MAX_ENTRIES) {
    return ERROR_EC_SLAVE_ENTRY_INDEX_OUT_OF_RANGE;
  }

  // Do not add this to sync manager and pdo since no ethercat configs are done.
  // This is just pure mem access of alreday configured entry/processimage
  ecmcEcEntry *entry = new ecmcEcData(asynPortDriver_,
                                      masterId_,
                                      slavePosition_,
                                      startEntry,
                                      entryByteOffset,
                                      entryBitOffset,
                                      direction,
                                      dt,
                                      id);

  if (!entry) {
    LOGERR(
      "%s/%s:%d: ERROR: Slave %d (0x%x,0x%x): Add data item failed (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      slavePosition_,
      vendorId_,
      productCode_,
      ERROR_EC_SLAVE_ADD_DATA_ITEM_FAIL);
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SLAVE_ADD_DATA_ITEM_FAIL);
  }

  if (entry->getError()) {
    return entry->getErrorID();
  }

  if (entryLookup_.empty()) {
    entryLookup_.reserve(EC_MAX_ENTRIES);
  }
  entryLookup_.push_back(std::make_pair(id, entry));
  entryLookupSorted_ = false;
  extraEntries_.push_back(entry);
  flatEntries_.push_back(entry);
  flatEntryIds_.push_back(id);
  return 0;
}

ecmcEcSlave::SyncManager * ecmcEcSlave::findSyncMan(uint8_t syncMangerIndex) {
  for (size_t i = 0; i < newSyncManagers_.size(); i++) {
    if (newSyncManagers_[i].index == syncMangerIndex) {
      return &newSyncManagers_[i];
    }
  }
  return NULL;
}

ecmcEcEntry * ecmcEcSlave::addEntryToNew(uint8_t        syncMangerIndex,
                                         uint16_t       pdoIndex,
                                         uint16_t       entryIndex,
                                         uint8_t        entrySubIndex,
                                         ecmcEcDataType dt,
                                         std::string    id,
                                         int            useInRealTime,
                                         int           *errorCode) {
  SyncManager *sm = findSyncMan(syncMangerIndex);
  if (!sm) {
    newSyncManagers_.push_back(SyncManager());
    sm = &newSyncManagers_.back();
    sm->direction = EC_DIR_INVALID;
    sm->index     = syncMangerIndex;
  }

  int err        = 0;
  ecmcEcEntry *entry = new ecmcEcEntry(asynPortDriver_,
                                       masterId_,
                                       slavePosition_,
                                       domain_,
                                       slaveConfig_,
                                       pdoIndex,
                                       entryIndex,
                                       entrySubIndex,
                                       sm->direction,
                                       dt,
                                       id,
                                       useInRealTime);
  if (!entry) {
    if (errorCode) {
      *errorCode = ERROR_EC_SLAVE_ENTRY_NULL;
    }
    return NULL;
  }
  // Register PDO mapping
  entry->compileRegInfo();

  sm->entries.push_back(entry);
  sm->entryIds.push_back(id);
  if (entryLookup_.empty()) {
    entryLookup_.reserve(EC_MAX_ENTRIES);
  }
  entryLookup_.push_back(std::make_pair(id, entry));
  entryLookupSorted_ = false;
  flatEntries_.push_back(entry);
  flatEntryIds_.push_back(id);
  if (errorCode) {
    *errorCode = 0;
  }
  return entry;
}

int ecmcEcSlave::configDC(
  uint16_t assignActivate,     /**< AssignActivate word. */
  uint32_t sync0Cycle,     /**< SYNC0 cycle time [ns]. */
  int32_t  sync0Shift,    /**< SYNC0 shift time [ns]. */
  uint32_t sync1Cycle,     /**< SYNC1 cycle time [ns]. */
  int32_t  sync1Shift /**< SYNC1 shift time [ns]. */) {
  if (slaveConfig_ == 0) {
    LOGERR(
      "%s/%s:%d: ERROR: Slave %d (0x%x,0x%x): Slave Config NULL (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      slavePosition_,
      vendorId_,
      productCode_,
      ERROR_EC_SLAVE_CONFIG_NULL);
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SLAVE_CONFIG_NULL);
  }

  ecrt_slave_config_dc(slaveConfig_,
                       assignActivate,
                       sync0Cycle,
                       sync0Shift,
                       sync1Cycle,
                       sync1Shift);
  return 0;
}

ecmcEcEntry * ecmcEcSlave::findEntry(std::string id) {
  ecmcEcEntry *entry = findEntryById(id);
  if (entry) {
    return entry;
  }

  // Simulation entries
  for (int i = 0; i < SIMULATION_ENTRIES; i++) {
    if (simEntries_[i] != NULL) {
      if (simEntries_[i]->getIdentificationName().compare(id) == 0) {
        return simEntries_[i];
      }
    }
  }
  return NULL;
}

ecmcEcEntry * ecmcEcSlave::findEntryById(const std::string& id) {
  if (!entryLookupSorted_) {
    std::sort(entryLookup_.begin(), entryLookup_.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    entryLookupSorted_ = true;
  }

  auto it = std::lower_bound(entryLookup_.begin(),
                             entryLookup_.end(),
                             id,
                             [](const auto& a, const std::string& key) {
                               return a.first < key;
                             });
  if (it != entryLookup_.end() && it->first == id) {
    return it->second;
  }
  return NULL;
}

int ecmcEcSlave::findEntryIndex(std::string id) {
  for (size_t i = 0; i < flatEntryIds_.size(); i++) {
    if (flatEntryIds_[i] == id) {
      return static_cast<int>(i);
    }
  }
  for (int i = 0; i < SIMULATION_ENTRIES; i++) {
    if (simEntries_[i] != NULL) {
      if (simEntries_[i]->getIdentificationName().compare(id) == 0) {
        return i;
      }
    }
  }
  LOGERR("%s/%s:%d: ERROR: Slave %d (0x%x,0x%x): Entry not found (0x%x).\n",
         __FILE__,
         __FUNCTION__,
         __LINE__,
         slavePosition_,
         vendorId_,
         productCode_,
         ERROR_EC_SLAVE_ENTRY_NULL);
  return -ERROR_EC_SLAVE_ENTRY_NULL;
}

int ecmcEcSlave::selectAsReferenceDC() {
  return ecrt_master_select_reference_clock(master_, slaveConfig_);
}

int ecmcEcSlave::setWatchDogConfig(

  // Number of 40 ns intervals. Used as a base unit for all slave watchdogs.
  // If set to zero, the value is not written, so the default is used.
  uint16_t watchdogDivider,

  // Number of base intervals for process data watchdog. If set to zero,
  // the value is not written, so the default is used.
  uint16_t watchdogIntervals) {
  if (!slaveConfig_) {
    LOGERR(
      "%s/%s:%d: ERROR: Slave %d (0x%x,0x%x): Slave Config NULL (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      slavePosition_,
      vendorId_,
      productCode_,
      ERROR_EC_SLAVE_CONFIG_NULL);
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SLAVE_CONFIG_NULL);
  }
  ecrt_slave_config_watchdog(slaveConfig_, watchdogDivider, watchdogIntervals);
  return 0;
}

int ecmcEcSlave::addSDOWrite(uint16_t sdoIndex,
                             uint8_t  sdoSubIndex,
                             uint32_t writeValue,
                             int      byteSize) {
  if (!slaveConfig_) {
    LOGERR(
      "%s/%s:%d: ERROR: Slave %d (0x%x,0x%x): Slave Config NULL (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      slavePosition_,
      vendorId_,
      productCode_,
      ERROR_EC_SLAVE_CONFIG_NULL);
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SLAVE_CONFIG_NULL);
  }

  return ecmcEcSDO::addSdoConfig(slaveConfig_,
                                 slavePosition_,
                                 sdoIndex,
                                 sdoSubIndex,
                                 writeValue,
                                 byteSize);
}

int ecmcEcSlave::addSDOWriteDT(uint16_t       sdoIndex,
                               uint8_t        sdoSubIndex,
                               const char    *value,
                               ecmcEcDataType dt) {
  if (!slaveConfig_) {
    LOGERR(
      "%s/%s:%d: ERROR: Slave %d (0x%x,0x%x): Slave Config NULL (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      slavePosition_,
      vendorId_,
      productCode_,
      ERROR_EC_SLAVE_CONFIG_NULL);
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SLAVE_CONFIG_NULL);
  }

  return ecmcEcSDO::addSdoConfigDT(slaveConfig_,
                                   slavePosition_,
                                   sdoIndex,
                                   sdoSubIndex,
                                   value,
                                   dt);
}

int ecmcEcSlave::getSlaveState(ec_slave_config_state_t *state) {
  state = &slaveState_;
  return 0;
}

int ecmcEcSlave::initAsyn() {
  char  buffer[EC_MAX_OBJECT_PATH_CHAR_LENGTH];
  char *name                  = buffer;
  ecmcAsynDataItem *paramTemp = NULL;

  // "ec%d.s%d.status"
  unsigned int charCount = snprintf(buffer,
                                    sizeof(buffer),
                                    ECMC_EC_STR "%d." ECMC_SLAVE_CHAR "%d." ECMC_ASYN_EC_SLAVE_PAR_STATUS_NAME,
                                    masterId_,
                                    slavePosition_);

  if (charCount >= sizeof(buffer) - 1) {
    LOGERR(
      "%s/%s:%d: Error: Failed to generate alias. Buffer to small (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      ERROR_EC_SLAVE_REG_ASYN_PAR_BUFFER_OVERFLOW);
    return ERROR_EC_SLAVE_REG_ASYN_PAR_BUFFER_OVERFLOW;
  }
  name      = buffer;
  paramTemp = asynPortDriver_->addNewAvailParam(name,
                                                asynParamUInt32Digital,
                                                (uint8_t *)&(statusWord_),
                                                sizeof(statusWord_),
                                                ECMC_EC_U32,
                                                0);

  if (!paramTemp) {
    LOGERR(
      "%s/%s:%d: ERROR: Add create default parameter for %s failed.\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      name);
    return ERROR_MAIN_ASYN_CREATE_PARAM_FAIL;
  }
  paramTemp->setAllowWriteToEcmc(false);
  paramTemp->refreshParam(1);
  paramTemp->addSupportedAsynType(asynParamInt32);
  paramTemp->addSupportedAsynType(asynParamUInt32Digital);
  slaveAsynParams_[ECMC_ASYN_EC_SLAVE_PAR_STATUS_ID] = paramTemp;
  asynPortDriver_->callParamCallbacks(ECMC_ASYN_DEFAULT_LIST,
                                      ECMC_ASYN_DEFAULT_ADDR);
  return 0;
}

int ecmcEcSlave::validate() {
  int errorCode = 0;

  for (size_t sm = 0; sm < newSyncManagers_.size(); sm++) {
    for (size_t j = 0; j < newSyncManagers_[sm].entries.size(); j++) {
      ecmcEcEntry *entry = newSyncManagers_[sm].entries[j];
      if (entry) {
        errorCode = entry->validate();
        if (errorCode) {
          return errorCode;
        }
      }
    }
  }
  for (size_t i = 0; i < extraEntries_.size(); i++) {
    if (extraEntries_[i]) {
      errorCode = extraEntries_[i]->validate();
      if (errorCode) {
        return errorCode;
      }
    }
  }

  // Check that SDO settings are made if needed for channels (if used in motion axis)
  if(enableSDOCheck_) {
    for(uint ch = 0; ch < sdoChVerify_.size(); ch++) {
      if(sdoChVerify_[ch].needSdo && !sdoChVerify_[ch].sdosDone) {
        LOGERR(
          "%s/%s:%d: ERROR: Important SDO settings, i.e. max current or other, missing for slave %d, ch %d.\n"
          "Use \"ecmcConfigOrDie \"Cfg.EcSetSlaveSDOSettingsDone(<slave_id>,<ch_id>,1)\"\" after addSlave.cmd to override\n",
          __FILE__,
          __FUNCTION__,
          __LINE__, slavePosition_, ch + 1);
        return ERROR_EC_SLAVE_SDO_SETTINGS_MISSING;
      }
    }
  }
  return 0;
}

int ecmcEcSlave::addSDOWriteComplete(uint16_t    sdoIndex,
                                     const char *dataBuffer,
                                     int         byteSize) {
  if (!slaveConfig_) {
    LOGERR(
      "%s/%s:%d: ERROR: Slave %d (0x%x,0x%x): Slave Config NULL (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      slavePosition_,
      vendorId_,
      productCode_,
      ERROR_EC_SLAVE_CONFIG_NULL);
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SLAVE_CONFIG_NULL);
  }

  return ecmcEcSDO::addWriteComplete(slaveConfig_,
                                     sdoIndex,
                                     dataBuffer,
                                     (size_t)byteSize);
}

int ecmcEcSlave::addSDOWriteBuffer(uint16_t    sdoIndex,
                                   uint8_t     sdoSubIndex,
                                   const char *dataBuffer,
                                   int         byteSize) {
  if (!slaveConfig_) {
    LOGERR(
      "%s/%s:%d: ERROR: Slave %d (0x%x,0x%x): Slave Config NULL (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      slavePosition_,
      vendorId_,
      productCode_,
      ERROR_EC_SLAVE_CONFIG_NULL);
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SLAVE_CONFIG_NULL);
  }

  return ecmcEcSDO::addSdoConfigBuffer(slaveConfig_,
                                       sdoIndex,
                                       sdoSubIndex,
                                       dataBuffer,
                                       (size_t)byteSize);
}

int ecmcEcSlave::addSDOAsync(uint16_t       sdoIndex, /**< SDO index. */
                             uint8_t        sdoSubIndex, /**< SDO subindex. */
                             ecmcEcDataType dt,
                             std::string    alias) {
  try {
    ecmcEcAsyncSDO *temp = new ecmcEcAsyncSDO(asynPortDriver_,
                                              masterId_,
                                              slavePosition_,
                                              slaveConfig_,
                                              sdoIndex,
                                              sdoSubIndex,
                                              dt,
                                              alias);
    asyncSDOvector_.push_back(temp);
  }
  catch (std::exception& e) {
    LOGERR(
      "%s/%s:%d: ERROR: Slave %d: Failed to create async SDO object (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      slavePosition_,
      ERROR_EC_SLAVE_SDO_ASYNC_CREATE_FAIL);
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SLAVE_SDO_ASYNC_CREATE_FAIL);
  }
  asyncSDOCounter_++;
  return 0;
}

int ecmcEcSlave::activate() {
  int ret = 0;

  for (size_t sm = 0; sm < newSyncManagers_.size(); sm++) {
    for (size_t j = 0; j < newSyncManagers_[sm].entries.size(); j++) {
      ecmcEcEntry *tempEntry = newSyncManagers_[sm].entries[j];

      if (tempEntry == NULL) {
        LOGERR("%s/%s:%d: ERROR: Entry NULL (0x%x).\n",
               __FILE__,
               __FUNCTION__,
               __LINE__,
               ERROR_EC_MAIN_ENTRY_NULL);
        return setErrorID(__FILE__,
                          __FUNCTION__,
                          __LINE__,
                          ERROR_EC_MAIN_ENTRY_NULL);
      }

      if (!tempEntry->getSimEntry()) {
        ret = tempEntry->activate();

        if (ret) {
          return ret;
        }
      }
    }
  }

  for (size_t i = 0; i < extraEntries_.size(); i++) {
    ecmcEcEntry *tempEntry = extraEntries_[i];
    if (tempEntry && !tempEntry->getSimEntry()) {
      ret = tempEntry->activate();
      if (ret) {
        return ret;
      }
    }
  }

  for (size_t i = 0; i < extraEntries_.size(); i++) {
    ecmcEcEntry *tempEntry = extraEntries_[i];
    if (tempEntry && !tempEntry->getSimEntry()) {
      ret = tempEntry->compileRegInfo();
      if (ret) {
        return ret;
      }
    }
  }

  return 0;
}

int ecmcEcSlave::compileRegInfo() {
  int ret = 0;

  for (size_t sm = 0; sm < newSyncManagers_.size(); sm++) {
    for (size_t j = 0; j < newSyncManagers_[sm].entries.size(); j++) {
      ecmcEcEntry *tempEntry = newSyncManagers_[sm].entries[j];

      if (tempEntry == NULL) {
        LOGERR("%s/%s:%d: ERROR: Entry NULL (0x%x).\n",
               __FILE__,
               __FUNCTION__,
               __LINE__,
               ERROR_EC_MAIN_ENTRY_NULL);
        return setErrorID(__FILE__,
                          __FUNCTION__,
                          __LINE__,
                          ERROR_EC_MAIN_ENTRY_NULL);
      }

      if (!tempEntry->getSimEntry()) {
        ret = tempEntry->compileRegInfo();

        if (ret) {
          return ret;
        }
      }
    }
  }

  return 0;
}

int ecmcEcSlave::getAllowOffline() {
  if (domain_) {
    return domain_->getAllowOffline();
  }

  // No domain attached the simulation slave
  return 1;
}

/* The two commands, setNeedSDOSettings and setSDOSettingsDone, is
   typically set from the configuration frame work:
  * setNeedSDOSettings from addSlave.cmd for certain hardware
  * setSDOSettingsDone from ecmccomp applyComponent.cmd, configureSlave.cmd or applySkaveConfig.cmd.
  Note: chId start count at 1
*/ 
int ecmcEcSlave::setNeedSDOSettings(int chId, int need) {
  
  if(chId <= 0 || chId > SDO_SETTINGS_MAX_CH) {
    return ERROR_EC_SLAVE_SDO_CH_ID_OUT_OF_RANGE;
  }

  sdoVerifyChX temp;
  temp.needSdo = 0; 
  temp.sdosDone = 0;

  // Ensure vector is long enough..
  while(sdoChVerify_.size() < (uint)chId) {
    sdoChVerify_.push_back(temp);
  }
  
  // Zero based index
  sdoChVerify_[chId-1].needSdo = need;
  return 0;
}

// chId start count at 1
int ecmcEcSlave::setSDOSettingsDone(int chId, int done) {

  if(chId <= 0 || chId > SDO_SETTINGS_MAX_CH) {
    return ERROR_EC_SLAVE_SDO_CH_ID_OUT_OF_RANGE; 
  }

  sdoVerifyChX temp;
  temp.needSdo = 0; 
  temp.sdosDone = 0;

  // Ensure vector is long enough..
  while(sdoChVerify_.size() < (uint)chId) {
    sdoChVerify_.push_back(temp);
  }

  // Zero based index
  sdoChVerify_[chId-1].sdosDone = done;

  return 0;
}

int ecmcEcSlave::setEnableSDOCheck(int enable) {
  enableSDOCheck_ = enable;
  return 0;
}

int ecmcEcSlave::addSimEntry(std::string    id,
                             ecmcEcDataType dt,
                             uint64_t value) {
  // Buffer full
  if (simEntryCounter_ >= SIMULATION_ENTRIES) {
    return ERROR_EC_SLAVE_CONFIG_FAILED;
  }

  // Data buffer
  simBuffer_[simEntryCounter_] = value;
  
  // Entry
  simEntries_[simEntryCounter_]= new ecmcEcEntry(asynPortDriver_,
                                                 masterId_,
                                                 slavePosition_,
                                                 (uint8_t*)&(simBuffer_[simEntryCounter_]),
                                                 dt,
                                                 id);

  simEntries_[simEntryCounter_]->writeValue(value);

  if (entryLookup_.empty()) {
    entryLookup_.reserve(EC_MAX_ENTRIES);
  }
  entryLookup_.push_back(std::make_pair(id, simEntries_[simEntryCounter_]));
  entryLookupSorted_ = false;
  extraEntries_.push_back(simEntries_[simEntryCounter_]);
  simEntryCounter_++;
  return 0;
}
