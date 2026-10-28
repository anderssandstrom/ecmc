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

#include <string>
#include <vector>
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

#include "ecmcDataErrors.h"
#include "ecmcDataAccess.h"

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
              int                 useInRealtime,
              bool                useExistingMapping = false,
              bool                registerByPosition = false,
              uint8_t             syncManagerIndex = 0,
              unsigned int        pdoPosition = 0,
              unsigned int        entryPosition = 0);

  // only used for simulation purpose
  ecmcEcEntry(ecmcAsynPortDriver *asynPortDriver,
              int                 masterId,
              int                 slaveId,
              uint8_t            *domainAdr,
              ecmcEcDataType      dt,
              std::string         id);
  virtual ~ecmcEcEntry();
  virtual std::shared_ptr<ecmcDataAccess> getDataAccess();
  void                  initVars();
  uint16_t              getEntryIndex();
  uint8_t               getEntrySubIndex();
  int                   getBits();
  int                   getEntryInfo(ec_pdo_entry_info_t *info);
  int                   getByteOffset();
  ecmcEcDataType        getDataType();

  // After activate
  int                   activate();

  uint8_t*              getDomainAdr();
  int                   writeValue(uint64_t value);
  int                   writeDouble(double value);
  int                   writeValueForce(uint64_t value);
  int                   writeBit(int      bitNumber,
                                 uint64_t value);
  int                   writeBitForce(int bitNumber, uint64_t value);
  int                   writeBits(int startBitNumber, int bits,
                                  uint64_t valueToWrite);
  int                   readValue(uint64_t *value);
  inline void           copyValueTo(ecmcEcEntry *destination,
                                    uint64_t mask) const {
    destination->buffer_ = buffer_ & mask;
  }
  inline bool           matchesValueBuffer(const void *data) const {
    return data == &buffer_;
  }
  int                   readDouble(double *value);
  int                   readBit(int       bitNumber,
                                uint64_t *value);
  int                   readBits(int startBitNumber,
                                 int bits, uint64_t *result);
  virtual int           updateInputProcessImage();
  virtual int           updateOutProcessImage();
  int                   setUpdateInRealtime(int update);
  int                   getUpdateInRealtime();
  std::string           getIdentificationName();
  bool                  matchesIdentificationName(const std::string& id);
  int                   addAlias(const std::string& alias);
  int                   compileRegInfo();
  int                   updateAsyn(bool force);
  bool                  getSimEntry();
  virtual int           validate();
  int                   setComAlarm(bool alarm);
  int                   getSlaveId();
  virtual int           getDomainOK();
  virtual ecmcEcDomain* getDomain();
  ec_direction_t        getDirection();

protected:
  std::shared_ptr<ecmcDataAccess> getDirectDataAccess();
  typedef void (ecmcEcEntry::*processImageReadFuncPtr)();
  typedef void (ecmcEcEntry::*processImageWriteFuncPtr)();

  void                  setDomainAdr();
  int                   initAsyn();
  void                  bindProcessImageHandlers();
  void                  readNone();
  void                  readB1();
  void                  readB2();
  void                  readB3();
  void                  readB4();
  void                  readU8();
  void                  readS8();
  void                  readS8ToU8();
  void                  readU16();
  void                  readS16();
  void                  readS16ToU16();
  void                  readU32();
  void                  readS32();
  void                  readS32ToU32();
#ifdef EC_READ_U64
  void                  readU64();
#endif
#ifdef EC_READ_S64
  void                  readS64();
  void                  readS64ToU64();
#endif
#ifdef EC_READ_REAL
  void                  readF32();
#endif
#ifdef EC_READ_LREAL
  void                  readF64();
#endif
  void                  writeNone();
  void                  writeB1();
  void                  writeB2();
  void                  writeB3();
  void                  writeB4();
  void                  writeU8();
  void                  writeS8();
  void                  writeS8ToU8();
  void                  writeU16();
  void                  writeS16();
  void                  writeS16ToU16();
  void                  writeU32();
  void                  writeS32();
  void                  writeS32ToU32();
#ifdef EC_WRITE_U64
  void                  writeU64();
#endif
#ifdef EC_WRITE_S64
  void                  writeS64();
  void                  writeS64ToU64();
#endif
#ifdef EC_WRITE_REAL
  void                  writeF32();
#endif
#ifdef EC_WRITE_LREAL
  void                  writeF64();
#endif
  uint8_t *domainAdr_;
  uint8_t *adr_;
  uint16_t entryIndex_;
  uint8_t entrySubIndex_;
  int16_t pdoIndex_;
  uint bitOffset_;
  bool sim_;
  std::string idString_;
  std::vector<std::string> aliasStrings_;
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
  bool useExistingMapping_;
  bool registerByPosition_;
  uint8_t syncManagerIndex_;
  unsigned int pdoPosition_;
  unsigned int entryPosition_;
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
  processImageReadFuncPtr processImageReadFunc_;
  processImageWriteFuncPtr processImageWriteFunc_;
  std::shared_ptr<ecmcDataAccess> dataAccess_;
};
#endif  /* ECMCECENTRY_H_ */
