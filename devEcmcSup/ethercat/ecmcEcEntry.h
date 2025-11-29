/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcEcEntry.h
*
*  Created on: Dec 2, 2015
*      Author: anderssandstrom
*
\*************************************************************************/

#ifndef ECMCECENTRY_H_
#define ECMCECENTRY_H_
#ifdef __cplusplus
/**
 * @file ecmcEcEntry.h
 * @brief EtherCAT PDO entry wrapper with asyn integration and transfer helpers.
 */
#endif

#include <string>
#include <cmath>
#include "stdio.h"
#include "limits.h"
#include "ecrt.h"
#include "ecmcDefinitions.h"
#include "ecmcError.h"
#include "ecmcEcDomain.h"
#include "ecmcOctetIF.h"
#include "ecmcAsynPortDriver.h"
#include "alarm.h"  // EPICS alarms

#include "asynPortDriver.h"
#ifndef VERSION_INT
#  define VERSION_INT(V, R, M,\
                      P) (((V) << 24) | ((R) << 16) | ((M) << 8) | (P))
#endif // ifndef VERSION_INT

#define VERSION_INT_4_37            VERSION_INT(4, 37, 0, 0)
#define ECMC_ASYN_VERSION_INT VERSION_INT(ASYN_VERSION,\
                                          ASYN_REVISION,\
                                          ASYN_MODIFICATION,\
                                          0)

#if ECMC_ASYN_VERSION_INT >= VERSION_INT_4_37
#define ECMC_ASYN_ASYNPARAMINT64
#endif // if ECMC_ASYN_VERSION_INT >= VERSION_INT_4_37

// ECENTRY ERRORS
#define ERROR_EC_ENTRY_DATA_POINTER_NULL 0x21000
#define ERROR_EC_ENTRY_INVALID_OFFSET 0x21001
#define ERROR_EC_ENTRY_INVALID_DOMAIN_ADR 0x21002
#define ERROR_EC_ENTRY_INVALID_BIT_LENGTH 0x21003
#define ERROR_EC_ENTRY_LINK_FAILED 0x21004
#define ERROR_EC_ENTRY_INDEX_OUT_OF_RANGE 0x21005
#define ERROR_EC_ENTRY_INVALID_BIT_INDEX 0x21006
#define ERROR_EC_ENTRY_READ_FAIL 0x21007
#define ERROR_EC_ENTRY_WRITE_FAIL 0x21008
#define ERROR_EC_ENTRY_ASYN_TYPE_NOT_SUPPORTED 0x21009
#define ERROR_EC_ENTRY_ASSIGN_ADD_FAIL 0x2100A
#define ERROR_EC_ENTRY_REGISTER_FAIL 0x2100B
#define ERROR_EC_ENTRY_VALUE_OUT_OF_RANGE 0x2100C
#define ERROR_EC_ENTRY_SET_ALARM_STATE_FAIL 0x2100D
#define ERROR_EC_ENTRY_EC_DOMAIN_ERROR 0x2100E
#define ERROR_EC_ENTRY_DATATYPE_INVALID 0x2100F
#define ERROR_EC_ENTRY_SIZE_OUT_OF_RANGE 0x21010

/**
 * @brief Represents a PDO entry and handles process image transfers.
 */
class ecmcEcEntry : public ecmcError {
public:
  ecmcEcEntry(ecmcAsynPortDriver *asynPortDriver,
              int                 masterId,
              int                 slaveId,
              ecmcEcDomain       *domain,
              ec_slave_config_t  *slave,
              uint16_t            pdoIndex,
              uint16_t            entryIndex,
              uint8_t             entrySubIndex,
              ec_direction_t      nDirection,
              ecmcEcDataType      dt,
              std::string         id,
              int                 useInRealtime);

  // only used for simulation purpose
  ecmcEcEntry(ecmcAsynPortDriver *asynPortDriver,
              int                 masterId,
              int                 slaveId,
              uint8_t            *domainAdr,
              ecmcEcDataType      dt,
              std::string         id);
  virtual ~ecmcEcEntry();
  void                  initVars();
  uint16_t              getEntryIndex();
  uint8_t               getEntrySubIndex();
  /** @brief Get number of bits in entry. */
  int                   getBits();
  /** @brief Populate PDO entry info structure. */
  int                   getEntryInfo(ec_pdo_entry_info_t *info);
  /** @brief Return byte offset within domain. */
  int                   getByteOffset();
  /** @brief Access entry data type. */
  ecmcEcDataType        getDataType();

  // After activate
  /** @brief Activate entry after domain setup. */
  int                   activate();

  uint8_t*              getDomainAdr();
  /** @brief Write raw value. */
  int                   writeValue(uint64_t value);
  /** @brief Write double value with conversion. */
  int                   writeDouble(double value);
  /** @brief Force write raw value bypassing domain check. */
  int                   writeValueForce(uint64_t value);
  /** @brief Write single bit at relative index. */
  int                   writeBit(int      bitNumber,
                                 uint64_t value);
  /** @brief Force write single bit. */
  int                   writeBitForce(int bitNumber, uint64_t value);
  /** @brief Write range of bits. */
  int                   writeBits(int startBitNumber, int bits,
                                  uint64_t valueToWrite);
  /** @brief Read raw value. */
  int                   readValue(uint64_t *value);
  /** @brief Read double value with conversion. */
  int                   readDouble(double *value);
  /** @brief Read single bit. */
  int                   readBit(int       bitNumber,
                                uint64_t *value);
  /** @brief Read range of bits. */
  int                   readBits(int startBitNumber,
                                 int bits, uint64_t *result);
  /** @brief Update entry from process image. */
  virtual int           updateInputProcessImage();
  /** @brief Push entry value to process image. */
  virtual int           updateOutProcessImage();
  /** @brief Toggle realtime updates. */
  int                   setUpdateInRealtime(int update);
  /** @brief Query realtime update flag. */
  int                   getUpdateInRealtime();
  /** @brief Human-friendly identifier. */
  std::string           getIdentificationName();
  /** @brief Register PDO entry mapping info. */
  int                   compileRegInfo();
  /** @brief Refresh asyn parameters. */
  int                   updateAsyn(bool force);
  /** @brief True if simulation entry. */
  bool                  getSimEntry();
  /** @brief Validate offsets, sizes, and pointers. */
  virtual int           validate();
  /** @brief Set communication alarm flag. */
  int                   setComAlarm(bool alarm);
  /** @brief Get slave ID owning this entry. */
  int                   getSlaveId();
  /** @brief Check domain OK flag. */
  virtual int           getDomainOK();
  /** @brief Domain object pointer. */
  virtual ecmcEcDomain* getDomain();
  /** @brief Direction of data transfer. */
  ec_direction_t        getDirection();
  using TransferFunc = void (ecmcEcEntry::*)();
  struct TransferConfig {
    TransferFunc input;
    TransferFunc output;
    size_t       usedSize;
    bool         supportInt32;
    bool         supportUInt32Digital;
    bool         supportFloat64;
    bool         supportInt64;
  };
  static const TransferConfig& getTransferConfig(ecmcEcDataType dt);

protected:
  void                  setDomainAdr();
  int                   initAsyn();

  void         configureTransferFunctions();
  void         noopTransfer();
  void         inputNone();
  void         outputNone();
  void         inputBit1();
  void         inputBit2();
  void         inputBit3();
  void         inputBit4();
  void         inputU8();
  void         inputS8();
  void         inputS8ToU8();
  void         inputU16();
  void         inputS16();
  void         inputS16ToU16();
  void         inputU32();
  void         inputS32();
  void         inputS32ToU32();
#ifdef EC_READ_U64
  void         inputU64();
#endif
#ifdef EC_READ_S64
  void         inputS64();
  void         inputS64ToU64();
#endif
#ifdef EC_READ_REAL
  void         inputF32();
#endif
#ifdef EC_READ_LREAL
  void         inputF64();
#endif
  void         outputBit1();
  void         outputBit2();
  void         outputBit3();
  void         outputBit4();
  void         outputU8();
  void         outputS8();
  void         outputS8ToU8();
  void         outputU16();
  void         outputS16();
  void         outputS16ToU16();
  void         outputU32();
  void         outputS32();
  void         outputS32ToU32();
#ifdef EC_WRITE_U64
  void         outputU64();
#endif
#ifdef EC_WRITE_S64
  void         outputS64();
  void         outputS64ToU64();
#endif
#ifdef EC_WRITE_REAL
  void         outputF32();
#endif
#ifdef EC_WRITE_LREAL
  void         outputF64();
#endif
  uint8_t *domainAdr_;
  uint8_t *adr_;
  uint16_t entryIndex_;
  uint8_t entrySubIndex_;
  int16_t pdoIndex_;
  uint bitOffset_;
  bool sim_;
  std::string idString_;
  char *idStringChar_;
  int updateInRealTime_;
  int masterId_;
  int slaveId_;
  int bitLength_;
  int byteOffset_;
  ecmcAsynPortDriver *asynPortDriver_;
  ecmcAsynDataItem *entryAsynParam_;
  ecmcEcDataType dataType_;
  ec_slave_config_t *slave_;
  ecmcEcDomain *domain_;
  ec_direction_t direction_;
  uint64_t buffer_;
  int8_t *int8Ptr_;
  uint8_t *uint8Ptr_;
  int16_t *int16Ptr_;
  uint16_t *uint16Ptr_;
  int32_t *int32Ptr_;
  uint32_t *uint32Ptr_;
  int64_t *int64Ptr_;
  uint64_t *uint64Ptr_;
  float *float32Ptr_;
  double *float64Ptr_;
  size_t usedSizeBytes_;
  TransferFunc inputTransfer_;
  TransferFunc outputTransfer_;
};
#endif  /* ECMCECENTRY_H_ */
