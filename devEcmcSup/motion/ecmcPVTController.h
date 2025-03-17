/*************************************************************************\
* Copyright (c) 2024 Paul Scherrer Institut
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcPVTController.h
*
*  Created on: September 04, 2024
*      Author: anderssandstrom
*
\*************************************************************************/

#ifndef ECMCPVTCONTROLLER_H_
#define ECMCPVTCONTROLLER_H_

#define ERROR_PVT_CTRL_AXIS_COUNT_ZERO 0x242000
#define ERROR_PVT_CTRL_EC_LINK_FUNCTION_UNKNOWN 0x242001

enum ecmcPVTSMType {
  ECMC_PVT_IDLE                         = 0,
  ECMC_PVT_TRIGG_MOVE_AXES_TO_START     = 1,
  ECMC_PVT_WAIT_FOR_AXES_TO_REACH_START = 2,
  ECMC_PVT_TRIGG_PVT                    = 3,
  ECMC_PVT_EXECUTE_PVT                  = 4,
  ECMC_PVT_ABORT                        = 5,
  ECMC_PVT_ERROR                        = 6,
};

#define ECMC_PVT_EC_ENTRY_TRIGGER "pvtctrl.trigg.output"

// For future use when timestamped output might be needed
//#define ECMC_PVT_EC_ENTRY_TRIGGER_TIME "pvtctrl.trigg.timestamp"

#include "ecmcEcEntryLink.h"
#include "ecmcAxisPVTSequence.h"
#include "ecmcAxisBase.h"
#include <vector>

class ecmcPVTController: public ecmcEcEntryLink {
  public:
    ecmcPVTController(double sampleTime);
    ~ecmcPVTController();
    void   addAxis(ecmcAxisBase* axis);
    void   clearPVTAxes();
    size_t getCurrentPointId();
    size_t getCurrentTriggerId();
    double getCurrentTime();
    void   execute();
    void   setExecute(bool execute);
    bool   getBusy();
    void   errorReset();
    int    abortPVT();
    int    setEcEntry(ecmcEcEntry *entry, int entryIndex, int bitIndex);

  private:
    int    triggMoveAxesToStart();
    int    axesAtStart();
    int    triggPVT();
    int    axisNotBusy();
    int    validate();
    int    anyAxisInterlocked();
    void   initPVT();
    void   checkIfTimeToTrigger();    
    double sampleTime_;
    double nextTime_, accTime_, endTime_;
    std::vector<double> startPositions_;
    std::vector<ecmcAxisBase*> axes_;
    bool executeOld_, execute_;
    ecmcPVTSMType state_;
    bool busy_;
    bool triggerDefined_;
};
#endif  /* ECMCPVTCONTROLLER_H_ */
