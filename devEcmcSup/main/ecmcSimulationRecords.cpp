// Configuration-time EPICS records for declared simulated I/O fields.
// Device models, axis records and hardware-specific templates are independent.
#include "ecmcIoConfig.h"
#include "ecmcErrorsList.h"
#include "ecmcAsynPortDriver.h"
#include "dbAccess.h"
#include "dbStaticLib.h"
#include "dbDefs.h"
#include "ecmcDefinitions.h"
#include <cctype>
#include <cstdio>
#include <sstream>
#include <set>
#include <string>
#include <vector>

extern app_mode_type appModeCmd, appModeStat;
extern "C" int ecmcIocRunning(void);

namespace {
struct SignalRecord {
  ecmcSimulationDriver *driver;
  ecmcSimulationDriver::Entry *entry;
  int position;
  std::string name, pv, path, readType, writeType, deviceType;
  bool digital;
};
bool validName(const std::string &name) {
  if (name.empty()) return false;
  for (unsigned char ch : name)
    if (!std::isalnum(ch) && ch != ':' && ch != '_' && ch != '-' && ch != '.') return false;
  return true;
}
void writeRecord(std::ostream &db, const SignalRecord &signal, bool output) {
  const std::string pv = signal.pv + (signal.entry->direction == 2 && !output ? "-RB" :
                                    signal.entry->direction == 1 && output ? "-Set" : "");
  db << "record(" << (output ? signal.writeType : signal.readType) << ",\"" << pv << "\") {\n"
     << " field(DESC,\"Simulated I/O " << (output ? "injection" : "readback") << "\")\n"
     << " field(DTYP,\"" << signal.deviceType << "\")\n"
     << " field(" << (output ? "OUT" : "INP") << ",\"@" << (signal.digital ? "asynMask(" : "asyn(")
     << signal.driver->portName() << ",0," << (signal.digital ? "0xffffffff," : "")
     << "1)T_SMP_MS=10/TYPE=" << signal.deviceType << "/sim." << signal.path << (output ? "=" : "?") << "\")\n"
     << " field(PINI,\"" << (output ? "NO" : "YES") << "\")\n"
     << " field(SCAN,\"" << (output ? "Passive" : "I/O Intr") << "\")\n"
     << " info(ecmcPath,\"" << signal.path << "\")\n";
  if (signal.digital) db << " field(NOBT,\"" << signal.entry->access->getBits() << "\")\n";
  db << "}\n";
}
}

int loadSimulationRecords(const char *prefix) {
  if (appModeCmd != ECMC_MODE_CONFIG || appModeStat != ECMC_MODE_CONFIG || ecmcIocRunning())
    return ERROR_MAIN_APP_MODE_ALREADY_RUNTIME;
  if (!prefix || !validName(prefix) || std::string(prefix).find('.') != std::string::npos || !pdbbase) return ERROR_MAIN_ECMC_COMMAND_FORMAT_ERROR;
  try {
    std::vector<SignalRecord> signals;
    std::set<std::string> recordNames;
    DBENTRY dbEntry;
    dbInitEntry(pdbbase, &dbEntry);
    int error = 0;
    for (const auto &driver : ecmcSimulationDrivers()) {
      if (!driver->portName() || !validName(driver->portName())) { error = ERROR_MAIN_ECMC_COMMAND_FORMAT_ERROR; break; }
      for (int index = 0; driver->positionByIndex(index) != -2 && !error; ++index) {
        const int position = driver->positionByIndex(index);
        for (const auto &item : driver->findDevice(position)->entries) {
          auto *entry = item.second.get();
          if (!entry->realtime || item.first == "ZERO" || item.first == "ONE") continue;
          if (!entry->recordsPrefix.empty()) {
            if (entry->recordsPrefix != prefix) error = ERROR_MAIN_IO_BACKEND_LOCKED;
            continue;
          }
          SignalRecord signal;
          signal.driver = driver.get(); signal.entry = entry;
          signal.position = position; signal.name = item.first;
          signal.path = "ec" + std::to_string(driver->id()) + ".s" + std::to_string(position) + "." + item.first;
          signal.pv = std::string(prefix) + "ec" + std::to_string(driver->id()) +
            ":s" + std::to_string(position) + ":" + item.first;
          signal.digital = false;
          const auto type = entry->access->type;
          if (type == ECMC_EC_F32 || type == ECMC_EC_F64) {
            signal.readType = "ai"; signal.writeType = "ao"; signal.deviceType = "asynFloat64";
          } else if (entry->access->getBits() == 64) {
#ifdef ECMC_ASYN_ASYNPARAMINT64
            signal.readType = "int64in"; signal.writeType = "int64out"; signal.deviceType = "asynInt64";
#else
            error = ERROR_MAIN_IO_CONFIG_UNSUPPORTED; break;
#endif
          } else if (type == ECMC_EC_S8 || type == ECMC_EC_S16 || type == ECMC_EC_S32) {
            signal.readType = "longin"; signal.writeType = "longout"; signal.deviceType = "asynInt32";
          } else {
            signal.readType = "mbbiDirect"; signal.writeType = "mbboDirect";
            signal.deviceType = "asynUInt32Digital"; signal.digital = true;
          }
          // Check every name/type before registering views or adding records.
          const std::string companion = signal.pv + (entry->direction == 2 ? "-RB" : "-Set");
          if (!recordNames.insert(signal.pv).second ||
              !recordNames.insert(companion).second ||
              !validName(signal.pv) || companion.size() >= PVNAME_STRINGSZ ||
              dbFindRecordType(&dbEntry, signal.readType.c_str()) ||
              dbFindRecordType(&dbEntry, signal.writeType.c_str()) ||
              !dbFindRecord(&dbEntry, signal.pv.c_str()) ||
              !dbFindRecord(&dbEntry, companion.c_str())) {
            error = ERROR_MAIN_SIM_RECORDS_LOAD_FAILED; break;
          }
          signals.push_back(std::move(signal));
        }
      }
      if (error) break;
    }
    dbFinishEntry(&dbEntry);
    if (error || signals.empty()) return error;
    std::ostringstream database;
    for (const auto &signal : signals) {
      error = signal.driver->prepareSimulationParameter(signal.position, signal.name.c_str());
      if (error) return error;
      writeRecord(database, signal, true);
      writeRecord(database, signal, false);
    }
    const std::string text = database.str();
    FILE *file = tmpfile();
    if (!file) return ERROR_MAIN_SIM_RECORDS_LOAD_FAILED;
    if (fwrite(text.data(), 1, text.size(), file) != text.size() || fseek(file, 0, SEEK_SET)) {
      fclose(file); return ERROR_MAIN_SIM_RECORDS_LOAD_FAILED;
    }
    // EPICS owns and closes file, including on failure.
    if (dbReadDatabaseFP(&pdbbase, file, nullptr, nullptr)) return ERROR_MAIN_SIM_RECORDS_LOAD_FAILED;
    for (const auto &signal : signals) signal.entry->recordsPrefix = prefix;
    return 0;
  } catch (...) { return ERROR_MAIN_EXCEPTION; }
}
