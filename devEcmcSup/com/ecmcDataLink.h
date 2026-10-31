/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcDataLink.h
*
*  Created on: May 23, 2016
*      Author: anderssandstrom
*
\*************************************************************************/

#ifndef ECMC_DATA_LINK_H_
#define ECMC_DATA_LINK_H_

#include "ecmcDataAccess.h"
#include "ecmcDataErrors.h"
#include "ecmcError.h"

#define ECMC_DATA_LINKS_MAX 20
#define ECMC_EC_ENTRY_LINKS_MAX ECMC_DATA_LINKS_MAX

struct ecmcDataLinkInfo {
  ecmcDataAccess *entry;
  int          bitNumber;
};

class ecmcMotionStateYamlWriter;

class ecmcDataLink : public ecmcError {
  friend class ecmcMotionStateYamlWriter;
public:
  ecmcDataLink();
  ecmcDataLink(int *errorPtr,
                  int *warningPtr);
  ~ecmcDataLink();
  void           initVars();

  int            setDataAtIndex(const ecmcDataAccess &entry,
                                 int          index,
                                 int          bitIndex);
  template <typename Provider>
  int setDataAtIndex(Provider *provider, int index, int bitIndex) {
    if (!provider) return ERROR_EC_ENTRY_LINK_FAILED;
    try { return setDataAtIndex(provider->getDataAccess(), index, bitIndex); }
    catch (...) { return ERROR_EC_ENTRY_LINK_FAILED; }
  }
  int setDataAtIndex(std::shared_ptr<ecmcDataAccess> entry, int index, int bitIndex);
  int clearDataAtIndex(int index);
  std::shared_ptr<ecmcDataAccess> getDataAtIndex(int index, int *bitIndex) const;
  int            validateData(int index);
  bool readDataTime(int index, ecmcSignalTime &time) const;
  int            readDataValue(int       entryIndex,
                                  uint64_t *value);
  int            readDataValueDouble(int     entryIndex,
                                        double *value);
  int            readDataBits(int entryIndex,
                                 int bits,
                                 uint64_t *value);
  int            writeDataValue(int      entryIndex,
                                   uint64_t value);

  int            writeDataValueDouble(int    entryIndex,
                                         double value);
  int            writeDataBits(int entryIndex, 
                                  int bits,       // Note: start bit is already stored in entry
                                  uint64_t value);
  bool           checkDataExist(int entryIndex);
  bool           checkDataValid(int entryIndex);
  bool           checkAllDataValid();

  // Historical spellings retained for source compatibility; new code uses
  // the neutral data methods. These wrappers add no provider or value layer.
  template <typename Provider>
  int setEntryAtIndex(Provider *provider, int index, int bitIndex) {
    return setDataAtIndex(provider, index, bitIndex);
  }
  int readEcEntryValueDouble(int     entryIndex,
                                        double *value) { return readDataValueDouble(entryIndex, value); }
  int readEcEntryValue(int       entryIndex,
                                  uint64_t *value) { return readDataValue(entryIndex, value); }
  int writeEcEntryValueDouble(int    entryIndex,
                                         double value) { return writeDataValueDouble(entryIndex, value); }
  int writeEcEntryValue(int      entryIndex,
                                   uint64_t value) { return writeDataValue(entryIndex, value); }
  int readEcEntryBits(int entryIndex,
                                 int bits,
                                 uint64_t *value) { return readDataBits(entryIndex, bits, value); }
  int writeEcEntryBits(int entryIndex,
                                  int bits,       // Note: start bit is already stored in entry
                                  uint64_t value) { return writeDataBits(entryIndex, bits, value); }
  bool checkDomainOKAllEntries() { return checkAllDataValid(); }
  bool checkDomainOK(int entryIndex) { return checkDataValid(entryIndex); }
  bool checkEntryExist(int entryIndex) { return checkDataExist(entryIndex); }
  ecmcEcDataType getEntryDataType(int index) { return getDataType(index); }
  int getEntryBitCount(int  index,
                                  int *bitCount) { return getDataBitCount(index, bitCount); }
  int getEntryStartBit(int  index,
                                  int *startBit) { return getDataStartBit(index, startBit); }
  int getSlaveId(int index) { return getSourceId(index); }
  int validateEntryBit(int index) { return validateDataBit(index); }
  int validateEntry(int index) { return validateData(index); }
  int setDomainError(const char *fileName,
                                const char *functionName,
                                int         lineNumber) { return setDataError(fileName, functionName, lineNumber); }

protected:
  int            setDataError(const char *fileName,
                                const char *functionName,
                                int         lineNumber);
  int            validateDataBit(int index);
  int            getDataBitCount(int  index,
                                  int *bitCount);
  int            getDataStartBit(int  index,
                                  int *startBit);
  ecmcEcDataType getDataType(int index);
  int            getSourceId(int index);

private:
  // Keep hot slots pointer+bit, as before. Ownership is only touched at bind,
  // unlink and destruction; reference counts are never changed during access.
  ecmcDataLinkInfo entryInfoArray_[ECMC_EC_ENTRY_LINKS_MAX];
  std::shared_ptr<ecmcDataAccess> accessOwners_[ECMC_EC_ENTRY_LINKS_MAX];
};

#endif  /* ECMC_DATA_LINK_H_ */
