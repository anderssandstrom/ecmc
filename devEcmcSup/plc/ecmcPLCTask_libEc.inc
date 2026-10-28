/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
*
*  ecmcPLCTask_libEc.inc
*
*  Created on: Nov 19, 2018
*      Author: anderssandstrom
*
\*************************************************************************/

#ifndef ecmcPLC_libEc_inc_
#define ecmcPLC_libEc_inc_

#include "ecmcSdoApi.h"
#include <cmath>
#include <limits>

#define ECMC_EC_LIB_MAX_BITS 64

#define ERROR_PLC_EC_LIB_BITS_OUT_OF_RANGE 0x20800
#define ERROR_PLC_EC_LIB_TIMING_UNRESOLVED 0x20801

extern ecmcEc *ec;

#define CHECK_PLC_BITS_AND_RETURN_VALUE_IF_ERROR(bits) { \
  ec_errorCode=0;                                        \
  if(!std::isfinite(bits) || bits>=ECMC_EC_LIB_MAX_BITS || bits<0 || std::trunc(bits)!=bits){              \
    ec_errorCode=ERROR_PLC_EC_LIB_BITS_OUT_OF_RANGE;     \
    ecmcRtLoggerLogError("%s/%s:%d: ERROR: Bit position out of range.\n", __FILE__, __FUNCTION__, __LINE__);       \
    return value;                                        \
  }                                                      \
}                                                        \

const char* ecLibCmdList[] = { "ec_set_bit(",
                               "ec_clr_bit(",
                               "ec_flp_bit(",
                               "ec_chk_bit(",
                               "ec_print_bin(",
                               "ec_print_hex(",
                               "ec_get_err(",
                               "ec_wrt_bit(",
                               "ec_mm_cp(",
                               "ec_err_rst(",
                               "ec_wrt_bits(",
                               "ec_chk_bits(",
                               "ec_get_time(",
                               "ec_get_last_receive_time(",
                               "ec_get_last_send_time(",
                               "ec_get_slave_input_event_time(",
                               "ec_get_mm_type(",
                               "ec_get_mm_data(",
                               "ec_set_mm_data(",
                               "ec_get_mm_size(",
                               "ec_get_time_l32(",
                               "ec_get_time_u32(",
                               "ec_mm_append_to_ds(",
                               "ec_mm_append_to_ds_scale_offset(",
                               "ec_mm_push_asyn(",
                               "ec_get_time_local_nsec(",
                               "ec_get_time_local_sec(",
                               "ec_get_time_local_min(",
                               "ec_get_time_local_hour(",
                               "ec_get_dom_state(",
                               "ec_get_time_frm_src(",
                               "ec_get_time_offset_mono(",
                               "ec_sdo_read(",
                               "ec_sdo_write(",
                               "ec_sdo_get_busy(",
                               "ec_sdo_get_error(",
                               "ec_sdo_get_size(",
                               "ec_sdo_get_capacity(",
                               "ec_sdo_get_value(",
                               "ec_sdo_set_value(",

                              };

static int ec_errorCode = 0;
static const int ec_cmd_count = sizeof(ecLibCmdList) / sizeof(ecLibCmdList[0]);


// PLC indices are doubles: validate before narrowing to C API integer indices.
inline bool ec_sdo_valid_index(double value) {
  return std::isfinite(value) && value >= 0 &&
         value <= std::numeric_limits<int>::max() && std::trunc(value) == value;
}
inline bool ec_sdo_valid_ref(double slave, double index) {
  ec_errorCode = 0;
  if (!ec_sdo_valid_index(slave) || slave > 65535 || !ec_sdo_valid_index(index)) {
    ec_errorCode = ERROR_EC_SDO_ASYNC_VALUE;
    return false;
  }
  return true;
}

inline double ec_sdo_read(double slave, double index) {
  if (!ec_sdo_valid_ref(slave, index)) return ec_errorCode;
  ec_errorCode = ecSdoRead(static_cast<int>(slave), static_cast<int>(index));
  return ec_errorCode;
}

inline double ec_sdo_write(double slave, double index) {
  if (!ec_sdo_valid_ref(slave, index)) return ec_errorCode;
  ec_errorCode = ecSdoWrite(static_cast<int>(slave), static_cast<int>(index));
  return ec_errorCode;
}

inline double ec_sdo_get_busy(double slave, double index) {
  if (!ec_sdo_valid_ref(slave, index)) return -ec_errorCode;
  int value = 0;
  ec_errorCode = ecSdoGetBusy(static_cast<int>(slave), static_cast<int>(index), &value);
  return ec_errorCode ? -ec_errorCode : value;
}

inline double ec_sdo_get_error(double slave, double index) {
  if (!ec_sdo_valid_ref(slave, index)) return -ec_errorCode;
  int value = 0;
  ec_errorCode = ecSdoGetError(static_cast<int>(slave), static_cast<int>(index), &value);
  return ec_errorCode ? -ec_errorCode : value;
}

inline double ec_sdo_get_size(double slave, double index) {
  if (!ec_sdo_valid_ref(slave, index)) return -ec_errorCode;
  int value = 0;
  ec_errorCode = ecSdoGetSize(static_cast<int>(slave), static_cast<int>(index), &value);
  return ec_errorCode ? -ec_errorCode : value;
}

inline double ec_sdo_get_capacity(double slave, double index) {
  if (!ec_sdo_valid_ref(slave, index)) return -ec_errorCode;
  int value = 0;
  ec_errorCode = ecSdoGetCapacity(static_cast<int>(slave), static_cast<int>(index), &value);
  return ec_errorCode ? -ec_errorCode : value;
}

inline double ec_sdo_get_value(double slave, double index, double element) {
  if (!ec_sdo_valid_ref(slave, index)) return 0;
  if (!ec_sdo_valid_index(element)) { ec_errorCode = ERROR_EC_SDO_ASYNC_SIZE; return 0; }
  double value = 0;
  ec_errorCode = ecSdoGetValue(static_cast<int>(slave), static_cast<int>(index),
                              static_cast<int>(element), &value);
  return ec_errorCode ? 0 : value;
}
inline double ec_sdo_set_value(double slave, double index, double element, double value) {
  if (!ec_sdo_valid_ref(slave, index)) return ec_errorCode;
  if (!ec_sdo_valid_index(element)) return ec_errorCode = ERROR_EC_SDO_ASYNC_SIZE;
  ec_errorCode = ecSdoSetValue(static_cast<int>(slave), static_cast<int>(index),
                              static_cast<int>(element), value);
  return ec_errorCode;
}

inline double ec_set_bit(double value, double bitIndex)
{
  CHECK_PLC_BITS_AND_RETURN_VALUE_IF_ERROR(bitIndex);
  uint64_t temp=(uint64_t)value;  
  return (double)(temp | (uint64_t(1) << (int)bitIndex));
}

inline double ec_wrt_bit(double value, double wrtValue,double bitIndex)
{
  CHECK_PLC_BITS_AND_RETURN_VALUE_IF_ERROR(bitIndex);
  uint64_t temp=(uint64_t)value;
  if(wrtValue>0) {
    return (double)(temp | (uint64_t(1) << (int)bitIndex));
  }
  else {
    return (double)(temp & ~(uint64_t(1) << (int)bitIndex));
  }
}

inline double ec_wrt_bits(double value,
                          double wrtValue,
                          double startBit,
                          double stopBit)
{  
  CHECK_PLC_BITS_AND_RETURN_VALUE_IF_ERROR(startBit);
  CHECK_PLC_BITS_AND_RETURN_VALUE_IF_ERROR(stopBit);
  if (stopBit < startBit) {
    ec_errorCode = ERROR_PLC_EC_LIB_BITS_OUT_OF_RANGE;
    return value;
  }
  uint64_t temp=(uint64_t)value;
  uint64_t data=(uint64_t)wrtValue;
  uint64_t mask=-1;
  if((stopBit-startBit+1)<64) {
    mask=((uint64_t(1) << ((int)(stopBit-startBit+1)))-1);
  }
  
  return (temp & ~(mask << (int)startBit)) | ((data & mask) << (int)startBit);
}

inline double ec_chk_bits(double value, double startBit, double stopBit)
{  
  CHECK_PLC_BITS_AND_RETURN_VALUE_IF_ERROR(startBit);
  CHECK_PLC_BITS_AND_RETURN_VALUE_IF_ERROR(stopBit);
  if (stopBit < startBit) {
    ec_errorCode = ERROR_PLC_EC_LIB_BITS_OUT_OF_RANGE;
    return value;
  }
  
  uint64_t temp=(uint64_t)value;
  uint64_t mask=~uint64_t(0) << (int)startBit;
  if((stopBit-startBit+1)<64) {
    mask=((uint64_t(1) << ((int)(stopBit-startBit+1)))-1)<<(int)startBit;
  }   
  
  return (double)((temp & mask) >> (int)startBit);
}

inline double ec_get_time() {
  ec_errorCode = 0;
  if(!ec) {
    ec_errorCode = ERROR_MAIN_EC_MASTER_NULL;
    return -(double)ec_errorCode;
  }

  return (double)ec->getTimeNs();
}

/* Cached time of the receive phase in the current application cycle.
 * Unlike ec_get_time(), this does not read a clock when the PLC calls it. */
inline double ec_get_last_receive_time() {
  ec_errorCode = 0;
  if(!ec) {
    ec_errorCode = ERROR_MAIN_EC_MASTER_NULL;
    return -(double)ec_errorCode;
  }

  return (double)ec->getLastReceiveTimeNs();
}

/* Cached time of the most recently completed send phase.  During PLC
 * execution this is normally the send phase from the preceding cycle. */
inline double ec_get_last_send_time() {
  ec_errorCode = 0;
  if(!ec) {
    ec_errorCode = ERROR_MAIN_EC_MASTER_NULL;
    return -(double)ec_errorCode;
  }

  return (double)ec->getLastSendTimeNs();
}

inline double ec_get_slave_input_event_time(double slavePosition) {
  ec_errorCode = 0;
  if(!ec) {
    ec_errorCode = ERROR_MAIN_EC_MASTER_NULL;
    return -(double)ec_errorCode;
  }
  ecmcEcSlave *slave = ec->findSlave((int)slavePosition);
  if(!slave) {
    ec_errorCode = ERROR_MAIN_EC_SLAVE_NULL;
    return -(double)ec_errorCode;
  }
  const ecmcEcCycleTiming& cycle = ec->getCycleTiming();
  const ecmcEcEndpointTiming& timing = slave->getInputTiming();
  uint64_t eventTimeNs = 0;
  bool valid = false;
  if(timing.source == ecmcEcTimingSource::SYNC0_DERIVED ||
     timing.source == ecmcEcTimingSource::SYNC1_DERIVED) {
    // Resolve the effective input event in the EtherCAT/DC application-time
    // domain. Slave-specific PDO generation age remains represented by
    // timing.cycleOffset; host receive/frame timing must not be baked into the
    // slave sample timestamp.
    valid = ecmcEcResolveScheduledEventNs(cycle.applicationTimeNs, timing,
      slave->getDcConfig(), slave->getDcSchedule(), cycle.nominalPeriodNs,
      &eventTimeNs);
  } else if(timing.source == ecmcEcTimingSource::CYCLE_ONLY) {
    valid = ecmcEcResolveCycleEventNs(cycle, timing, &eventTimeNs);
  }
  if(!valid) {
    ec_errorCode = ERROR_PLC_EC_LIB_TIMING_UNRESOLVED;
    return -(double)ec_errorCode;
  }
  return (double)eventTimeNs;
}

inline double ec_get_time_l32() {
  ec_errorCode = 0;
  if(!ec) {
    ec_errorCode = ERROR_MAIN_EC_MASTER_NULL;
    return -(double)ec_errorCode;
  }  
  return (double)((uint32_t)ec->getTimeNs());
}

inline double ec_get_time_u32() {
  ec_errorCode = 0;
  if(!ec) {
    ec_errorCode = ERROR_MAIN_EC_MASTER_NULL;
    return -(double)ec_errorCode;
  }  
  return (double)((uint32_t) (ec->getTimeNs() >> 32));
}

inline double ec_get_time_frm_src(double clock_id) {
  ec_errorCode = 0;
  if(!ec) {
    ec_errorCode = ERROR_MAIN_EC_MASTER_NULL;  
    return -(double)ec_errorCode;
  }

  return (double)ec->getTimeNs((int)clock_id);
}

inline double ec_get_time_offset_mono() {
  ec_errorCode = 0;
  if(!ec) {
    ec_errorCode = ERROR_MAIN_EC_MASTER_NULL;  
    return -(double)ec_errorCode;
  }

  return (double)ec->getTimeOffsetNs();
}

inline double ec_get_time_local_nsec(double nanos) {
  ec_errorCode = 0;
  uint64_t nanoseconds=(uint64_t) nanos;
  uint64_t seconds=nanoseconds/1000000000ULL;
  return (double)(nanos-seconds*1000000000ULL);
}

inline double ec_get_time_local_sec(double nanos) {
  ec_errorCode = 0;
  uint64_t nanoseconds=(uint64_t) nanos;
  timespec ts;
  struct tm buf;
  NS2TIMESPEC(nanoseconds,ts)
  localtime_r(&ts.tv_sec,&buf);
  return (double) buf.tm_sec;
}

inline double ec_get_time_local_min(double nanos) {
  ec_errorCode = 0;
  uint64_t nanoseconds=(uint64_t) nanos;
  timespec ts;
  struct tm buf;
  NS2TIMESPEC(nanoseconds,ts)
  localtime_r(&ts.tv_sec,&buf);
  return (double) buf.tm_min;
}

inline double ec_get_time_local_hour(double nanos) {
  ec_errorCode = 0;
  uint64_t nanoseconds=(uint64_t) nanos;
  timespec ts;
  struct tm buf;
  NS2TIMESPEC(nanoseconds,ts)
  localtime_r(&ts.tv_sec,&buf);
  return (double) buf.tm_hour;
}

inline double ec_clr_bit(double value, double bitIndex)
{
  CHECK_PLC_BITS_AND_RETURN_VALUE_IF_ERROR(bitIndex);
  uint64_t temp=(uint64_t)value;  
  return (double)(temp & ~(uint64_t(1) << (int)bitIndex));
}

inline double ec_flp_bit(double value, double bitIndex)
{
  CHECK_PLC_BITS_AND_RETURN_VALUE_IF_ERROR(bitIndex);
  uint64_t temp=(uint64_t)value;  
  return (double)(temp ^ (uint64_t(1) << (int)bitIndex));
}

inline double ec_chk_bit(double value, double bitIndex)
{
  CHECK_PLC_BITS_AND_RETURN_VALUE_IF_ERROR(bitIndex);
  uint64_t temp=(uint64_t)value;  
  return (double)(temp & (uint64_t(1) << (int)bitIndex))>0;
}

inline double ec_print_hex(double value, double startBit,double stopBit)
{
  CHECK_PLC_BITS_AND_RETURN_VALUE_IF_ERROR(startBit);
  CHECK_PLC_BITS_AND_RETURN_VALUE_IF_ERROR(stopBit);
  if (stopBit < startBit) {
    ec_errorCode = ERROR_PLC_EC_LIB_BITS_OUT_OF_RANGE;
    return value;
  }
  
  uint64_t temp=(uint64_t)value;
  uint64_t mask=~uint64_t(0) << (int)startBit;
  if((stopBit-startBit+1)<64) {
    mask=((uint64_t(1) << ((int)(stopBit-startBit+1)))-1)<<(int)startBit;
  }   
  printf("%" PRIx64 "",(temp & mask) >> (int)startBit);  
  return 0;
}

inline double ec_print_bin(double value, double startBit,double stopBit)
{
  CHECK_PLC_BITS_AND_RETURN_VALUE_IF_ERROR(startBit);
  CHECK_PLC_BITS_AND_RETURN_VALUE_IF_ERROR(stopBit);
  if (stopBit < startBit) {
    ec_errorCode = ERROR_PLC_EC_LIB_BITS_OUT_OF_RANGE;
    return value;
  }
  
  uint64_t temp=(uint64_t)value;
  for(int i=stopBit;i>=startBit;i--){
    printf("%u",(temp & (uint64_t(1) << i)) ? 1:0);
  }
  
  return 0;
}

inline double ec_get_err()
{  
  return (double)ec_errorCode;
}

// Mem map copy
inline double ec_mm_cp(double srcId, double destId) {
  ec_errorCode = 0;
  if(!ec) {
    ec_errorCode = ERROR_MAIN_EC_MASTER_NULL;
    return (double)ec_errorCode;
  }
  
  ecmcEcMemMap *src = ec->getMemMap((int)srcId);
  ecmcEcMemMap *dest = ec->getMemMap((int)destId);
  if(!src || !dest) {
    ec_errorCode = ERROR_MAIN_MEM_MAP_NULL;
    return (double)ec_errorCode;
  }

  //Size to copy
  int sizeToCp = src->getByteSize();
  int sizeDest = dest->getByteSize();
  if(sizeDest<sizeToCp) {
    sizeToCp = sizeDest;
  }

  //Write data from src to dest 
  uint8_t * const destBuffer = dest->getBufferPointer();
  uint8_t * const srcBuffer  = src->getBufferPointer();
  memcpy(destBuffer, srcBuffer, sizeToCp);
  return ec_errorCode;
}

// Get mm data type
inline double ec_get_mm_type(double srcId) {
  ec_errorCode = 0;
  if(!ec) {
    ec_errorCode = ERROR_MAIN_EC_MASTER_NULL;
    return -(double)ec_errorCode;
  }
  
  ecmcEcMemMap *src = ec->getMemMap((int)srcId);
  
  if(!src) {
    ec_errorCode = ERROR_MAIN_MEM_MAP_NULL;
    return -(double)ec_errorCode;
  }
  
  return (double)src->getDataType();
}

// Get mm data
inline double ec_get_mm_data(double srcId,double index) {
  ec_errorCode = 0;
  if(!ec) {
    ec_errorCode = ERROR_MAIN_EC_MASTER_NULL;
    return 0;
  }
  
  ecmcEcMemMap *src = ec->getMemMap((int)srcId);
  
  if(!src) {
    ec_errorCode = ERROR_MAIN_MEM_MAP_NULL;
    return 0;
  }

  double dataTemp = 0;

  ec_errorCode = src->getDoubleDataAtIndex((int)index,&dataTemp);
  if(ec_errorCode) {
    return 0;
  }

  return dataTemp;
}

// Set mm data type
inline double ec_set_mm_data(double srcId,double index, double data) {
  ec_errorCode = 0;
  if(!ec) {
    ec_errorCode = ERROR_MAIN_EC_MASTER_NULL;
    return 0;
  }
  
  ecmcEcMemMap *src = ec->getMemMap((int)srcId);
  
  if(!src) {
    ec_errorCode = ERROR_MAIN_MEM_MAP_NULL;
    return 0;
  }

  ec_errorCode = src->setDoubleDataAtIndex((int)index,data);

  return ec_errorCode;
}

// Get mm data size
inline double ec_get_mm_size(double srcId) {
  ec_errorCode = 0;
  if(!ec) {
    ec_errorCode = ERROR_MAIN_EC_MASTER_NULL;
    return 0;
  }
  
  ecmcEcMemMap *src = ec->getMemMap((int)srcId);
  
  if(!src) {
    ec_errorCode = ERROR_MAIN_MEM_MAP_NULL;
    return 0;
  }

  return (double)src->getElementCount();
}

// Append mm data to ds
inline double ec_mm_append_to_ds(double mmId,double dsId) {
  ec_errorCode = 0;
  if(!ec) {
    ec_errorCode = ERROR_MAIN_EC_MASTER_NULL;
    return 0;
  }
  
  ecmcEcMemMap *src = ec->getMemMap((int)mmId);
  
  if(!src) {
    ec_errorCode = ERROR_MAIN_MEM_MAP_NULL;
    return (double)ec_errorCode;
  }
  
  int index = (int)dsId;
  CHECK_PLC_DATA_STORAGE_RETURN_IF_ERROR(index);
  auto * const ds = dataStorages[index];
  
  const int mmSize = src->getElementCount();
  double dataTemp = 0;
  //TODO: This is slow... Better solution needed. Better to make a plugin
  for(int i=0;i<mmSize;i++) {

    ec_errorCode = src->getDoubleDataAtIndex((int)i,&dataTemp);
    if(ec_errorCode) {
      return (double)ec_errorCode;
    }
    ec_errorCode = ds->appendData(dataTemp, i==mmSize-1);

  }

  return (double)ec_errorCode;
}

// Append mm data to ds and scale and offset
inline double ec_mm_append_to_ds_scale_offset(double mmId,double dsId,double dScale,double dOffset) {
  ec_errorCode = 0;
  if(!ec) {
    ec_errorCode = ERROR_MAIN_EC_MASTER_NULL;
    return 0;
  }
  
  ecmcEcMemMap *src = ec->getMemMap((int)mmId);
  
  if(!src) {
    ec_errorCode = ERROR_MAIN_MEM_MAP_NULL;
    return (double)ec_errorCode;
  }
  
  int index = (int)dsId;
  CHECK_PLC_DATA_STORAGE_RETURN_IF_ERROR(index);
  auto * const ds = dataStorages[index];
  
  const int mmSize = src->getElementCount();
  double dataTemp = 0;
  //TODO: This is slow... Better solution needed. Better to make a plugin
  for(int i=0;i<mmSize;i++) {

    ec_errorCode = src->getDoubleDataAtIndex((int)i,&dataTemp);
    if(ec_errorCode) {
      return (double)ec_errorCode;
    }
    // update asyn only on last element
    ec_errorCode = ds->appendData(dataTemp * dScale + dOffset, i==mmSize-1);
  }

  return (double)ec_errorCode;
}

inline double ec_mm_push_asyn(double mmIndex) {
  int index = (int)mmIndex;

  ecmcEcMemMap *mm = ec->getMemMap(index);
  
  if(!mm) {
    ec_errorCode = ERROR_MAIN_MEM_MAP_NULL;
    return (double)ec_errorCode;
  }
  
  return (double)mm->updateAsyn(1);
}

// Reset error
inline double ec_err_rst() {
  ec_errorCode = 0;
  return 0;
}

// Get domain state (if negative then error code)
inline double ec_get_dom_state(double domainId) {
  ec_errorCode = 0;
  if(!ec) {
    ec_errorCode = ERROR_MAIN_EC_MASTER_NULL;
    return -(double)ec_errorCode;
  }
  
  return (double)ec->getDomState((int)domainId);
}

#endif /* ecmcPLC_libEc_inc_ */
