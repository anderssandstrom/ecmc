/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
*
*  ecmcPLCTask_libDs.inc
*
*  Created on: Nov 19, 2018
*      Author: anderssandstrom
*
\*************************************************************************/

#ifndef ecmcPLC_libDs_inc_
#define ecmcPLC_libDs_inc_

#include <cmath>
#include <limits>

#define CHECK_PLC_DATA_STORAGE_RETURN_IF_ERROR(dsIndex) {           \
    ds_errorCode = 0;                                               \
    if (dsIndex >= ECMC_MAX_DATA_STORAGE_OBJECTS || dsIndex < 0) {  \
      ds_errorCode = ERROR_PLC_DATA_STORAGE_INDEX_OUT_OF_RANGE;     \
      ecmcRtLoggerLogError("%s/%s:%d: ERROR: Data storage index out of range.\n", __FILE__, __FUNCTION__, __LINE__);          \
      return (double)ERROR_PLC_DATA_STORAGE_INDEX_OUT_OF_RANGE;     \
    }                                                               \
    if (dataStorages[dsIndex] == NULL) {                    \
      ds_errorCode = ERROR_PLC_DATA_STORAGE_OBJECT_NULL;            \
      ecmcRtLoggerLogError("%s/%s:%d: ERROR: Data storage object is NULL.\n", __FILE__, __FUNCTION__, __LINE__);                  \
      return (double)ERROR_PLC_DATA_STORAGE_OBJECT_NULL;            \
    }                                                               \
}                                                                   \

#define CHECK_PLC_DATA_STORAGE_RETURN_ZERO_IF_ERROR(dsIndex) {      \
    ds_errorCode = 0;                                               \
    if (dsIndex >= ECMC_MAX_DATA_STORAGE_OBJECTS || dsIndex < 0) {  \
      ds_errorCode = ERROR_PLC_DATA_STORAGE_INDEX_OUT_OF_RANGE;     \
      ecmcRtLoggerLogError("%s/%s:%d: ERROR: Data storage index out of range.\n", __FILE__, __FUNCTION__, __LINE__);          \
      return 0;                                                     \
    }                                                               \
    if (dataStorages[dsIndex] == NULL) {                    \
      ds_errorCode = ERROR_PLC_DATA_STORAGE_OBJECT_NULL;            \
      ecmcRtLoggerLogError("%s/%s:%d: ERROR: Data storage object is NULL.\n", __FILE__, __FUNCTION__, __LINE__);                  \
      return 0;                                                     \
    }                                                               \
}                                                                   \

#define CHECK_PLC_DATA_STORAGE_RETURN_MINUS_ONE_IF_ERROR(dsIndex) { \
    ds_errorCode = 0;                                               \
    if (dsIndex >= ECMC_MAX_DATA_STORAGE_OBJECTS || dsIndex < 0) {  \
      ds_errorCode = ERROR_PLC_DATA_STORAGE_INDEX_OUT_OF_RANGE;     \
      ecmcRtLoggerLogError("%s/%s:%d: ERROR: Data storage index out of range.\n", __FILE__, __FUNCTION__, __LINE__);          \
      return -1;                                                    \
    }                                                               \
    if (dataStorages[dsIndex] == NULL) {                    \
      ds_errorCode = ERROR_PLC_DATA_STORAGE_OBJECT_NULL;            \
      ecmcRtLoggerLogError("%s/%s:%d: ERROR: Data storage object is NULL.\n", __FILE__, __FUNCTION__, __LINE__);                  \
      return -1;                                                    \
    }                                                               \
}                                                                   \

const char *dsLibCmdList[] = { "ds_append_data(",
                               "ds_clear_data(",
                               "ds_get_data(",
                               "ds_set_data(",
                               "ds_get_buff_id(",
                               "ds_set_buff_id(",
                               "ds_get_err(",
                               "ds_is_full(",
                               "ds_get_size(",
                               "ds_push_asyn(",
                               "ds_get_avg(",
                               "ds_get_min(",
                               "ds_get_max(",
                               "ds_err_rst(",
                               "ds_append_to_ds("
};

extern ecmcDataStorage  *dataStorages[ECMC_MAX_DATA_STORAGE_OBJECTS];
static int ds_errorCode = 0;
static int ds_cmd_count = 15;

// Validate PLC doubles before narrowing to indices; invalid object indices use
// the existing out-of-range return/error conventions of each wrapper.
inline int ds_object_index(double value) {
  if (!std::isfinite(value) || value < 0 || value >= ECMC_MAX_DATA_STORAGE_OBJECTS ||
      std::trunc(value) != value) return ECMC_MAX_DATA_STORAGE_OBJECTS;
  return static_cast<int>(value);
}
inline int ds_element_index(double value) {
  if (!std::isfinite(value) || value < 0 ||
      value > std::numeric_limits<int>::max() || std::trunc(value) != value) return -1;
  return static_cast<int>(value);
}

inline double ds_append_data(double dsIndex, double data) {
  int index = ds_object_index(dsIndex);

  CHECK_PLC_DATA_STORAGE_RETURN_IF_ERROR(index);
  ds_errorCode = dataStorages[index]->appendData(data);
  return (double)ds_errorCode;
}

inline double ds_clear_data(double dsIndex) {
  int index = ds_object_index(dsIndex);

  CHECK_PLC_DATA_STORAGE_RETURN_IF_ERROR(index);
  ds_errorCode = dataStorages[index]->clearBuffer();
  return (double)ds_errorCode;
}

inline double ds_get_data(double dsIndex, double dataIndex) {
  int index = ds_object_index(dsIndex);

  CHECK_PLC_DATA_STORAGE_RETURN_ZERO_IF_ERROR(index);
  double temp = 0;
  ds_errorCode =
    dataStorages[index]->getDataElement(ds_element_index(dataIndex), &temp);
  return temp;
}

inline double ds_set_data(double dsIndex, double dataIndex, double data) {
  int index = ds_object_index(dsIndex);

  CHECK_PLC_DATA_STORAGE_RETURN_IF_ERROR(index);
  ds_errorCode = dataStorages[index]->setDataElement(ds_element_index(dataIndex), data);
  return (double)ds_errorCode;
}

inline double ds_get_buff_id(double dsIndex) {
  int index = ds_object_index(dsIndex);

  CHECK_PLC_DATA_STORAGE_RETURN_MINUS_ONE_IF_ERROR(index);
  return dataStorages[index]->getCurrentIndex();
}

inline double ds_set_buff_id(double dsIndex, double bufferIndex) {
  int index = ds_object_index(dsIndex);

  CHECK_PLC_DATA_STORAGE_RETURN_IF_ERROR(index);
  ds_errorCode = dataStorages[index]->setCurrentPosition(ds_element_index(bufferIndex));
  return (double)ds_errorCode;
}

inline double ds_get_err() {
  return (double)ds_errorCode;
}

inline double ds_is_full(double dsIndex) {
  int index = ds_object_index(dsIndex);

  CHECK_PLC_DATA_STORAGE_RETURN_MINUS_ONE_IF_ERROR(index);
  ds_errorCode = 0;
  return (double)dataStorages[index]->isStorageFull();
}

inline double ds_get_size(double dsIndex) {
  int index = ds_object_index(dsIndex);

  CHECK_PLC_DATA_STORAGE_RETURN_MINUS_ONE_IF_ERROR(index);
  ds_errorCode = 0;
  return (double)dataStorages[index]->getSize();
}

inline double ds_push_asyn(double dsIndex) {
  int index = ds_object_index(dsIndex);

  CHECK_PLC_DATA_STORAGE_RETURN_MINUS_ONE_IF_ERROR(index);
  ds_errorCode = 0;  
  return (double)dataStorages[index]->updateAsyn(1);
}

inline double ds_get_avg(double dsIndex) {
  int index = ds_object_index(dsIndex);

  CHECK_PLC_DATA_STORAGE_RETURN_ZERO_IF_ERROR(index);
  ds_errorCode = 0;
  return (double)dataStorages[index]->getAvg();
}

inline double ds_get_min(double dsIndex) {
  int index = ds_object_index(dsIndex);

  CHECK_PLC_DATA_STORAGE_RETURN_ZERO_IF_ERROR(index);
  ds_errorCode = 0;
  return (double)dataStorages[index]->getMin();
}

inline double ds_get_max(double dsIndex) {
  int index = ds_object_index(dsIndex);

  CHECK_PLC_DATA_STORAGE_RETURN_ZERO_IF_ERROR(index);
  ds_errorCode = 0;
  return (double)dataStorages[index]->getMax();
}

inline double ds_append_to_ds(double dsFromId, double dsFromDataIndex, double elements, double dsToId) {
  int fromId = ds_object_index(dsFromId);
  int toId = ds_object_index(dsToId);
  ds_errorCode = 0;

  CHECK_PLC_DATA_STORAGE_RETURN_ZERO_IF_ERROR(fromId);
  CHECK_PLC_DATA_STORAGE_RETURN_ZERO_IF_ERROR(toId);
  
  if (ds_element_index(dsFromDataIndex) < 0) {
    return ds_errorCode = ERROR_DATA_STORAGE_POSITION_OUT_OF_RANGE;
  }
  if (ds_element_index(elements) < 0) {
    return ds_errorCode = ERROR_MAIN_DATA_STORAGE_INVALID_SIZE;
  }
  int srcSize = dataStorages[fromId]->getSize();
  if(dsFromDataIndex>=srcSize) {
    ds_errorCode = ERROR_DATA_STORAGE_POSITION_OUT_OF_RANGE;;
    return (double) ds_errorCode;    
  }

  int elementsLocal=(int)elements;
  if(dsFromDataIndex+elements>=srcSize) {
    elementsLocal=srcSize-(int)dsFromDataIndex;
  }

  double *srcPtr = NULL;
  ds_errorCode = dataStorages[fromId]->getDataElementPtr((int)dsFromDataIndex, &srcPtr);
  if(ds_errorCode) {
    return ds_errorCode;
  }
  
  ds_errorCode = dataStorages[toId]->appendData(srcPtr,elementsLocal);
  return ds_errorCode;
}


inline double ds_err_rst() {
  
  ds_errorCode = 0;
  return (double) 0;
}

#endif  /* ecmcPLC_libDs_inc_ */
