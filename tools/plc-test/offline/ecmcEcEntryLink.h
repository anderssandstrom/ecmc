// Hardware entry boundary for the production encoder/monitor/position compare.
// Virtual axes use external feedback. No hardware entry can be linked here.
#pragma once
#include "ecmcEcEntry.h"
#include "ecmcErrorsList.h"
#include <stdexcept>
class ecmcEcEntryLink : public ecmcError {
 public:
  ecmcEcEntryLink() = default;
  ecmcEcEntryLink(int *error, int *warning) : ecmcError(error, warning) {}
  int setEntryAtIndex(ecmcEcEntry *, int, int) {
    throw std::logic_error("hardware entries unavailable in virtual-axis tests");
  }
  int validateEntry(int) { return ERROR_MAIN_EC_ENTRY_NULL; }
  int validateEntryBit(int) { return ERROR_MAIN_EC_ENTRY_NULL; }
  bool checkEntryExist(int) { return false; }
  bool checkDomainOK(int) { return false; }
  bool checkDomainOKAllEntries() { return true; } // no linked hardware entries
  int readEcEntryValue(int, uint64_t *) { return ERROR_MAIN_EC_ENTRY_NULL; }
  int readEcEntryValueDouble(int, double *) { return ERROR_MAIN_EC_ENTRY_NULL; }
  int readEcEntryBits(int, int, uint64_t *) { return ERROR_MAIN_EC_ENTRY_NULL; }
  int writeEcEntryValue(int, uint64_t) { return ERROR_MAIN_EC_ENTRY_NULL; }
  int writeEcEntryValueDouble(int, double) { return ERROR_MAIN_EC_ENTRY_NULL; }
  int writeEcEntryBits(int, int, uint64_t) { return ERROR_MAIN_EC_ENTRY_NULL; }
 protected:
  int getEntryBitCount(int, int *bits) { *bits = 0; return ERROR_MAIN_EC_ENTRY_NULL; }
  int getEntryStartBit(int, int *bit) { *bit = 0; return ERROR_MAIN_EC_ENTRY_NULL; }
  ecmcEcDataType getEntryDataType(int) { return ECMC_EC_NONE; }
  int getSlaveId(int) { return -1; }
};
