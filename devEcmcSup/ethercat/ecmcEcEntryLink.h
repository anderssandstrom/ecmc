/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcEcEntryLink.h
*
*  Created on: May 23, 2016
*      Author: anderssandstrom
*
\*************************************************************************/

#ifndef ECMCECENTRYLINK_H_
#define ECMCECENTRYLINK_H_
#ifdef __cplusplus
/**
 * @file ecmcEcEntryLink.h
 * @brief Aggregates EtherCAT entries for grouped read/write and validation.
 */
#endif

#include "ecmcEcEntry.h"

#define ECMC_EC_ENTRY_LINKS_MAX 20

struct entryInfo {
  ecmcEcEntry *entry;
  int          bitNumber;
};

class ecmcEcEntryLink : public ecmcError {
public:
  ecmcEcEntryLink();
  ecmcEcEntryLink(int *errorPtr,
                  int *warningPtr);
  ~ecmcEcEntryLink();
  void           initVars();

  /** @brief Set entry pointer and optional bit index at given slot. */
  int            setEntryAtIndex(ecmcEcEntry *entry,
                                 int          index,
                                 int          bitIndex);
  /** @brief Validate that an entry exists at index. */
  int            validateEntry(int index);
  /** @brief Read raw value from linked entry (domain checked). */
  int            readEcEntryValue(int       entryIndex,
                                  uint64_t *value);
  /** @brief Read double value from linked entry. */
  int            readEcEntryValueDouble(int     entryIndex,
                                        double *value);
  /** @brief Read bits from linked entry starting at stored bit offset. */
  int            readEcEntryBits(int entryIndex,
                                 int bits,
                                 uint64_t *value);
  /** @brief Write raw value to linked entry (domain checked). */
  int            writeEcEntryValue(int      entryIndex,
                                   uint64_t value);

  /** @brief Write double value to linked entry. */
  int            writeEcEntryValueDouble(int    entryIndex,
                                         double value);
  /** @brief Write bits to linked entry starting at stored bit offset. */
  int            writeEcEntryBits(int entryIndex, 
                                  int bits,       // Note: start bit is already stored in entry
                                  uint64_t value);
  /** @brief Check if entry exists at index. */
  bool           checkEntryExist(int entryIndex);
  /** @brief True if entry domain is OK. */
  bool           checkDomainOK(int entryIndex);
  /** @brief True if all linked entries have OK domains. */
  bool           checkDomainOKAllEntries();

protected:
  int            validateEntryBit(int index);
  int            getEntryBitCount(int  index,
                                  int *bitCount);
  int            getEntryStartBit(int  index,
                                  int *startBit);
  ecmcEcDataType getEntryDataType(int index);
  int            getSlaveId(int index);

private:
  entryInfo entryInfoArray_[ECMC_EC_ENTRY_LINKS_MAX];
};

#endif  /* ECMCECENTRYLINK_H_ */
