/*************************************************************************\
* Copyright (c) 2024 Paul Scherrer Institut
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcAxisGroup.h
*
*  Created on: Mar 28, 2024
*      Author: anderssandstrom
*
\*************************************************************************/

#ifndef ECMCAXISGROUP_H_
#define ECMCAXISGROUP_H_

#include "ecmcError.h"
#include "ecmcAxisBase.h"
#include "ecmcDefinitions.h"
#include <stdexcept>
#include <vector>
#include <string>

class ecmcAxisGroup : public ecmcError {
  public:
    ecmcAxisGroup(const char *grpName);
    ~ecmcAxisGroup();
    // Add axis to group
    void addAxis(ecmcAxisBase *axis);
    // Check if all axes in group are enable
    bool getEnable();
    // Check if all axes in group are not enable
    bool getDisable();
    // Check if at least one axis in group are enable
    bool getAnyEnable();
    // Check if all axes in group are enabled
    bool getEnabled();
    // Check if all axes in group are not not enabled
    bool getDisabled();
    // Check if at least one axis in group are enabled
    bool getAnyEnabled();
    // Check if all axes in group are busy
    bool getBusy();
    // Check if all axes in group are not busy
    bool getFree();
    // Check if at least one axis in group are busy
    bool getAnyBusy();
    // Check if at least one axis in group are in error state
    int getAnyErrorId();
    // Set enable of all axes in group
    int setEnable(bool enable);
    // set traj source of all axes in group
    int setTrajSrc(dataSource trajSource);
    // set enc source of all axes in group
    int setEncSrc(dataSource encSource);
    // Reset errors all axes
    void setErrorReset();
    // Set errors all axes
    void setError(int error);
    // Set slaved axis error all axes
    void setSlavedAxisInError();
    // Stop motion
    void stop();
    
  private:
    std::string name_;    
    std::vector<ecmcAxisBase*>  axes_;
    size_t axesCounter_;
    std::vector<int> axesIds_;

}
#endif  /* ECMCAXISGROUP_H_ */