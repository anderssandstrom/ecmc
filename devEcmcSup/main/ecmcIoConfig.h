#ifndef ECMC_IO_CONFIG_H_
#define ECMC_IO_CONFIG_H_

#ifdef __cplusplus
#include "ecmcSimulationDriver.h"

// Configuration ownership only; none of these functions resolve names in RT.
const std::vector<std::unique_ptr<ecmcSimulationDriver>> &ecmcSimulationDrivers();
ecmcSimulationDriver *ecmcFindSimulation(int id);
ecmcSimulationDriver *ecmcSelectedSimulation();
void ecmcSelectIoConfiguration(int id);
int ecmcIoConfigurationId();
void ecmcSetIoConfigurationContext(int id);
class ecmcIoConfigurationScope {
public:
  explicit ecmcIoConfigurationScope(int id) : previous_(ecmcIoConfigurationId()) {
    ecmcSetIoConfigurationContext(id);
  }
  ~ecmcIoConfigurationScope() { ecmcSetIoConfigurationContext(previous_); }
private:
  int previous_;
};
// Configuration lookup only. Both backends expose the same bound descriptor.
int ecmcResolveIoEntry(int slave, const char *name, std::shared_ptr<ecmcDataAccess> &access);
int ecmcEnableIoMotionChecks(int slave);
int ecmcValidateSimulations();
void ecmcClearSimulations();

extern "C" {
#endif
// backend: 0=EtherCAT (default), 1=memory simulation. Before EcSetMaster/devices.
int setIoBackend(int id, int backend);
int setSimulationValue(const char *path, const char *value);
int setSimulationHealthy(int id, int healthy);
int loadSimulationRecords(const char *prefix);
#ifdef __cplusplus
}
#endif
#endif
