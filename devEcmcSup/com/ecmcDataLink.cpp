/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcDataLink.cpp
*
*  Created on: May 23, 2016
*      Author: anderssandstrom
*
\*************************************************************************/

#include "ecmcDataLink.h"
#include "ecmcRtLogger.h"

extern app_mode_type appModeStat;

int ecmcDataLink::setDataError(const char *fileName,
                                     const char *functionName,
                                     int         lineNumber) {
  if (appModeStat == ECMC_MODE_STARTUP) {
    return ERROR_EC_ENTRY_EC_DOMAIN_ERROR;
  }

  return setErrorID(fileName,
                    functionName,
                    lineNumber,
                    ERROR_EC_ENTRY_EC_DOMAIN_ERROR);
}

ecmcDataLink::ecmcDataLink(int *errorPtr, int *warningPtr) : ecmcError(
    errorPtr,
    warningPtr) {
  initVars();
}

ecmcDataLink::ecmcDataLink() {
  initVars();
}

void ecmcDataLink::initVars() {
  for (int i = 0; i < ECMC_EC_ENTRY_LINKS_MAX; i++) {
    accessOwners_[i].reset();
    entryInfoArray_[i].entry     = NULL;
    entryInfoArray_[i].bitNumber = -1;
  }
}

ecmcDataLink::~ecmcDataLink() {}

int ecmcDataLink::setDataAtIndex(const ecmcDataAccess &entry, int index, int bitIndex) {
  try { return setDataAtIndex(std::make_shared<ecmcDataAccess>(entry), index, bitIndex); }
  catch (...) { return ERROR_EC_ENTRY_LINK_FAILED; }
}

int ecmcDataLink::setDataAtIndex(std::shared_ptr<ecmcDataAccess> entry, int index, int bitIndex) {
  if (!entry || !entry->data || index < 0 || index >= ECMC_DATA_LINKS_MAX)
    return setErrorID(__FILE__, __FUNCTION__, __LINE__, ERROR_EC_ENTRY_LINK_FAILED);
  accessOwners_[index] = std::move(entry);
  entryInfoArray_[index].entry = accessOwners_[index].get();
  entryInfoArray_[index].bitNumber = bitIndex;
  return 0;
}

int ecmcDataLink::validateData(int index) {
  if ((index >= ECMC_EC_ENTRY_LINKS_MAX) || (index < 0)) {
    ecmcRtLoggerLogError("%s/%s:%d: ERROR: Entry list index %d out of range.(0x%x).\n",
           __FILE__,
           __FUNCTION__,
           __LINE__,
           index,
           ERROR_EC_ENTRY_INDEX_OUT_OF_RANGE);
    return ERROR_EC_ENTRY_INDEX_OUT_OF_RANGE;
  }

  if (entryInfoArray_[index].bitNumber >= 0) {
    return validateDataBit(index);
  }

  if (entryInfoArray_[index].entry == NULL) {
    ecmcRtLoggerLogError(
      "%s/%s:%d: ERROR: Entry ethercat data pointer NULL at index %d (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      index,
      ERROR_EC_ENTRY_DATA_POINTER_NULL);
    return ERROR_EC_ENTRY_DATA_POINTER_NULL;
  }

  return 0;
}

int ecmcDataLink::validateDataBit(int index) {
  if ((index >= ECMC_EC_ENTRY_LINKS_MAX) || (index < 0)) {
    ecmcRtLoggerLogError("%s/%s:%d: ERROR: Entry list index %d out of range.(0x%x).\n",
           __FILE__,
           __FUNCTION__,
           __LINE__,
           index,
           ERROR_EC_ENTRY_INDEX_OUT_OF_RANGE);
    return ERROR_EC_ENTRY_INDEX_OUT_OF_RANGE;
  }

  if (entryInfoArray_[index].entry == NULL) {
    ecmcRtLoggerLogError(
      "%s/%s:%d: ERROR: Entry ethercat data pointer NULL at index %d (0x%x).\n",
      __FILE__,
      __FUNCTION__,
      __LINE__,
      index,
      ERROR_EC_ENTRY_DATA_POINTER_NULL);
    return ERROR_EC_ENTRY_DATA_POINTER_NULL;
  }

  if (entryInfoArray_[index].bitNumber<0 ||
                                       entryInfoArray_[index].bitNumber>
      entryInfoArray_[index].entry->getBits() -
      1) {
    ecmcRtLoggerLogError("%s/%s:%d: ERROR: Invalid bit index.(0x%x).\n",
           __FILE__,
           __FUNCTION__,
           __LINE__,
           ERROR_EC_ENTRY_INVALID_BIT_INDEX);
    return ERROR_EC_ENTRY_INVALID_BIT_INDEX;
  }

  return 0;
}

int ecmcDataLink::readDataValue(int entryIndex, uint64_t *value) {
  ecmcDataAccess * const entry = entryInfoArray_[entryIndex].entry;
  const int bitNumber       = entryInfoArray_[entryIndex].bitNumber;

  if (!checkDataValid(entryIndex)) {
    return setDataError(__FILE__, __FUNCTION__, __LINE__);
  }

  uint64_t tempRaw = 0;

  if (bitNumber < 0) {
    if (entry->readValue(&tempRaw)) {
      return setErrorID(__FILE__,
                        __FUNCTION__,
                        __LINE__,
                        ERROR_EC_ENTRY_READ_FAIL);
    }
  } else {
    if (entry->readBit(bitNumber, &tempRaw)) {
      return setErrorID(__FILE__,
                        __FUNCTION__,
                        __LINE__,
                        ERROR_EC_ENTRY_READ_FAIL);
    }
  }
  *value = tempRaw;
  return 0;
}

int ecmcDataLink::readDataValueDouble(int entryIndex, double *value) {
  ecmcDataAccess * const entry = entryInfoArray_[entryIndex].entry;

  if (!checkDataValid(entryIndex)) {
    return setDataError(__FILE__, __FUNCTION__, __LINE__);
  }

  double tempDouble = 0;

  if (entry->readDouble(&tempDouble)) {
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_ENTRY_READ_FAIL);
  }
  *value = tempDouble;
  return 0;
}

int ecmcDataLink::writeDataBits(int entryIndex, int bits, uint64_t value) {
  ecmcDataAccess * const entry = entryInfoArray_[entryIndex].entry;
  const int bitNumber       = entryInfoArray_[entryIndex].bitNumber;

  if (entry->writeBits(bitNumber, bits, value)) {
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_ENTRY_WRITE_FAIL);
  }
  
  // Still write value to entry (above) even if domain error to keep value up to date
  if (!checkDataValid(entryIndex)) {
    return setDataError(__FILE__, __FUNCTION__, __LINE__);
  }
  return 0;
}


int ecmcDataLink::readDataBits(int entryIndex, int bits, uint64_t *value) {
  ecmcDataAccess * const entry = entryInfoArray_[entryIndex].entry;
  const int bitNumber       = entryInfoArray_[entryIndex].bitNumber;

  if (!checkDataValid(entryIndex)) {
    return setDataError(__FILE__, __FUNCTION__, __LINE__);
  }

  uint64_t tempRaw = 0;

 if (entry->readBits(bitNumber, bits, &tempRaw)) {
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_ENTRY_READ_FAIL);
  }

  *value = tempRaw;
  return 0;
}

int ecmcDataLink::writeDataValue(int entryIndex, uint64_t value) {
  ecmcDataAccess * const entry = entryInfoArray_[entryIndex].entry;
  const int bitNumber       = entryInfoArray_[entryIndex].bitNumber;

  if (bitNumber < 0) {
    if (entry->writeValue(value)) {
      return setErrorID(__FILE__,
                        __FUNCTION__,
                        __LINE__,
                        ERROR_EC_ENTRY_WRITE_FAIL);
    }
  } else {
    if (entry->writeBit(bitNumber, value)) {
      return setErrorID(__FILE__,
                        __FUNCTION__,
                        __LINE__,
                        ERROR_EC_ENTRY_WRITE_FAIL);
    }
  }

  // Still write value to entry (above) even if domain error to keep value up to date
  if (!checkDataValid(entryIndex)) {
    return setDataError(__FILE__, __FUNCTION__, __LINE__);
  }

  return 0;
}

int ecmcDataLink::writeDataValueDouble(int entryIndex, double value) {
  ecmcDataAccess * const entry = entryInfoArray_[entryIndex].entry;

  if (entry->writeDouble(value)) {
    return setErrorID(__FILE__,
                      __FUNCTION__,
                      __LINE__,
                      ERROR_EC_ENTRY_WRITE_FAIL);
  }

  // Still write value to entry (above) even if domain error to keep value up to date
  if (!checkDataValid(entryIndex)) {
    return setDataError(__FILE__, __FUNCTION__, __LINE__);
  }

  return 0;
}

int ecmcDataLink::getDataBitCount(int index, int *bitCount) {
  if (entryInfoArray_[index].entry == NULL) {
    return ERROR_EC_ENTRY_DATA_POINTER_NULL;
  }

  *bitCount = entryInfoArray_[index].entry->getBits();
  return 0;
}

int ecmcDataLink::getDataStartBit(int index, int *startBit) {
  if (entryInfoArray_[index].entry == NULL) {
    return ERROR_EC_ENTRY_DATA_POINTER_NULL;
  }

  *startBit = entryInfoArray_[index].bitNumber;
  return 0;
}

bool ecmcDataLink::checkDataExist(int entryIndex) {
  if ((entryIndex >= 0) && (entryIndex < ECMC_EC_ENTRY_LINKS_MAX)) {
    return entryInfoArray_[entryIndex].entry != NULL;
  }
  return false;
}

ecmcEcDataType ecmcDataLink::getDataType(int index) {
  if ((index < 0) || (index >= ECMC_EC_ENTRY_LINKS_MAX)) {
    return ECMC_EC_NONE;
  }

  if (entryInfoArray_[index].entry == NULL) {
    return ECMC_EC_NONE;
  }

  return entryInfoArray_[index].entry->getDataType();
}

int ecmcDataLink::getSourceId(int index) {
  if ((index < 0) || (index >= ECMC_EC_ENTRY_LINKS_MAX)) {
    return -1000;
  }

  if (entryInfoArray_[index].entry == NULL) {
    return -1000;
  }

  return entryInfoArray_[index].entry->getSourceId();
}

bool ecmcDataLink::checkDataValid(int entryIndex) {
  if ((entryIndex < 0) || (entryIndex >= ECMC_EC_ENTRY_LINKS_MAX)) {
    return false;
  }

  if (entryInfoArray_[entryIndex].entry == NULL) {
    return false;
  }

  return entryInfoArray_[entryIndex].entry->isValid();
}

bool ecmcDataLink::checkAllDataValid() {
  for (int i = 0; i < ECMC_EC_ENTRY_LINKS_MAX; i++) {
    ecmcDataAccess * const entry = entryInfoArray_[i].entry;
    if (entryInfoArray_[i].entry) {
      if (!entry->isValid()) {
        return false;
      }
    } else {  // no more entries
      break;
    }
  }
  return true;
}

int ecmcDataLink::clearDataAtIndex(int index) {
  if (index < 0 || index >= ECMC_EC_ENTRY_LINKS_MAX) return ERROR_EC_ENTRY_INDEX_OUT_OF_RANGE;
  entryInfoArray_[index].entry = nullptr;
  accessOwners_[index].reset();
  entryInfoArray_[index].bitNumber = -1;
  return 0;
}

std::shared_ptr<ecmcDataAccess> ecmcDataLink::getDataAtIndex(int index, int *bitIndex) const {
  if (index < 0 || index >= ECMC_EC_ENTRY_LINKS_MAX) return nullptr;
  if (bitIndex) *bitIndex = entryInfoArray_[index].bitNumber;
  return accessOwners_[index];
}
