/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcPlugin.cpp
*
*  Created on: Oct 21, 2020
*      Author: anderssandstrom
*
\*************************************************************************/

#include "ecmcPluginClient.h"
#include "ecmcOctetIF.h"        // Log Macros
#include "ecmcDefinitions.h"
#include "ecmcErrorsList.h"

// TODO: REMOVE GLOBALS
#include "ecmcGlobalsExtern.h"

void* getEcmcDataItem(char *idStringWP) {
  LOGINFO4("%s/%s:%d: idStringWP =%s\n",
           __FILE__,
           __FUNCTION__,
           __LINE__,
           idStringWP);

  if (!asynPort)return NULL;

  return (void *)asynPort->findAvailDataItem(idStringWP);
}

void* getEcmcAsynDataItem(char *idStringWP) {
  LOGINFO4("%s/%s:%d: idStringWP =%s\n",
           __FILE__,
           __FUNCTION__,
           __LINE__,
           idStringWP);

  if (!asynPort)return NULL;

  return (void *)asynPort->findAvailDataItem(idStringWP);
}

void* getEcMaster() {
  return (void *)ec;
}

int getEcmcMasterIndex() {
  if (!ec) {
    return -1;
  }
  return ec->getMasterIndex();
}

uint32_t getEcmcSlaveStateWord(int masterIndex, int slaveIndex) {
  if (!ec) {
    return 0u;
  }

  if (masterIndex >= 0 && ec->getMasterIndex() != masterIndex) {
    return 0u;
  }

  ecmcEcSlave* slave = ec->getSlave(slaveIndex);
  if (!slave) {
    return 0u;
  }

  ec_slave_config_state_t state {};
  if (slave->getSlaveState(&state) != 0) {
    return 0u;
  }

  uint32_t word = 0u;
  word |= 1u << 0;
  if (state.online) {
    word |= 1u << 1;
  }
  if (state.operational) {
    word |= 1u << 2;
  }
  word |= (static_cast<uint32_t>(state.al_state) & 0xFu) << 3;
  return word;
}

uint32_t getEcmcMasterStateWord(int masterIndex) {
  if (!ec) {
    return 0u;
  }

  if (masterIndex >= 0 && ec->getMasterIndex() != masterIndex) {
    return 0u;
  }

  ec_master_t* master = ec->getMaster();
  if (!master) {
    return 0u;
  }

  ec_master_state_t state {};
  ecrt_master_state(master, &state);

  uint32_t word = 0u;
  word |= 1u << 0;
  if (state.link_up) {
    word |= 1u << 1;
  }
  word |= (static_cast<uint32_t>(state.al_states) & 0xFu) << 2;
  word |= (static_cast<uint32_t>(state.slaves_responding) & 0xFFFFu) << 16;
  return word;
}

void* getEcmcAsynPortDriver() {
  LOGINFO4("%s/%s:%d:\n",
           __FILE__,
           __FUNCTION__,
           __LINE__);

  return (void *)asynPort;
}

double getEcmcSampleRate() {
  LOGINFO4("%s/%s:%d:\n",
           __FILE__,
           __FUNCTION__,
           __LINE__);

  return mcuFrequency;
}

double getEcmcSampleTimeMS() {
  // mcuPeriod is in nano seconds
  return mcuPeriod / 1E6;
}

int getEcmcEpicsIOCState() {
  if (!asynPort) {
    return -1;
  }
  return asynPort->getEpicsState();
}
