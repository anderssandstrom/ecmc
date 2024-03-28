/*************************************************************************\
* Copyright (c) 2024 Paul Scherrer Institut
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcAxisGroup.cpp
*
*  Created on: Mar 28, 2024
*      Author: anderssandstrom
*
\*************************************************************************/

ecmcAxisGroup::ecmcAxisGroup(const char *grpName){
  name_ = grpName;
  axesCounter_ = 0;
};

ecmcAxisGroup::~ecmcAxisGroup(){

};

// Add axis to group
void ecmcAxisGroup::addAxis(ecmcAxisBase *axis){
  if(!axis) {
    throw std::runtime_error( "Axis NULL");
  }
  axes_.push_back(axis);
  axesIds_.push_back(axis->getAxisID)
  axesCounter_++;
};

// Check if all axes in group are enable
bool ecmcAxisGroup::getEnable(){
  for(std::vector<ecmcAxisBase*>::iterator axis = axes_.begin(); axis != axes_.end(); ++axis) {
    if((*axis)) {
      if (!(*axis)->getEnable()) {
        return 0;
      }
    }
  }
  return 1;
};

// Check if all axes in group are not enable
bool ecmcAxisGroup::getDisable(){
  for(std::vector<ecmcAxisBase*>::iterator axis = axes_.begin(); axis != axes_.end(); ++axis) {
    if((*axis)) {
      if ((*axis)->getEnable()) {
        return 0;
      }
    }
  }
  return 1;
};

// Check if at least one axis in group are enable
bool ecmcAxisGroup::getAnyEnable(){ 
  for(std::vector<ecmcAxisBase*>::iterator axis = axes_.begin(); axis != axes_.end(); ++axis) {
    if((*axis)) {
      if ((*axis)->getEnable()) {
        return 1;
      }
    }
  }
  return 0;
};

// Check if all axes in group are enabled
bool ecmcAxisGroup::getEnabled(){
  for(std::vector<ecmcAxisBase*>::iterator axis = axes_.begin(); axis != axes_.end(); ++axis) {
    if((*axis)) {
      if (!(*axis)->getEnabled()) {
        return 0;
      }
    }
  }
  return 1;
};

// Check if all axes in group are not not enabled
bool ecmcAxisGroup::getDisabled(){
  for(std::vector<ecmcAxisBase*>::iterator axis = axes_.begin(); axis != axes_.end(); ++axis) {
    if((*axis)) {
      if ((*axis)->getEnabled()) {
        return 0;
      }
    }
  }
  return 1;
};

// Check if at least one axis in group are enabled
bool ecmcAxisGroup::getAnyEnabled(){
  for(std::vector<ecmcAxisBase*>::iterator axis = axes_.begin(); axis != axes_.end(); ++axis) {
    if((*axis)) {
      if ((*axis)->getEnabled()) {
        return 1;
      }
    }
  }
  return 0;
};

// Check if all axes in group are not busy
bool ecmcAxisGroup::getFree(){
  for(std::vector<ecmcAxisBase*>::iterator axis = axes_.begin(); axis != axes_.end(); ++axis) {
    if((*axis)) {
      if ((*axis)->getBusy()) {
        return 0;
      }
    }
  }
  return 1;
};

// Check if all axes in group are busy
bool ecmcAxisGroup::getBusy(){
  for(std::vector<ecmcAxisBase*>::iterator axis = axes_.begin(); axis != axes_.end(); ++axis) {
    if((*axis)) {
      if (!(*axis)->getBusy()) {
        return 0;
      }
    }
  }
  return 1;
};

// Check if at least one axis in group are busy
bool ecmcAxisGroup::getAnyBusy(){
  for(std::vector<ecmcAxisBase*>::iterator axis = axes_.begin(); axis != axes_.end(); ++axis) {
    if((*axis)) {
      if ((*axis)->getBusy()) {
        return 1;
      }
    }
  }
  return 0;
};

// Check if at least one axis in group are in error state
int ecmcAxisGroup::getAnyErrorId(){
  int errorId = 0;
  for(std::vector<ecmcAxisBase*>::iterator axis = axes_.begin(); axis != axes_.end(); ++axis) {
    if((*axis)) {
      errorId = (*axis)->getErrorId();
      if (errorId) {
        return errorId;
      }
    }
  }
  return 0;
};

// Set enable of all axes in group
int ecmcAxisGroup::setEnable(bool enable){
  for(std::vector<ecmcAxisBase*>::iterator axis = axes_.begin(); axis != axes_.end(); ++axis) {
    if((*axis)) {
      (*axis)->setEnable(enable);
    }
  }
}

// set traj source of all axes in group
int ecmcAxisGroup::setTrajSrc(dataSource trajSource){
  for(std::vector<ecmcAxisBase*>::iterator axis = axes_.begin(); axis != axes_.end(); ++axis) {
    if((*axis)) {
      (*axis)->setTrajDataSourceType(trajSource);
    }
  }
}

// set enc source of all axes in group
int ecmcAxisGroup::setEncSrc(dataSource encSource){
  for(std::vector<ecmcAxisBase*>::iterator axis = axes_.begin(); axis != axes_.end(); ++axis) {
    if((*axis)) {
      (*axis)->setEncDataSourceType(encSource);
    }
  }
}

void ecmcAxisGroup::setErrorReset(){
  for(std::vector<ecmcAxisBase*>::iterator axis = axes_.begin(); axis != axes_.end(); ++axis) {
    if((*axis)) {
      (*axis)->resetError();
    }
  }
}

// Set errors all axes
void ecmcAxisGroup::setError(int error){
  for(std::vector<ecmcAxisBase*>::iterator axis = axes_.begin(); axis != axes_.end(); ++axis) {
    if((*axis)) {
      (*axis)->setErrorID(error);
    }
  }
}

// Set slaved axis error all axes
void ecmcAxisGroup::setSlaveAxisError(){
  for(std::vector<ecmcAxisBase*>::iterator axis = axes_.begin(); axis != axes_.end(); ++axis) {
    if((*axis)) {
      (*axis)->setSlavedAxisInError();
    }
  }
};

// Set slaved axis error all axes
void ecmcAxisGroup::stop(){
  for(std::vector<ecmcAxisBase*>::iterator axis = axes_.begin(); axis != axes_.end(); ++axis) {
    if((*axis)) {
      (*axis)->stopMotion();
    }
  }
};
