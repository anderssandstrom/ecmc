#ifndef ECMC_AXIS_DIAGNOSTIC_TEXT_H
#define ECMC_AXIS_DIAGNOSTIC_TEXT_H
#include "ecmcDefinitions.h"
inline const char *ecmcInterlockToString(int interlock) {
  switch (interlock) {
  case ECMC_INTERLOCK_NONE:
    return "";
  case ECMC_INTERLOCK_SOFT_BWD:
    return "Soft limit backward";
  case ECMC_INTERLOCK_SOFT_FWD:
    return "Soft limit forward";
  case ECMC_INTERLOCK_HARD_BWD:
    return "Hard limit backward";
  case ECMC_INTERLOCK_HARD_FWD:
    return "Hard limit forward";
  case ECMC_INTERLOCK_NO_EXECUTE:
    return "No execute";
  case ECMC_INTERLOCK_POSITION_LAG:
    return "Position lag";
  case ECMC_INTERLOCK_BOTH_LIMITS:
    return "Both limits";
  case ECMC_INTERLOCK_EXTERNAL:
    return "External interlock";
  case ECMC_INTERLOCK_TRANSFORM:
    return "Transform interlock";
  case ECMC_INTERLOCK_MAX_SPEED:
    return "Max speed";
  case ECMC_INTERLOCK_CONT_HIGH_LIMIT:
    return "Controller high limit";
  case ECMC_INTERLOCK_CONT_OUT_INCREASE_AT_LIMIT_SWITCH:
    return "Control output increase at limit";
  case ECMC_INTERLOCK_AXIS_ERROR_STATE:
    return "Axis error state";
  case ECMC_INTERLOCK_UNEXPECTED_LIMIT_SWITCH_BEHAVIOUR:
    return "Unexpected limit switch behaviour";
  case ECMC_INTERLOCK_VELOCITY_DIFF:
    return "Velocity difference";
  case ECMC_INTERLOCK_ETHERCAT_MASTER_NOT_OK:
    return "EtherCAT master not OK";
  case ECMC_INTERLOCK_PLC_NORMAL:
    return "PLC interlock";
  case ECMC_INTERLOCK_PLC_BWD:
    return "PLC interlock backward";
  case ECMC_INTERLOCK_PLC_FWD:
    return "PLC interlock forward";
  case ECMC_INTERLOCK_ENC_DIFF:
    return "Encoder difference";
  case ECMC_INTERLOCK_ANALOG:
    return "Analog interlock";
  case ECMC_INTERLOCK_SAFETY:
    return "Safety interlock";
  case ECMC_INTERLOCK_STALL:
    return "Stall";
  default:
    return "Unknown interlock";
  }
}

inline const char *ecmcInterlockToShortString(int interlock) {
  switch (interlock) {
  case ECMC_INTERLOCK_NONE:
    return "";
  case ECMC_INTERLOCK_SOFT_BWD:
    return "SftBwd";
  case ECMC_INTERLOCK_SOFT_FWD:
    return "SftFwd";
  case ECMC_INTERLOCK_HARD_BWD:
    return "HrdBwd";
  case ECMC_INTERLOCK_HARD_FWD:
    return "HrdFwd";
  case ECMC_INTERLOCK_NO_EXECUTE:
    return "NoExec";
  case ECMC_INTERLOCK_POSITION_LAG:
    return "PosLag";
  case ECMC_INTERLOCK_BOTH_LIMITS:
    return "BothLim";
  case ECMC_INTERLOCK_EXTERNAL:
    return "ExtIL";
  case ECMC_INTERLOCK_TRANSFORM:
    return "TrnsIL";
  case ECMC_INTERLOCK_MAX_SPEED:
    return "MaxSpd";
  case ECMC_INTERLOCK_CONT_HIGH_LIMIT:
    return "CntHi";
  case ECMC_INTERLOCK_CONT_OUT_INCREASE_AT_LIMIT_SWITCH:
    return "CntInc";
  case ECMC_INTERLOCK_AXIS_ERROR_STATE:
    return "AxErr";
  case ECMC_INTERLOCK_UNEXPECTED_LIMIT_SWITCH_BEHAVIOUR:
    return "LimSw";
  case ECMC_INTERLOCK_VELOCITY_DIFF:
    return "VelDif";
  case ECMC_INTERLOCK_ETHERCAT_MASTER_NOT_OK:
    return "ECat";
  case ECMC_INTERLOCK_PLC_NORMAL:
    return "PLC";
  case ECMC_INTERLOCK_PLC_BWD:
    return "PLCBwd";
  case ECMC_INTERLOCK_PLC_FWD:
    return "PLCFwd";
  case ECMC_INTERLOCK_ENC_DIFF:
    return "EncDif";
  case ECMC_INTERLOCK_ANALOG:
    return "Analog";
  case ECMC_INTERLOCK_SAFETY:
    return "Safety";
  case ECMC_INTERLOCK_STALL:
    return "Stall";
  default:
    return "Unkn";
  }
}
#endif
