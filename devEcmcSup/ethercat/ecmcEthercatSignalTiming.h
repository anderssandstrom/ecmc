#ifndef ECMC_ETHERCAT_SIGNAL_TIMING_H_
#define ECMC_ETHERCAT_SIGNAL_TIMING_H_
#include "ecmcDataAccess.h"
class ecmcEc;
class ecmcEcSlave;
void ecmcBindEthercatTiming(ecmcEc &master, ecmcEcSlave &slave,
                           ecmcDataAccess &access);
#endif
