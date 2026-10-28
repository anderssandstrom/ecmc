#pragma once
#include "ecmcEcEntryLink.h"
class ecmcDataLink : public ecmcEcEntryLink {
public:
  using ecmcEcEntryLink::ecmcEcEntryLink;
  int setDataAtIndex(ecmcEcEntry *e, int i, int bit) { return setEntryAtIndex(e, i, bit); }
  int validateData(int i) { return validateEntry(i); }
  bool checkDataExist(int i) { return checkEntryExist(i); }
  int readDataValue(int i, uint64_t *v) { return readEcEntryValue(i, v); }
  int writeDataValue(int i, uint64_t v) { return writeEcEntryValue(i, v); }
  int writeDataValueDouble(int i, double v) { return writeEcEntryValueDouble(i, v); }
protected:
  int getDataBitCount(int i, int *v) { return getEntryBitCount(i, v); }
  int getDataStartBit(int i, int *v) { return getEntryStartBit(i, v); }
  ecmcEcDataType getDataType(int i) { return getEntryDataType(i); }
};
