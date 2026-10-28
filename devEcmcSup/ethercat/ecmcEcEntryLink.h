#ifndef ECMCECENTRYLINK_H_
#define ECMCECENTRYLINK_H_
#include "ecmcEcEntry.h"
#include "ecmcDataLink.h"
// Legacy configuration record retained for external source compatibility.
struct entryInfo { ecmcEcEntry *entry; int bitNumber; };
class ecmcEcEntryLink : public ecmcDataLink {
public:
  using ecmcDataLink::ecmcDataLink;
  int setEntryAtIndex(ecmcEcEntry *entry, int index, int bitIndex) {
    return setDataAtIndex(entry, index, bitIndex);
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
};
#endif
