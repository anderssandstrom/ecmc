#include <cassert>
#include <iostream>

#define ECMC_TEST_STUBS 1

#include "tests/mocks/EcmcTestStubs.h"

asynUser *pPrintOutAsynUser = nullptr;
unsigned int debug_print_flags = 0;
unsigned int die_on_error_flags = 0;

namespace {

void testWriteAndReadFullValue() {
  ecmcEcEntry entry;
  ecmcEcEntryLink link;
  assert(link.setEntryAtIndex(&entry, 0, -1) == 0);

  const uint64_t value = 0xA5A5A5A5ULL;
  assert(link.writeEcEntryValue(0, value) == 0);

  uint64_t readBack = 0;
  assert(link.readEcEntryValue(0, &readBack) == 0);
  assert(readBack == value);
}

void testWriteAndReadBits() {
  ecmcEcEntry entry;
  ecmcEcEntryLink link;
  assert(link.setEntryAtIndex(&entry, 0, -1) == 0);

  // Write into lower 8 bits and verify mask/shift handling.
  assert(link.writeEcEntryBits(0, 8, 0xAB) == 0);
  uint64_t readBits = 0;
  assert(link.readEcEntryBits(0, 8, &readBits) == 0);
  assert(readBits == 0xAB);
}

void testEntryExistenceChecks() {
  ecmcEcEntry entry;
  ecmcEcEntryLink link;
  link.setEntryAtIndex(&entry, 1, -1);
  assert(link.checkEntryExist(1));
  assert(!link.checkEntryExist(2));
}

void testInvalidIndexIsRejected() {
  ecmcEcEntry entry;
  ecmcEcEntryLink link;
  assert(link.setEntryAtIndex(&entry, ECMC_EC_ENTRY_LINKS_MAX + 1, -1) != 0);
}

}  // namespace

int main() {
  testWriteAndReadFullValue();
  testWriteAndReadBits();
  testEntryExistenceChecks();
  testInvalidIndexIsRejected();
  std::cout << "EtherCAT entry link tests passed\n";
  return 0;
}
