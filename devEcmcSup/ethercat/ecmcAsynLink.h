/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcAsynLink.h
*
*  Created on: Dec 2, 2015
*      Author: anderssandstrom
*
\*************************************************************************/

#ifndef ECMCASYNLINK_H_
#define ECMCASYNLINK_H_

#ifdef __cplusplus
/**
 * @file ecmcAsynLink.h
 * @brief Abstract interface for connecting EtherCAT data to asynPortDriver.
 */
#endif

#include "asynPortDriver.h"

/**
 * @brief Bridge that exposes EtherCAT data through asynPortDriver APIs.
 */
class ecmcAsynLink {
public:
  ecmcAsynLink();
  virtual ~ecmcAsynLink();
  /** @brief Read signed 32-bit value. */
  virtual int readInt32(epicsInt32 *value) = 0;
  /** @brief Write signed 32-bit value. */
  virtual int writeInt32(epicsInt32 value) = 0;
  /** @brief Read digital 32-bit value with mask. */
  virtual int readUInt32Digital(epicsUInt32 *value,
                                epicsUInt32  mask) = 0;
  /** @brief Write digital 32-bit value with mask. */
  virtual int writeUInt32Digital(epicsUInt32 value,
                                 epicsUInt32 mask) = 0;
  /** @brief Read double value. */
  virtual int readFloat64(epicsFloat64 *value)     = 0;
  /** @brief Write double value. */
  virtual int writeFloat64(epicsFloat64 value)     = 0;
  /** @brief Read int8 array. */
  virtual int readInt8Array(epicsInt8 *value,
                            size_t     nElements,
                            size_t    *nIn) = 0;
  /** @brief Write int8 array. */
  virtual int writeInt8Array(epicsInt8 *value,
                             size_t     nElements) = 0;
  /** @brief Read int16 array. */
  virtual int readInt16Array(epicsInt16 *value,
                             size_t      nElements,
                             size_t     *nIn) = 0;
  /** @brief Write int16 array. */
  virtual int writeInt16Array(epicsInt16 *value,
                              size_t      nElements) = 0;
  /** @brief Read int32 array. */
  virtual int readInt32Array(epicsInt32 *value,
                             size_t      nElements,
                             size_t     *nIn) = 0;
  /** @brief Write int32 array. */
  virtual int writeInt32Array(epicsInt32 *value,
                              size_t      nElements) = 0;
  /** @brief Read float array. */
  virtual int readFloat32Array(epicsFloat32 *value,
                               size_t        nElements,
                               size_t       *nIn) = 0;
  /** @brief Write float array. */
  virtual int writeFloat32Array(epicsFloat32 *value,
                                size_t        nElements) = 0;
  /** @brief Read double array. */
  virtual int readFloat64Array(epicsFloat64 *value,
                               size_t        nElements,
                               size_t       *nIn) = 0;
  /** @brief Write double array. */
  virtual int writeFloat64Array(epicsFloat64 *value,
                                size_t        nElements) = 0;
};
#endif /* ECMCASYNLINK_H_ */
