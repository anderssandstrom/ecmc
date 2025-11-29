/*************************************************************************\
* Copyright (c) 2023 Paul Scherrer Institute
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcEcDomain.h
*
*  Created on: Sept 29, 2023
*      Author: anderssandstrom
*
\*************************************************************************/

#ifndef ECMCECDOMAIN_H_
#define ECMCECDOMAIN_H_
#ifdef __cplusplus
/**
 * @file ecmcEcDomain.h
 * @brief Encapsulates EtherCAT domain lifecycle, state checks, and asyn exposure.
 */
#endif

#include "ecrt.h"
#include "ecmcDefinitions.h"
#include "ecmcError.h"
#include "ecmcAsynPortDriver.h"
#include "ecmcErrorsList.h"

/**
 * @brief Handles EtherCAT domain creation, processing, and diagnostics.
 */
class ecmcEcDomain : public ecmcError {
public:
  ecmcEcDomain(ecmcAsynPortDriver *asynPortDriver,
               ec_master_t        *master,
               int                 masterIndex,
               int                 objIndex,
               int                 exeCycles,
               int                 offsetCycles);
  ~ecmcEcDomain();
  /** @brief Return EtherCAT domain handle. */
  ec_domain_t* getDomain();
  /** @brief Allow domain to be offline. */
  int          setAllowOffline(int allow);
  /** @brief Get allow-offline flag. */
  int          getAllowOffline();
  /** @brief Update domain state and status word. */
  int          checkState();
  /** @brief Process incoming data at scheduled cycle. */
  void         process();
  /** @brief Queue outgoing data at scheduled cycle. */
  void         queue();
  /** @brief Refresh asyn parameters. */
  void         updateAsyn();
  /** @brief Register domain diagnostics in asyn. */
  int          initAsyn();
  /** @brief Return configured domain size. */
  size_t       getSize();
  /** @brief Set allowed consecutive failure cycles. */
  int          setFailedCyclesLimitInterlock(int cycles);
  /** @brief Background hook to reset counters. */
  void         slowExecute();
  /** @brief Pointer to process image data. */
  uint8_t*     getDataPtr();
  /** @brief Domain OK status. */
  int          getOK();

private:
  void         initVars();
  int objIndex_;
  int masterIndex_;
  ec_master_t *master_;
  ec_domain_t *domain_;

  // ec_domain_state_t stateOld_;
  ec_domain_state_t state_;
  int statusOk_;
  uint32_t statusWordOld_;
  int notOKCounter_;
  int notOKCounterTotal_;
  int notOKCounterMax_;
  int notOKCyclesLimit_;

  size_t size_;
  uint32_t statusWord_;
  int allowOffLine_;
  ecmcAsynPortDriver *asynPortDriver_;
  ecmcAsynDataItem *asynParStat_;
  ecmcAsynDataItem *asynParFailCount_;
  uint8_t *domainPd_;
  int exeCycles_;
  int offsetCycles_;
  int cycleCounter_;
};
#endif  /* ECMCECDOMAIN_H_ */
