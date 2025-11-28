/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcEcSyncManager.cpp
*
*  Created on: Dec 11, 2015
*      Author: anderssandstrom
*
\*************************************************************************/

#include "ecmcEcSyncManager.h"

ecmcEcSyncManager::ecmcEcSyncManager(ecmcAsynPortDriver *asynPortDriver,
                                     int                 masterId,
                                     int                 slaveId,
                                     ecmcEcDomain       *domain,
                                     ec_slave_config_t  *slave,
                                     ec_direction_t      direction,
                                     uint8_t             syncMangerIndex) {
  initVars();
  asynPortDriver_  = asynPortDriver;
  masterId_        = masterId;
  slaveId_         = slaveId;
  syncMangerIndex_ = syncMangerIndex;
  direction_       = direction;
  slaveConfig_     = slave;
  domain_          = domain;

  int errorCode = ecrt_slave_config_sync_manager(slaveConfig_,
                                                 syncMangerIndex_,
                                                 direction_,
                                                 EC_WD_DEFAULT);

  if (errorCode) {
    LOGERR(
      "%s/%s:%d: ERROR: ecrt_slave_config_sync_manager() failed with error code %d (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      errorCode,
      ERROR_EC_SM_CONFIG_FAIL);
    setErrorID(__FILE__, __FUNCTION__, __LINE__, ERROR_EC_SM_CONFIG_FAIL);
  }

  ecrt_slave_config_pdo_assign_clear(slaveConfig_, syncMangerIndex_);
  LOGINFO5("%s/%s:%d: INFO: Sync manager %d configured: direction %d.\n",
           __FILE__,
           __FUNCTION__,
           __LINE__,
           syncMangerIndex,
           direction);
}

void ecmcEcSyncManager::initVars() {
  errorReset();

  asynPortDriver_  = NULL;
  masterId_        = -1;
  slaveId_         = -1;
  direction_       = EC_DIR_INPUT;
  syncMangerIndex_ = 0;
  slaveConfig_     = NULL;
  domain_          = NULL;
}

ecmcEcSyncManager::~ecmcEcSyncManager() {
  for (size_t i = 0; i < pdos_.size(); i++) {
    delete pdos_[i];
  }
  pdos_.clear();
}

int ecmcEcSyncManager::addPdo(uint16_t pdoIndex) {
  if (pdos_.size() >= static_cast<size_t>(EC_MAX_PDOS - 1)) {
    LOGERR("%s/%s:%d: ERROR: PDO array full (0x%x).\n",
           __FILE__,
           __FUNCTION__,
           __LINE__,
           ERROR_EC_SM_PDO_ARRAY_FULL);
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SM_PDO_ARRAY_FULL);
  }
  pdos_.push_back(new ecmcEcPdo(asynPortDriver_,
                                masterId_,
                                slaveId_,
                                domain_,
                                slaveConfig_,
                                syncMangerIndex_,
                                pdoIndex,
                                direction_));
  return 0;
}

ecmcEcPdo * ecmcEcSyncManager::getPdo(int index) {
  if (index >= EC_MAX_PDOS) {
    LOGERR("%s/%s:%d: ERROR: PDO index out of range (0x%x).\n",
           __FILE__,
           __FUNCTION__,
           __LINE__,
           ERROR_EC_SM_PDO_INDEX_OUT_OF_RANGE);
    setErrorID(__FILE__,
               __FUNCTION__,
               __LINE__,
               ERROR_EC_SM_PDO_INDEX_OUT_OF_RANGE);
    return NULL;
  }
  if (index < 0 || static_cast<size_t>(index) >= pdos_.size()) {
    return NULL;
  }
  return pdos_[index];
}

int ecmcEcSyncManager::getPdoCount() {
  return static_cast<int>(pdos_.size());
}

int ecmcEcSyncManager::getInfo(ec_sync_info_t *info) {
  if (info == NULL) {
    LOGERR("%s/%s:%d: ERROR: Output parameter pointer NULL (0x%x).\n",
           __FILE__,
           __FUNCTION__,
           __LINE__,
           ERROR_EC_SM_ENTRY_INFO_STRUCT_NULL);
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_SM_ENTRY_INFO_STRUCT_NULL);
  }

  info->dir           = direction_;
  info->index         = syncMangerIndex_;
  info->n_pdos        = static_cast<unsigned int>(pdos_.size());
  info->pdos          = NULL;
  info->watchdog_mode = EC_WD_DEFAULT;
  return 0;
}

ec_direction_t ecmcEcSyncManager::getDirection() {
  return direction_;
}

uint8_t ecmcEcSyncManager::getSyncMangerIndex() {
  return syncMangerIndex_;
}

ecmcEcEntry * ecmcEcSyncManager::addEntry(
  uint16_t       pdoIndex,
  uint16_t       entryIndex,
  uint8_t        entrySubIndex,
  ecmcEcDataType dt,
  std::string    id,
  int            useInRealTime,
  int           *errorCode
  ) {
  int err        = 0;
  ecmcEcPdo *pdo = findPdo(pdoIndex);

  if (pdo == NULL) {
    err = addPdo(pdoIndex);

    if (err) {
      *errorCode = err;
      return NULL;
    }
    pdo = pdos_.back();  // Last added sync manager
  }

  ecmcEcEntry *entry = pdo->addEntry(entryIndex,
                                     entrySubIndex,
                                     dt,
                                     id,
                                     useInRealTime,
                                     &err);

  if (err || !entry) {
    *errorCode = err;
    return NULL;
  }

  return entry;
}

ecmcEcPdo * ecmcEcSyncManager::findPdo(uint16_t pdoIndex) {
  for (size_t i = 0; i < pdos_.size(); i++) {
    ecmcEcPdo *pdo = pdos_[i];
    if (pdo && pdo->getPdoIndex() == pdoIndex) {
      return pdo;
    }
  }
  return NULL;
}

ecmcEcEntry * ecmcEcSyncManager::findEntry(std::string id) {
  ecmcEcEntry *temp = NULL;

  if (pdos_.empty()) {
    return temp;
  }

  for (size_t i = 0; i < pdos_.size(); i++) {
    ecmcEcPdo *pdo = pdos_[i];
    if (pdo) {
      temp = pdo->findEntry(id);

      if (temp) {
        return temp;
      }
    }
  }
  return NULL;
}
