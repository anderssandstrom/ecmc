#include "ecmcIoConfig.h"
#include "ecmcEthercatSignalTiming.h"
#include "ecmcGlobalsExtern.h"
#include "ecmcAsynPortDriverUtils.h"
#include "ecmcErrorsList.h"
#include "ecmcDataErrors.h"
#include <set>

namespace {
struct IoConfiguration {
  std::vector<std::unique_ptr<ecmcSimulationDriver>> simulations;
  std::vector<ecmcIoDriver *> cyclicDrivers;
  std::set<int> selected;
  int current = -1;
};
IoConfiguration &configuration() {
  // Lifetime is controlled by ecmcCleanup, before asyn is torn down.
  static auto *state = new IoConfiguration;
  return *state;
}
}

const std::vector<std::unique_ptr<ecmcSimulationDriver>> &ecmcSimulationDrivers() {
  return configuration().simulations;
}
const std::vector<ecmcIoDriver *> &ecmcAdditionalIoDrivers() {
  return configuration().cyclicDrivers;
}
ecmcSimulationDriver *ecmcFindSimulation(int id) {
  for (const auto &driver : configuration().simulations) if (driver->id() == id) return driver.get();
  return nullptr;
}
ecmcSimulationDriver *ecmcSelectedSimulation() { return ecmcFindSimulation(configuration().current); }
int ecmcIoConfigurationId() { return configuration().current; }
void ecmcSetIoConfigurationContext(int id) { configuration().current = id; }
void ecmcSelectIoConfiguration(int id) {
  configuration().selected.insert(id);
  configuration().current = id;
}

int setIoBackend(int id, int backend) {
  if (appModeCmd != ECMC_MODE_CONFIG || appModeStat != ECMC_MODE_CONFIG)
    return ERROR_MAIN_APP_MODE_ALREADY_RUNTIME;
  if (id < 0 || id >= ECMC_SHM_MAX_MASTERS || (backend != 0 && backend != 1))
    return ERROR_MAIN_ECMC_LINK_INVALID;
  auto *existing = ecmcFindSimulation(id);
  if ((backend == 1) == (existing != nullptr)) return 0;
  if (existing || configuration().selected.count(id) || (ec && ec->getInitDone() && ec->getMasterIndex() == id))
    return ERROR_MAIN_IO_BACKEND_LOCKED;
  try {
    if (backend) {
      auto driver = std::make_unique<ecmcSimulationDriver>(id, asynPort);
      if (driver->configurationError()) return driver->configurationError();
      int error = driver->addDevice(0, -1, 0, 0);
      if (error) return error;
      // The original ECMC object already publishes ec0.s-1.ZERO/ONE. Share its
      // descriptors so PVs and links continue to access exactly the same storage.
      if (id == 0 && ec) {
        auto *constants = ec->getSlave(-1);
        if (constants) for (const char *name : {"ZERO", "ONE"}) {
          auto *entry = constants->findEntry(name);
          if (entry) {
            error = driver->importEntry(-1, name, entry->getDataAccess());
            if (error) return error;
          }
        }
      }
      // Reserve both containers before committing either membership change.
      configuration().cyclicDrivers.reserve(configuration().cyclicDrivers.size() + 1);
      configuration().simulations.reserve(configuration().simulations.size() + 1);
      configuration().cyclicDrivers.push_back(driver.get());
      configuration().simulations.push_back(std::move(driver));
    }
  } catch (...) { return ERROR_MAIN_EXCEPTION; }
  return 0;
}

int setSimulationValue(const char *path, const char *value) {
  if (!path || !value) return ERROR_MAIN_ECMC_LINK_INVALID;
  int master = -1, slave = -1, bit = -1;
  char alias[EC_MAX_OBJECT_PATH_CHAR_LENGTH];
  int error = parseEcPath(const_cast<char *>(path), &master, &slave, alias, &bit);
  if (error) return error;
  auto *driver = ecmcFindSimulation(master);
  if (!driver || bit >= 0) return ERROR_MAIN_ECMC_LINK_INVALID;
  return driver->setValue(slave, alias, value);
}

int setSimulationHealthy(int id, int healthy) {
  if (healthy != 0 && healthy != 1) return ERROR_MAIN_ECMC_LINK_INVALID;
  auto *driver = ecmcFindSimulation(id);
  if (!driver) return ERROR_MAIN_ECMC_LINK_INVALID;
  driver->setHealthy(healthy != 0);
  return 0;
}

int ecmcValidateSimulations() {
  for (const auto &driver : configuration().simulations) {
    if (!configuration().selected.count(driver->id())) continue;
    int error = driver->validate();
    if (error) return error;
  }
  return 0;
}
void ecmcClearSimulations() {
  configuration().cyclicDrivers.clear();
  configuration().simulations.clear();
  configuration().selected.clear();
  configuration().current = -1;
}

int ecmcResolveIoEntry(int master, int slave, const char *name,
                       std::shared_ptr<ecmcDataAccess> &access) {
  access.reset();
  if (!name) return ERROR_MAIN_EC_ENTRY_NULL;
  if (auto *simulation = ecmcFindSimulation(master)) {
    if (!simulation->findDevice(slave)) return ERROR_MAIN_EC_SLAVE_NULL;
    auto *entry = simulation->findEntry(slave, name);
    if (!entry) return ERROR_MAIN_EC_ENTRY_NULL;
    access = entry->access;
  } else {
    if (!ec) return ERROR_MAIN_EC_NOT_INITIALIZED;
    if (master != ec->getMasterIndex()) return ERROR_MAIN_EC_INDEX_OUT_OF_RANGE;
    auto *device = slave >= 0 ? ec->findSlave(slave) : ec->getSlave(slave);
    if (!device) return ERROR_MAIN_EC_SLAVE_NULL;
    auto *entry = device->findEntry(std::string(name));
    if (!entry) return ERROR_MAIN_EC_ENTRY_NULL;
    try {
      access = entry->getDataAccess();
      if (access) ecmcBindEthercatTiming(*ec, *device, *access);
    }
    catch (...) { return ERROR_EC_ENTRY_LINK_FAILED; }
  }
  return access ? 0 : ERROR_MAIN_EC_ENTRY_NULL;
}

int ecmcResolveIoEntry(int slave, const char *name,
                       std::shared_ptr<ecmcDataAccess> &access) {
  const int master = ecmcIoConfigurationId();
  if (ecmcFindSimulation(master) &&
      (appModeCmd != ECMC_MODE_CONFIG || appModeStat != ECMC_MODE_CONFIG))
    return ERROR_MAIN_APP_MODE_ALREADY_RUNTIME;
  // Legacy numeric commands use the selected backend, or the current real master.
  return ecmcResolveIoEntry(master < 0 && ec ? ec->getMasterIndex() : master,
                            slave, name, access);
}

int ecmcEnableIoMotionChecks(int slave) {
  if (auto *simulation = ecmcSelectedSimulation())
    return simulation->setSetting(slave, "enableSDOCheck", {1});
  if (!ec) return ERROR_MAIN_EC_NOT_INITIALIZED;
  auto *device = slave >= 0 ? ec->findSlave(slave) : ec->getSlave(slave);
  return device ? device->setEnableSDOCheck(1) : ERROR_MAIN_EC_SLAVE_NULL;
}
