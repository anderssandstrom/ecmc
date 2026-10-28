// In-memory drive I/O boundary. No EtherCAT transport or register conversion.
#pragma once
#include "ecmcError.h"
#include "ecmcDefinitions.h"
#include "ecmcErrorsList.h"
#define ERROR_EC_ENTRY_EC_DOMAIN_ERROR 0x2100E
struct ecmcEcEntry {
  uint64_t value = 0;
  int readError = 0, writeError = 0;
  int bits = 16;
  ecmcEcDataType type = ECMC_EC_U16;
};
