#pragma once
#include "ecmcError.h"
#include "ecmcDefinitions.h"
#include "ecmcDataAccess.h"
#include <stdexcept>
#define ERROR_EC_ENTRY_INVALID_BIT_LENGTH 0x21003
#define ERROR_EC_ENTRY_EC_DOMAIN_ERROR 0x2100E
#define ERROR_EC_ENTRY_WRITE_FAIL 0x21008
class ecmcEcEntry {
 public:
  std::shared_ptr<ecmcDataAccess> getDataAccess() { throw std::logic_error("hardware bindings unavailable in virtual-axis tests"); }
  int readValue(uint64_t *) {
    throw std::logic_error("hardware input unavailable in virtual-axis tests");
  }
  int writeValue(uint64_t) {
    throw std::logic_error("hardware output unavailable in virtual-axis tests");
  }
};
