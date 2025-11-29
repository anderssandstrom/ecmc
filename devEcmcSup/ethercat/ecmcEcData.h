/*************************************************************************\
* Copyright (c) 2023 Paul Scherrer Institute
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcEcData.h
*
*  Created on: Oct 27, 2023
*      Author: anderssandstrom
*
\*************************************************************************/

#ifndef ecmcEcData_H_
#define ecmcEcData_H_
#ifdef __cplusplus
/**
 * @file ecmcEcData.h
 * @brief Provides offset-based access to EtherCAT process image data.
 */
#endif
#include <string>
#include <cmath>
#include "stdio.h"
#include "ecrt.h"
#include "ecmcDefinitions.h"
#include "ecmcError.h"
#include "ecmcOctetIF.h"  // Logging macros
#include "ecmcAsynPortDriver.h"
#include "ecmcEcEntry.h"


#define WARNING_DATA_ITEM_EC_ENTRY_DIR_MISSMATCH 0x21150

/**
 * @brief Accessor for arbitrary data within the EtherCAT process image.
 *
 * Data is addressed relative to a start entry using byte/bit offsets and a data type.
 */
class ecmcEcData : public ecmcEcEntry {
public:
  ecmcEcData(ecmcAsynPortDriver *asynPortDriver,
             int                 masterId,
             int                 slaveId,
             ecmcEcEntry        *startEntry,
             size_t              entryByteOffset,
             size_t              entryBitOffset,
             ec_direction_t      nDirection,
             ecmcEcDataType      dt,
             std::string         id);
  ~ecmcEcData();

  // Overridden ecmcEcEntry functions
  /** @brief Enable/disable realtime updates. */
  int  setUpdateInRealtime(int update);
  /** @brief Pull data from process image. */
  int  updateInputProcessImage();
  /** @brief Push data to process image. */
  int  updateOutProcessImage();
  /** @brief Validate offsets and data type. */
  int  validate();

  using DataTransferFunc = void (ecmcEcData::*)();
  struct DataTransferConfig {
    DataTransferFunc input;
    DataTransferFunc output;
    size_t           usedSize;
  };
  static const DataTransferConfig& getTransferConfig(ecmcEcDataType dt);

private:
  void initVars();
  void configureTransferFunctions();
  void noopTransfer();
  void inputBit1();
  void inputBit2();
  void inputBit3();
  void inputBit4();
  void inputU8();
  void inputS8();
  void inputU16();
  void inputS16();
  void inputU32();
  void inputS32();
  void inputU64();
  void inputS64();
  void inputF32();
  void inputF64();
  void outputBit1();
  void outputBit2();
  void outputBit3();
  void outputBit4();
  void outputU8();
  void outputS8();
  void outputU16();
  void outputS16();
  void outputU32();
  void outputS32();
  void outputU64();
  void outputS64();
  void outputF32();
  void outputF64();

  // byte and bit offset from entry
  size_t entryByteOffset_;
  size_t entryBitOffset_;
  size_t byteSize_;
  ecmcEcEntry *startEntry_;
  DataTransferFunc inputTransfer_;
  DataTransferFunc outputTransfer_;
  static uint8_t read_1_bit_offset(uint8_t *buffer,
                                   int      byteOffset,
                                   int      bitOffset);
  static void    write_1_bit_offset(uint8_t *buffer,
                                    int      byteOffset,
                                    int      bitOffset,
                                    uint8_t  value);
  static uint8_t read_2_bit_offset(uint8_t *buffer,
                                   int      byteOffset,
                                   int      bitOffset);
  static void    write_2_bit_offset(uint8_t *buffer,
                                    int      byteOffset,
                                    int      bitOffset,
                                    uint8_t  value);
  static uint8_t read_3_bit_offset(uint8_t *buffer,
                                   int      byteOffset,
                                   int      bitOffset);
  static void    write_3_bit_offset(uint8_t *buffer,
                                    int      byteOffset,
                                    int      bitOffset,
                                    uint8_t  value);
  static uint8_t read_4_bit_offset(uint8_t *buffer,
                                   int      byteOffset,
                                   int      bitOffset);
  static void    write_4_bit_offset(uint8_t *buffer,
                                    int      byteOffset,
                                    int      bitOffset,
                                    uint8_t  value);
  static uint8_t read_uint8_offset(uint8_t *buffer,
                                   int      byteOffset,
                                   int      bitOffset);
  static void    write_uint8_offset(uint8_t *buffer,
                                    int      byteOffset,
                                    int      bitOffset,
                                    uint8_t  value);
  static int8_t read_int8_offset(uint8_t *buffer,
                                 int      byteOffset,
                                 int      bitOffset);
  static void   write_int8_offset(uint8_t *buffer,
                                  int      byteOffset,
                                  int      bitOffset,
                                  int8_t   value);
  static uint16_t read_uint16_offset(uint8_t *buffer,
                                     int      byteOffset,
                                     int      bitOffset);
  static void     write_uint16_offset(uint8_t *buffer,
                                      int      byteOffset,
                                      int      bitOffset,
                                      uint16_t value);
  static int16_t read_int16_offset(uint8_t *buffer,
                                   int      byteOffset,
                                   int      bitOffset);
  static void    write_int16_offset(uint8_t *buffer,
                                    int      byteOffset,
                                    int      bitOffset,
                                    int16_t  value);
  static uint32_t read_uint32_offset(uint8_t *buffer,
                                     int      byteOffset,
                                     int      bitOffset);
  static void     write_uint32_offset(uint8_t *buffer,
                                      int      byteOffset,
                                      int      bitOffset,
                                      uint32_t value);
  static int32_t read_int32_offset(uint8_t *buffer,
                                   int      byteOffset,
                                   int      bitOffset);
  static void    write_int32_offset(uint8_t *buffer,
                                    int      byteOffset,
                                    int      bitOffset,
                                    int32_t  value);
  static uint64_t read_uint64_offset(uint8_t *buffer,
                                     int      byteOffset,
                                     int      bitOffset);
  static void     write_uint64_offset(uint8_t *buffer,
                                      int      byteOffset,
                                      int      bitOffset,
                                      uint64_t value);
  static int64_t read_int64_offset(uint8_t *buffer,
                                   int      byteOffset,
                                   int      bitOffset);
  static void    write_int64_offset(uint8_t *buffer,
                                    int      byteOffset,
                                    int      bitOffset,
                                    int64_t  value);
  static float read_float_offset(uint8_t *buffer,
                                 int      byteOffset,
                                 int      bitOffset);
  static void  write_float_offset(uint8_t *buffer,
                                  int      byteOffset,
                                  int      bitOffset,
                                  float    value);
  static double read_double_offset(uint8_t *buffer,
                                   int      byteOffset,
                                   int      bitOffset);
  static void   write_double_offset(uint8_t *buffer,
                                    int      byteOffset,
                                    int      bitOffset,
                                    double   value);
};
#endif  /* ecmcEcData_H_ */
