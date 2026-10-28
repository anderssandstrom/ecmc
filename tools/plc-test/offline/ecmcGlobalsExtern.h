// Small private declaration boundary; do not pull IOC/motor/thread globals in.
#pragma once
#include "ecmcDefinitions.h"
class ecmcAxisBase;
class ecmcEc;
extern ecmcAxisBase *axes[ECMC_MAX_AXES];
extern ecmcEc *ec;
extern app_mode_type appModeStat;
