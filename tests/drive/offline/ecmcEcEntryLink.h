#pragma once
#include "ecmcEcEntry.h"
#include <array>
class ecmcEcEntryLink : public ecmcError {
 public:
  ecmcEcEntryLink() = default;
  ecmcEcEntryLink(int *error, int *warning) : ecmcError(error, warning) {}
  int setEntryAtIndex(ecmcEcEntry *entry, int index, int bit) {
    if (index < 0 || index >= int(entries_.size()) || bit != -1)
      return ERROR_MAIN_EC_ENTRY_NULL;
    entries_[index] = entry;
    return 0;
  }
  bool checkEntryExist(int index) {
    return index >= 0 && index < int(entries_.size()) && entries_[index];
  }
  int validateEntry(int index) { return checkEntryExist(index) ? 0 : ERROR_MAIN_EC_ENTRY_NULL; }
  int readEcEntryValue(int index, uint64_t *value) {
    if (validateEntry(index)) return ERROR_MAIN_EC_ENTRY_NULL;
    if (entries_[index]->readError) return entries_[index]->readError;
    *value = entries_[index]->value;
    return 0;
  }
  int writeEcEntryValue(int index, uint64_t value) {
    if (validateEntry(index)) return ERROR_MAIN_EC_ENTRY_NULL;
    if (entries_[index]->writeError) return entries_[index]->writeError;
    entries_[index]->value = value;
    return 0;
  }
  int writeEcEntryValueDouble(int, double) { return ERROR_MAIN_EC_ENTRY_NULL; }
 protected:
  int getEntryBitCount(int index, int *bits) {
    if (validateEntry(index)) return ERROR_MAIN_EC_ENTRY_NULL;
    *bits = entries_[index]->bits; return 0;
  }
  int getEntryStartBit(int index, int *bit) {
    if (validateEntry(index)) return ERROR_MAIN_EC_ENTRY_NULL;
    *bit = -1; return 0;
  }
  ecmcEcDataType getEntryDataType(int index) {
    return checkEntryExist(index) ? entries_[index]->type : ECMC_EC_NONE;
  }
 private:
  std::array<ecmcEcEntry *, 20> entries_{};
};
