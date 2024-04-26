/*************************************************************************\
* Copyright (c) 2024 Paul Scherrer Institut
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcAxisPVTSequence.cpp
*
*  Created on: April 25, 2024
*      Author: anderssandstrom
*
\*************************************************************************/

#include "ecmcAxisPVTSequence.h"

ecmcAxisPVTSequence::ecmcAxisPVTSequence(double sampleTime) {
  segmentCount_ = 0;
  pointCount_   = 0;
  totalTime_    = 0;
  sampleTime_   = sampleTime;
  currTime_     = 0;
  busy_         = false;
  currSegIndex_ = 0;
  //built_        = false;
}

void ecmcAxisPVTSequence::addSegment(ecmcPvtPoint *start, ecmcPvtPoint *end ) {
  segments_.push_back(new ecmcPvtSegment(start, end));
  segmentCount_++;
}

void ecmcAxisPVTSequence::addPoint(ecmcPvtPoint *pnt) {
  points_.push_back(pnt);
  pointCount_++;
  if(pointCount_ > 1) {
     addSegment(points_[pointCount_-2], points_[pointCount_-1]);
  };
  //built_ = false;
}

double ecmcAxisPVTSequence::startTime(){
  if(segmentCount_ <= 0) {
      return -1;
  }
  return segments_[0]->getStartPoint()->time_;
}

double ecmcAxisPVTSequence::endTime(){
  if(segmentCount_ <= 0) {
      return -1;
  }
  return segments_[segmentCount_-1]->getEndPoint()->time_;
}

// Call before starting a seq
void ecmcAxisPVTSequence::initSeq() {
  busy_ = true;
  currTime_ = startTime();
  currSegIndex_ = 0;
}

// void build() {
//   for(size_t i = 0; i < segmentCount_; i++) {
//     segments_[i]->build();
//   };
//   currTime_ = startTime();
//   busy_ = false;
//   built_ = true;
// };

bool ecmcAxisPVTSequence::validate() {
  return /*built_ && */ segmentCount_ > 0;
}

ecmcPvtSegment* ecmcAxisPVTSequence::getSeqmentAtTime(double time) {
  if(segmentCount_ <= 0) {
    return NULL;
  };
  if(time < startTime() || time > endTime() ) {
    return NULL;
  };
  for(size_t i=0; i < segmentCount_; i++) {
    if(time >= segments_[i]->getStartPoint()->time_  && 
       time < segments_[i]->getEndPoint()->time_ ) {
      return segments_[i];
    }
  }

  // Check if last seq endtime (equals)
  if( time == segments_[segmentCount_-1]->getEndPoint()->time_) {
    return segments_[segmentCount_-1];
  }
  
  return NULL;
}

bool ecmcAxisPVTSequence::isLastSample() {
  return isLastSample(currTime_);
}

bool ecmcAxisPVTSequence::isLastSample(double time) {
  return time >= endTime();
}

bool ecmcAxisPVTSequence::isTimeValid(double time) {
  if(segmentCount_ <= 0) {
    return false;
  }

  if(time < startTime() || time > endTime() ) {
    return false;
  }
  return true;
}

// Go to next sample in time
// return true as long not exceeding endtime
bool ecmcAxisPVTSequence::nextSampleStep(){
  // Increase time
  currTime_ = currTime_ + sampleTime_;
  
  // Switch segment?
  if(currTime_ > segments_[currSegIndex_]->getEndPoint()->time_) {
    if(currSegIndex_ < segmentCount_) {
      // the time must be in the next segment
      currSegIndex_++;
    } else {  // last segment and last sample, set to curr time to end-time
      currTime_ = endTime();
    }
  }
  return currTime_ < endTime();
}

// For RT sequential access
double ecmcAxisPVTSequence::getCurrPosition(){
  return segments_[currSegIndex_]->position(currTime_);
}

// For RT sequential access
double ecmcAxisPVTSequence::getCurrVelocity(){
  return segments_[currSegIndex_]->velocity(currTime_);
}

// For RT sequential access
double ecmcAxisPVTSequence::getCurrAcceleration(){
  return segments_[currSegIndex_]->acceleration(currTime_);
}

double ecmcAxisPVTSequence::getCurrTime(){
  return currTime_;
}

// For non RT access
double ecmcAxisPVTSequence::position(double time, int *valid) {
  ecmcPvtSegment* temp = getSeqmentAtTime(time);
  if(!temp) {
      *valid = 0;
      return 0;
  }
  *valid = 1;
  return temp->position(time);
}

// For non RT access
double ecmcAxisPVTSequence::velocity(double time, int *valid) {
  ecmcPvtSegment* temp = getSeqmentAtTime(time);
  if(!temp) {
      *valid = 0;
      return 0;
  }
  *valid = 1;
  return temp->velocity(time);
}

// For non RT access
double ecmcAxisPVTSequence::acceleration(double time, int *valid) {
  ecmcPvtSegment* temp = getSeqmentAtTime(time);
  if(!temp) {
      *valid = 0;
      return 0;
  }
  *valid = 1;
  return temp->acceleration(time);
}
