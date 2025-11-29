/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcEthercat.h
*
*  Created on: Jan 10, 2019
*      Author: anderssandstrom
*
\*************************************************************************/

/**
@file
    @brief EtherCAT commands
*/

#ifndef ECMC_ETHERCAT_H_
#define ECMC_ETHERCAT_H_

# ifdef __cplusplus
extern "C" {
# endif  // ifdef __cplusplus

/** @brief Selects EtherCAT master to use.
 *
 *  @param[in] masterIndex EtherCAT master index.
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: Select /dev/EtherCAT0.
 *  "Cfg.EcSetMaster(0)" //Command string to ecmcCmdParser.c
 */
int ecSetMaster(int masterIndex);

/** @brief  Retry configuring slaves for an selected EtherCAT master.
 *
 * Via this method, the application can tell the master to bring all slaves to
 * OP state. In general, this is not necessary, because it is automatically
 * done by the master. But with special slaves, that can be reconfigured by
 * the vendor during runtime, it can be useful.
 *
 * @note  masterIndex is not used. Currently only one master is supported and
 * this master will be reset. The masterIndex is for future use.
 *
 *  @param[in] masterIndex EtherCAT master index.
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: Reset master. Master index is NOT currently supported.
 *  "Cfg.EcResetMaster(10)" //Command string to ecmcCmdParser.c
 */
int ecResetMaster(int masterIndex);

/** @brief Adds an EtherCAT slave to the hardware configuration.
 *
 * Each added slave will be assigned an additional index which will be zero for
 * the first successfully added slave and then incremented for each successful
 * call to "Cfg.EcAddSlave()".
 *
 * NOTE: if an complete entry needs to be configured the command
 * "Cfg.EcAddEntryComplete()" should be used. This command will add slave,
 * sync. manger, pdo and entry if needed.
 *
 *  @param alias Alias of slave. Set to zero to disable.
 *  @param slaveBusPosition Position of the EtherCAT slave on the bus.
 *    slaveBusPosition = -1: Used to address the simulation slave. Only two
 *                           entries are configured, "ZERO" with default
 *                           value 0 and "ONE" with default value 1.
 *    slaveBusPosition = 0..65535: Addressing of normal EtherCAT slaves.
 *  @param vendorId Identification value for slave vendor.
 *    vendorId = 0x2: Beckhoff.
 *    vendorId = 0x48554B: Kendrion Kuhnke Automation GmbH.
 *  @param productCode Product identification code.
 *    productCode=0x13ed3052: EL5101 incremental encoder input.
 *
 * @note All configuration data can be found in the documentation for the
 * slave, in the ESI slave description file or by using the EtherLab
 * ethercat tool.
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: Add a EL5101 Beckhoff slave at slave position 1.
 *  "Cfg.EcAddSlave(0,1,0x2,0x13ed3052)" //Command string to ecmcCmdParser.c
 */
int ecAddSlave(uint16_t alias,
               uint16_t position,
               uint32_t vendorId,
               uint32_t productCode);

/// obsolete command. Use ecAddEntryComplete() command instead.
int ecAddSyncManager(int     slaveIndex,
                     int     direction,
                     uint8_t syncManagerIndex);

/// obsolete command. Use ecAddEntryComplete() command instead.
int ecAddPdo(int      slaveIndex,
             int      syncManager,
             uint16_t pdoIndex);

/** @brief Adds an EtherCAT entry and all prerequisites.
 *
 *  @param[in] slaveBusPosition Position of the EtherCAT slave on the bus.
 *    slaveBusPosition = -1: Used to address the simulation slave. Only two
 *                           entries are configured, "ZERO" with default
 *                           value 0 and "ONE" with default value 1.
 *    slaveBusPosition = 0..65535: Addressing of normal EtherCAT slaves.
 *  @param[in] vendorId Identification value for slave vendor.
 *    vendorId = 0x2: Beckhoff.
 *    vendorId = 0x48554B: Kendrion Kuhnke Automation GmbH.
 *  @param[in] productCode Product identification code.
 *    productCode=0x13ed3052: EL5101 incremental encoder input.
 *  @param[in] direction Data transfer direction.
 *    direction  = 1:  Output (from master).
 *    direction  = 2:  Input (to master).
 *  @param[in] syncManagerIndex Index of sync manager.
 *  @param[in] pdoIndex Index of process data object. Needs to be entered
 *                           in hex format.
 *  @param[in] entryIndex Index of process data object entry. Needs to be
 *                           entered in hex format.
 *  @param[in] entrySubIndex Index of process data object sub entry.
 *                           Needs to be entered in hex format.
 *  @param[in] bits Bit count.
 *  @param[in] entryIDString Identification string used for addressing the
 *                           entry.
 *  @param[in] signedValue 1 if value is of signed type otherwise 0
 *
 * @note All configuration data can be found in the documentation for the
 * slave, in the ESI slave description file or by using the EtherLab
 * ethercat tool.
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: Add an EtherCAT entry for the actual position of an EL5101
 * incremental encoder card.
 * "Cfg.EcAddEntryComplete(2,0x2,0x13ed3052,2,3,0x1a03,0x6010,0x10,16,POSITION)"
 * //Command string to ecmcCmdParser.c
 *
 * @note Example: Add an EtherCAT entry for the velocity setpoint of an EL7037
 * stepper drive card.
 * "Cfg.EcAddEntryComplete(7,0x2,0x1b7d3052,1,2,0x1604,0x7010,0x21,16,
 * VELOCITY_SETPOINT)" //Command string to ecmcCmdParser.c
 */
int ecAddEntryComplete(
  uint16_t slaveBusPosition,
  uint32_t vendorId,
  uint32_t productCode,
  int      direction,
  uint8_t  syncManagerIndex,
  uint16_t pdoIndex,
  uint16_t entryIndex,
  uint8_t  entrySubIndex,
  uint8_t  bits,
  char    *entryIDString,
  int      signedValue);

/** @brief Adds an EtherCAT entry (legacy syntax).
 *
 *  @param[in] slaveBusPosition Position of the EtherCAT slave on the bus.
 *    slaveBusPosition = -1: Used to address the simulation slave. Only two
 *                           entries are configured, "ZERO" with default
 *                           value 0 and "ONE" with default value 1.
 *    slaveBusPosition = 0..65535: Addressing of normal EtherCAT slaves.
 *  @param[in] vendorId Identification value for slave vendor.
 *    vendorId = 0x2: Beckhoff.
 *    vendorId = 0x48554B: Kendrion Kuhnke Automation GmbH.
 *  @param[in] productCode Product identification code.
 *    productCode=0x13ed3052: EL5101 incremental encoder input.
 *  @param[in] direction Data transfer direction.
 *    direction  = 1:  Output (from master).
 *    direction  = 2:  Input (to master).
 *  @param[in] syncManagerIndex Index of sync manager.
 *  @param[in] pdoIndex Index of process data object. Needs to be entered
 *                           in hex format.
 *  @param[in] entryIndex Index of process data object entry. Needs to be
 *                           entered in hex format.
 *  @param[in] entrySubIndex Index of process data object sub entry.
 *                           Needs to be entered in hex format.
 *  @param[in] dataType DataType of ethercat data:
 *                      B1:  1-bit
 *                      B2:  2-bits (lsb)
 *                      B3:  3-bits (lsb)
 *                      B4:  4-bits (lsb)
 *                      U8:  Unsigned 8-bit
 *                      S8:  Signed 8-bit
 *                      U16: Unsigned 16-bit
 *                      S16: Signed 16-bit
 *                      U32: Unsigned 32-bit
 *                      S32: Signed 32-bit
 *                      U64: Unsigned 64-bit
 *                      S64: Signed 64-bit
 *                      F32: Real 32-bit
 *                      F64: Double 64-bit
 *                      // Special datatypes with conversions (maps -+2^(n-1) to 0..2^n)
 *                      S8_TO_U8   : int8 converted to uint8, offset by 2^7
 *                      S16_TO_U16 : int16 converted to uint16, offset by 2^15
 *                      S32_TO_U32 : int32 converted to uint32, offset by 2^31
 *                      S64_TO_U64 : int64 converted to uint64, offset by 2^63
 *
 *  @param[in] entryIDString Identification string used for addressing the
 *                           entry.
 *  @param[in] updateInRT    1 if value should be updated in realtime 0
 *                           normally set to zero for entries that are
 *                           covered in memmaps.
 *
 * @note All configuration data can be found in the documentation of
 * the slave, in the ESI slave description file or by using the etherlab
 * (www.etherlab.org) ethercat tool.
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: Add an EtherCAT entry for the actual position of an EL5101
 * incremental encoder card.
 * "Cfg.EcAddEntry(2,0x2,0x13ed3052,2,3,0x1a03,0x6010,0x10,U16,POSITION,1)"
 * //Command string to ecmcCmdParser.c
 *
 * @note Example: Add an EtherCAT entry for the velocity setpoint of an EL7037
 * stepper drive card.
 * "Cfg.EcAddEntry(7,0x2,0x1b7d3052,1,2,0x1604,0x7010,0x21,S16,
 * VELOCITY_SETPOINT,1)" //Command string to ecmcCmdParser.c
 */
int ecAddEntry(
  uint16_t slaveBusPosition,
  uint32_t vendorId,
  uint32_t productCode,
  int      direction,
  uint8_t  syncManagerIndex,
  uint16_t pdoIndex,
  uint16_t entryIndex,
  uint8_t  entrySubIndex,
  char    *datatype,
  char    *entryIDString,
  int      updateInRealTime);

/** @brief Adds an async SDO object for runtime read/write.
 *
 * Adds an SDO object that can be accessed during runtime.
 *
 *  @param[in] slaveBusPosition Position of the EtherCAT slave on the bus.
 *    slaveBusPosition = -1: Used to address the simulation slave. Only two
 *                           entries are configured, "ZERO" with default
 *                           value 0 and "ONE" with default value 1.
 *    slaveBusPosition = 0..65535: Addressing of normal EtherCAT slaves.
 *  @param[in] index Index of process data object entry (hex).
 *  @param[in] subIndex Index of process data object sub entry (hex).
 *  @param[in] dataType DataType of ethercat data: U8, S8, U16, S16, U32, S32, U64, S64, F32, F64.
 *  @param[in] idString Identification string used for addressing the entry.
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example:
 * @code
 * Cfg.EcAddSdoAsync(3,0x8012,0x32,U8,"hwenable")
 * @endcode
 */
int ecAddSdoAsync(
  uint16_t slaveBusPosition,
  uint16_t entryIndex,
  uint8_t  entrySubIndex,
  char    *datatype,
  char    *idString);

/** @brief Adds a memory map object for direct EtherCAT access (preferred syntax).
 *
 *  The start of the memory map is addressed by a previously configured
 *  EtherCAT entry and a size.
 *
 *  @param[in] ecPath Identification string of the start EtherCAT entry (example "ec0.s1.AI_1").
 *  @param[in] byteSize Size of the memory map region (bytes).
 *  @param[in] direction Data transfer direction (1 = output, 2 = input).
 *  @param[in] dataType Data type of the EtherCAT data: U8, S8, U16, S16, U32, S32, U64, S64, F32, F64.
 *  @param[in] memMapIdString Identification string used for addressing the object.
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example:
 * @code
 * Cfg.EcAddMemMapDT(ec0.s10.AI1,200,2,S32,ec0.s10.mm.WAVEFORM)
 * @endcode
 */
int ecAddMemMapDT(
  char  *ecPath,
  size_t byteSize,
  int    direction,
  char  *dataType,
  char  *memMapIDString);

/** @brief Adds a memory map object for direct EtherCAT access (legacy syntax).
 *
 *  The start of the memory map is addressed by a previously configured
 *  EtherCAT entry and a size.
 *
 *  @param[in] startEntryBusPosition Bus position where the start entry is configured (0..65535).
 *  @param[in] startEntryIDString Identification string of the start EtherCAT entry.
 *  @param[in] byteSize Size of the memory map region (bytes).
 *  @param[in] direction Data transfer direction (1 = output, 2 = input).
 *  @param[in] entryIDString Identification string used for addressing the object.
 *
 *  @return 0 if success or otherwise an error code.
 *
 *  @note Example:
 *  @code
 *  Cfg.EcAddMemMap(10,AI1,200,2,ec0.mm.WAVEFORM)
 *  @endcode
 */
int ecAddMemMap(
  uint16_t startEntryBusPosition,
  char    *startEntryIDString,
  size_t   byteSize,
  int      direction,
  char    *memMapIDString);

/** @brief Adds a data item object for direct EtherCAT access.
 *
 *  The start of the data item is addressed by a previously configured
 *  EtherCAT entry, plus a byte and bit offset and data type.
 *
 *  @param[in] ecPath Identification string of the start EtherCAT entry (example "ec0.s1.AI_1").
 *  @param[in] entryByteOffset Byte offset.
 *  @param[in] entryBitOffset Bit offset.
 *  @param[in] direction Data transfer direction (1 = output, 2 = input).
 *  @param[in] dataType Data type of the EtherCAT data: B1, B2, B3, B4, U8, S8, U16, S16, U32, S32, U64, S64, F32, F64.
 *  @param[in] idString Identification string used for addressing the object (prefix "ec<masterid>.s<slaveid>" is added).
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example:
 * @code
 * Cfg.EcAddDataDT(ec0.s2.byte09,0,2,B1,a_bit)
 * @endcode
 */
int ecAddDataDT(
  char  *ecPath,
  size_t entryByteOffset,
  size_t entryBitOffset,
  int    direction,
  char  *dataType,
  char  *idString
  );

/** @brief Get index of a memmap object based on its name id string
 *
 *  @param[in] memMapIDString memmap name
 *  @param[out] index index of memmap corresponding to memMapIDString
 *
 *  @return 0 if success or otherwise an error code.
 *
 *  @note Example: Get memmap id.
 *  "EcGetMemMapId("ec0.s2.mm.CH1_ARRAY")" //Command string to ecmcCmdParser.c
 */
int ecGetMemMapId(char *memMapIDString,
                  int  *id);

/** @brief Configure slave DC clock.
 *
 *  @param[in] slaveBusPosition Position of the EtherCAT slave on the bus (0..65535).
 *  @param[in] assignActivate AssignActivate word (hex, see ESI documentation).
 *  @param[in] sync0Cycle Cycle time in ns.
 *  @param[in] sync0Shift Phase shift in ns.
 *  @param[in] sync1Cycle Cycle time in ns.
 *  @param[in] sync1Shift Not used.
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example:
 * @code
 * Cfg.EcSlaveConfigDC(1,0x320,1000000,10,1000000,0)
 * @endcode
 */
int ecSlaveConfigDC(
  int      slaveBusPosition,
  uint16_t assignActivate,   /**< AssignActivate word. */
  uint32_t sync0Cycle,       /**< SYNC0 cycle time [ns]. */
  int32_t  sync0Shift,       /**< SYNC0 shift time [ns]. */
  uint32_t sync1Cycle,       /**< SYNC1 cycle time [ns]. */
  int32_t  sync1Shift /**< SYNC1 shift time [ns]. */);

/** @brief Select EtherCAT reference clock.
 *
 *  @param[in] masterIndex Index of master, see command ecSetMaster().
 *  @param[in] slaveBusPosition Position of the EtherCAT slave on the bus.
 *                              Note: the slave needs to be equipped
 *                              with a DC.
 *    slaveBusPosition = 0..65535: Addressing of EtherCAT slaves.*
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: Select slave 3 as reference clock for master 0.
 * "Cfg.EcSelectReferenceDC(0,3)" //Command string to ecmcCmdParser.c
 */
int ecSelectReferenceDC(int masterIndex,
                        int slaveBusPosition);

/** @brief Adds a Service Data Object for writing.
 *
 * Adds a Service Data Object for writing to the sdo registers of EtherCAT
 * slaves. An sdo object will be added to the hardware configuration.
 * The writing will occur when the hardware configuration is applied.
 *
 * @note This command can only be used in configuration mode.
 *
 *  @param[in] slaveBusPosition Position of the EtherCAT slave on the bus.
 *    slaveBusPosition = 0..65535: Addressing of EtherCAT slaves.
 *  @param[in] sdoIndex Index of service data object. Needs to be
 *                           entered in hex format.
 *  @param[in] sdoSubIndex Sub index of service data object .
 *                           Needs to be entered in hex format.
 *  @param[in] value Value to write.
 *  @param[in] byteSize Byte count to write.
 *
 * @note All configuration data can be found in the documentation of
 * the slave, in the ESI slave description file or by using the etherlab
 * (www.etherlab.org) ethercat tool.
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: Write 1A (1000mA) to maximum current of the EL7037 stepper drive card
 * on slave position 2.
 * "Cfg.EcAddSdo(2,0x8010,0x1,1000,2)" //Command string to ecmcCmdParser.c
 */
int ecAddSdo(uint16_t slaveBusPosition,
             uint16_t sdoIndex,
             uint8_t  sdoSubIndex,
             uint32_t value,
             int      byteSize);

/** @brief Adds a Service Data Object for writing.
 *
 * Adds a Service Data Object for writing to the sdo registers of EtherCAT
 * slaves. An sdo object will be added to the hardware configuration.
 * The writing will occur when the hardware configuration is applied.
 *
 * @note This command can only be used in configuration mode.
 *
 *  @param[in] slaveBusPosition Position of the EtherCAT slave on the bus.
 *    slaveBusPosition = 0..65535: Addressing of EtherCAT slaves.
 *  @param[in] sdoIndex Index of service data object. Needs to be
 *                           entered in hex format.
 *  @param[in] sdoSubIndex Sub index of service data object .
 *                           Needs to be entered in hex format.
 *  @param[in] valueString Value to write.
 *  @param[in] dataType ecmc data type string.
 *                      U8:  Unsigned 8-bit
 *                      S8:  Signed 8-bit
 *                      U16: Unsigned 16-bit
 *                      S16: Signed 16-bit
 *                      U32: Unsigned 32-bit
 *                      S32: Signed 32-bit
 *                      U64: Unsigned 64-bit
 *                      S64: Signed 64-bit
 *                      F32: Real 32-bit
 *                      F64: Double 64-bit
 *
 * @note All configuration data can be found in the documentation of
 * the slave, in the ESI slave description file or by using the etherlab
 * (www.etherlab.org) ethercat tool.
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: Write 1A (1000mA) to maximum current of the EL7037 stepper drive card
 * on slave position 2.
 * "Cfg.EcAddSdoDT(2,0x8010,0x1,1000.0,F64)" //Command string to ecmcCmdParser.c
 */
int ecAddSdoDT(uint16_t slavePosition,
               uint16_t sdoIndex,
               uint8_t  sdoSubIndex,
               char    *valueString,
               char    *datatype);

/** @brief Adds a Service Data Object for writing.
 *
 * Adds a Service Data Object for writing to the sdo registers of EtherCAT
 * slaves. An sdo object will be added to the hardware configuration.
 * The writing will occur when the hardware configuration is applied.
 *
 * @note This command can only be used in configuration mode.
 *
 *  @param[in] slaveBusPosition Position of the EtherCAT slave on the bus.
 *    slaveBusPosition = 0..65535: Addressing of EtherCAT slaves.
 *  @param[in] sdoIndex Index of service data object. Needs to be
 *                           entered in hex format.
 *  @param[in] valueBuffer Values to be written as hex string, each byte separated with space.
 *  @param[in] byteSize Byte count to write.
 *
 * @note All configuration data can be found in the documentation of
 * the slave, in the ESI slave description file or by using the etherlab
 * (www.etherlab.org) ethercat tool.
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: Write a hex string of data to slave position 2.
 * "Cfg.EcAddSdoComplete(2,0x8010,0A FF CA 01 25 F1,6)" //Command string to ecmcCmdParser.c
 */
int ecAddSdoComplete(uint16_t    slaveBusPosition,
                     uint16_t    sdoIndex,
                     const char *valueBuffer,
                     int         byteSize);

/** @brief Adds a Service Data Object for writing.
 *
 * Adds a Service Data Object for writing to the sdo registers of EtherCAT
 * slaves. An sdo object will be added to the hardware configuration.
 * The writing will occur when the hardware configuration is applied.
 *
 * @note This command can only be used in configuration mode.
 *
 *  @param[in] slaveBusPosition Position of the EtherCAT slave on the bus.
 *    slaveBusPosition = 0..65535: Addressing of EtherCAT slaves.
 *  @param[in] sdoIndex Index of service data object. Needs to be
 *                           entered in hex format.
 *  @param[in] sdoSubIndex Sub index of service data object. Needs to be
 *                         entered in hex format.
 *  @param[in] valueBuffer Values to be written as hex string, each byte separated with space.
 *  @param[in] byteSize Byte count to write.
 *
 * @note All configuration data can be found in the documentation of
 * the slave, in the ESI slave description file or by using the etherlab
 * (www.etherlab.org) ethercat tool.
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: Write a hex string of data to slave position 2.
 * "Cfg.EcAddSdoBuffer(2,0x8010,0x1,0A FF CA 01 25 F1,6)" //Command string to ecmcCmdParser.c
 */
int ecAddSdoBuffer(uint16_t    slavePosition,
                   uint16_t    sdoIndex,
                   uint8_t     sdoSubIndex,
                   const char *valueBuffer,
                   int         byteSize);

/** @brief Write to a Service Data Object.
 *
 * Writing will occur directly when the command is issued.
 *
 * @note This command can only be used in configuration mode.
 *
 *  @param[in] slaveBusPosition Position of the EtherCAT slave on the bus.
 *    slaveBusPosition = 0..65535: Addressing of EtherCAT slaves.
 *  @param[in] sdoIndex Index of service data object. Needs to be
 *                           entered in hex format.
 *  @param[in] sdoSubIndex Sub index of service data object .
 *                           Needs to be entered in hex format.
 *  @param[in] value Value to write.
 *  @param[in] byteSize Byte count to write.
 *
 * @note All configuration data can be found in the documentation of
 * the slave, in the ESI slave description file or by using the etherlab
 * (www.etherlab.org) ethercat tool.
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: Write 1A (1000mA) to maximum current of the EL7037 stepper drive card
 * on slave position 2.
 * "Cfg.EcWriteSdo(2,0x8010,0x1,1000,2)" //Command string to ecmcCmdParser.c
 */
int ecWriteSdo(uint16_t slavePosition,
               uint16_t sdoIndex,
               uint8_t  sdoSubIndex,
               uint32_t value,
               int      byteSize);

/** @brief Allow domain to be offline
 *
 *  @param[in] allow 0 == don't allow (default), 1 == allow
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: Allow current domain to be offline.
 * "Cfg.EcSetDomAllowOffline(1)" //Command string to ecmcCmdParser.c
 */
int ecSetDomAllowOffline(int allow);

/** @brief Allow master to be offline
 *
 *  @param[in] allow 0 == don't allow (default), 1 == allow
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: Allow master to be offline.
 * "Cfg.EcSetEcAllowOffline(1)" //Command string to ecmcCmdParser.c
 */
int ecSetEcAllowOffline(int allow);

/** @brief Add domain
 *
 *  @param[in] rate execute domain in this rate (cycles)
 *  @param[in] offset offset cycles
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: Add a domain that executes every 10:th cycles with an offset off 1 cycle:
 * "Cfg.EcAddDomain(10,1)" //Command string to ecmcCmdParser.c
 */
int ecAddDomain(int rateCycles,
                int offsetCycles);

/** @brief Write to a Service Data Object.
 *
 * Note: same  as "ecWriteSdo(uint16_t slavePposition,uint16_t sdoIndex,uint8_t sdoSubIndex,
 * uint32_t value,int byteSize)" but without subindex. Complete SDO access will be used.
 *
 */
/*int ecWriteSdoComplete(uint16_t slavePosition,
                       uint16_t sdoIndex,
                       uint32_t value,
                       int      byteSize);*/

/** @brief Read a Service Data Object.
 *
 * Read will occur directly when the command is issued.
 *
 * @note This command can only be used in configuration mode.
 *
 *  @param[in] slaveBusPosition Position of the EtherCAT slave on the bus.
 *    slaveBusPosition = 0..65535: Addressing of EtherCAT slaves.
 *  @param[in] sdoIndex Index of service data object. Needs to be
 *                           entered in hex format.
 *  @param[in] sdoSubIndex Sub index of service data object .
 *                           Needs to be entered in hex format.
 *  @param[in] byteSize Byte count to read.
 *  @param[out] data      Read data.
 *
 * @note All configuration data can be found in the documentation of
 * the slave, in the ESI slave description file or by using the etherlab
 * (www.etherlab.org) ethercat tool.
 *
 * @return 0 if success other wise an error code.
 *
 * @note Example: Read maximum current setting of the EL7037 stepper drive card
 * on slave position 2.
 * "Cfg.EcReadSdo(2,0x8010,0x1,2)" //Command string to ecmcCmdParser.c
 */
int ecReadSdo(uint16_t  slavePosition,
              uint16_t  sdoIndex,
              uint8_t   sdoSubIndex,
              int       byteSize,
              uint32_t *value);

/** @brief Verify a Service Data Object.
 *
 * Read will occur directly when the command is issued.
 *
 * @note This command can only be used in configuration mode.
 *
 *  @param[in] slaveBusPosition Position of the EtherCAT slave on the bus.
 *    slaveBusPosition = 0..65535: Addressing of EtherCAT slaves.
 *  @param[in] sdoIndex Index of service data object. Needs to be
 *                           entered in hex format.
 *  @param[in] sdoSubIndex Sub index of service data object .
 *                           Needs to be entered in hex format.
 *  @param[in] byteSize Byte count to read.
 *  @param[in] verValue      Expected value of SDO.
 *
 * @note All configuration data can be found in the documentation of
 * the slave, in the ESI slave description file or by using the etherlab
 * (www.etherlab.org) ethercat tool.
 *
 * @return 0 if success other wise an error code.
 *
 * @note Example: Verify maximum current setting of the EL7037 stepper drive card
 * on slave position 2 is set to 1000mA.
 * "Cfg.EcVerifySdo(2,0x8010,0x1,1000,2)" //Command string to ecmcCmdParser.c
 */
int ecVerifySdo(uint16_t slavePosition,
                uint16_t sdoIndex,
                uint8_t  sdoSubIndex,
                uint32_t verValue,
                int      byteSize);

/** @brief Read SoE 
 *
 * @note This command can only be used in configuration mode.
 *
 *  @param[in]  slaveBusPosition Position of the EtherCAT slave on the bus.
 *    slaveBusPosition = 0..65535: Addressing of EtherCAT slaves.
 *  @param[in]  driveNo   SoE drive number.
 *  @param[in]  idn       SoE IDN.
 *  @param[in]  byteSize  Byte count to read.
 *  @param[out] value     Pointer to read data.
 *
 * @note All configuration data can be found in the documentation of
 * the slave
 *
 * @return 0 if success other wise an error code.
 *
 * @note Example: Read data for drive 0
 * on slave position 2.
 * "Cfg.EcReadSoE(2,0,0x1000,2)" //Command string to ecmcCmdParser.c
 */
int ecReadSoE(uint16_t slavePosition,  /**< Slave position. */
              uint8_t  driveNo,  /**< Drive number. */
              uint16_t idn,  /**< SoE IDN (see ecrt_slave_config_idn()). */
              size_t   byteSize,  /**< Size of data to write. */
              uint8_t *value  /**< Pointer to data to write. */
              );

/** @brief Write SoE 
 *
 * @note This command can only be used in configuration mode.
 *
 *  @param[in]  slaveBusPosition Position of the EtherCAT slave on the bus.
 *    slaveBusPosition = 0..65535: Addressing of EtherCAT slaves.
 *  @param[in]  driveNo   SoE drive number.
 *  @param[in]  idn       SoE IDN.
 *  @param[in]  byteSize  Byte count to write.
 *  @param[out] value     Pointer to data to write.
 *
 * @note All configuration data can be found in the documentation of
 * the slave
 *
 * @return 0 if success other wise an error code.
 *
 * @note Example: Read data for drive 0
 * on slave position 2.
 * "Cfg.EcWriteSoE(2,0,0x1000,2)" //Command string to ecmcCmdParser.c
 */
int ecWriteSoE(uint16_t slavePosition,  /**< Slave position. */
               uint8_t  driveNo,  /**< Drive number. */
               uint16_t idn,  /**< SoE IDN (see ecrt_slave_config_idn()). */
               size_t   byteSize,  /**< Size of data to write. */
               uint8_t *value  /**< Pointer to data to write. */
               );

/** @brief Configure Slave watch dog.
 *
 *  @param[in] slaveBusPosition Position of the EtherCAT slave on the bus.
 *                              Note: the slave needs to be equipped
 *                              with a DC.
 *    slaveBusPosition = 0..65535: Addressing of EtherCAT slaves.
 *  @param[in] watchdogDivider  Number of 40 ns intervals. Used as a
 *                              base unit for all slave watchdogs. If set
 *                              to zero, the value is not written, so the
 *                              default is used.
 *  @param[in] watchdogIntervals Number of base intervals for process
 *                              data watchdog. If set to zero, the value
 *                              is not written, so the default is used.
 *
 * @note Example: Set watchdog times to 100,100 for slave at busposition 1.
 * "Cfg.EcSlaveConfigWatchDog(1,100,100)" //Command string to ecmcCmdParser.c
 *
 * @return 0 if success or otherwise an error code.
 */
int ecSlaveConfigWatchDog(int slaveBusPosition,
                          int watchdogDivider,
                          int watchdogIntervals);

/** @brief Apply hardware configuration to master.
 *
 * This command needs to be executed before entering runtime.
 *
 * @note This command can only be used in configuration mode.
 *
 *  @param[in] masterIndex Index of master, see command ecSetMaster().
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: Apply hardware configuration to master 0 (/dev/EtherCAT0).
 * "Cfg.EcApplyConfig(0)" //Command string to ecmcCmdParser.c
 */
int ecApplyConfig(int masterIndex);

/** @brief Writes a value to an EtherCAT entry.
  *
  *  @param[in] slaveIndex Index of order of added slave (not bus position),
  *                        see command EcAddSlave()).
  *  @param[in] entryIndex Index of order of added entry (within slave).
  *  @param[in] value Value to be written.
  *
  * @note the slaveIndex and entryIndex can be read with the commands
  * readEcSlaveIndex() and readEcEntryIndexIDString().
  *
  * @return 0 if success or otherwise an error code.
  *
  * @note Example: Write a 0 to the 2:nd added entry (entryIndex=1) in the 4:th
  * added slave (slaveIndex=3).
  *  "WriteEcEntry(3,1,0)" //Command string to ecmcCmdParser.c
  */
int writeEcEntry(int      slaveIndex,
                 int      entryIndex,
                 uint64_t value);

/** @brief Writes a value to an EtherCAT entry addressed by slaveBusPosition
 * and entryIdString.
  *
  *  @param[in] slaveBusPosition Position of the EtherCAT slave on the bus.
  *    slaveBusPosition = -1: Used to address the simulation slave. Only two
  *                           entries are configured, "ZERO" with default
  *                           value 0 and "ONE" with default value 1.
  *    slaveBusPosition = 0..65535: Addressing of normal EtherCAT slaves.
  *  @param[in] entryIdString String for addressing purpose (see command
  *                      "Cfg.EcAddEntryComplete() for more information").
  *  @param[in] value Value to be written.
  *
  * Note: This command should not be used when realtime performance is needed
  * (also see "WriteECEntry()" command).
  *
  * @return 0 if success or otherwise an error code.
  *
  * @note Example: Write a 1 to a digital output configured as "OUTPUT_0" on slave 1
  *  "Cfg.WriteEcEntryIDString(1,OUTPUT_1,1)" //Command string to ecmcCmdParser.c
  */
int writeEcEntryIDString(int      slaveBusPosition,
                         char    *entryIdString,
                         uint64_t value);

/** @brief Writes a value to an EtherCAT entry addressed by an ethercat path
 * .
  *
  *  @param[in] ecPath Path of the ethercat entry (ec<mid>.s<sid>.<entry name>).
  *  @param[in] value Value to be written.
  *
  * Note: This command should not be used when realtime performance is needed
  * (also see "WriteECEntry()" command).
  *
  * @return 0 if success or otherwise an error code.
  *
  * @note Example: Write a 1 to a digital output configured as "OUTPUT_0" on slave 1
  *  "Cfg.WriteEcEntryEcPath(ec0.s1.OUTPUT_1,1)" //Command string to ecmcCmdParser.c
  */
int writeEcEntryEcPath(char *ecPath,
                       uint64_t value);

/** @brief Read a value from an EtherCAT entry.
  *
  *  @param[in] slaveIndex Index of order of added slave (not bus position),
  *                        see command EcAddSlave()).
  *  @param[in] entryIndex Index of order of added entry (within slave).
  *  @param[out] value Read value (result).
  *
  * @note the slaveIndex and entryIndex can be read with the commands
  * readEcSlaveIndex() and readEcEntryIndexIDString().
  *
  * @return 0 if success or otherwise an error code.
  *
  * @note Example: Read the 2:nd added entry (entryIndex=1) in the 4:th added
  * slave (slaveIndex=3).
  * "ReadEcEntry(3,1)" //Command string to ecmcCmdParser.c
  */
int readEcEntry(int       slaveIndex,
                int       entryIndex,
                uint64_t *value);

/** @brief Read a value from an EtherCAT entry addressed by slaveBusPosition
 *   and entryIdString.
  *
  *  @param[in] slaveBusPosition Position of the EtherCAT slave on the bus.
  *    slaveBusPosition = -1: Used to address the simulation slave. Only two
  *                           entries are configured, "ZERO" with default
  *                           value 0 and "ONE" with default value 1.
  *    slaveBusPosition = 0..65535: Addressing of normal EtherCAT slaves.
  *  @param[in] entryIdString String for addressing purpose (see command
  *                      "Cfg.EcAddEntryComplete() for more information").
  *  @param[out] value Read value (result).
  *
  * Note: This command should not be used when realtime performance is needed
  * (also see "WriteECEntry()" command).
  *
  * @return 0 if success or otherwise an error code.
  *
  * @note Example: Read a digital input configured as "INPUT_0" on slave 1
  *  "ReadEcEntryIDString(1,INPUT_0)" //Command string to ecmcCmdParser.c
  */
int readEcEntryIDString(int       slavePosition,
                        char     *entryIDString,
                        uint64_t *value);

/** @brief Read the object Index of an entry addressed by slaveBusPosition
 *   and entryIdString.
  *
  * The first entry added in a slave will receive index 0, then for each added
  * entry, its index will be incremented by one. This index will be returned by
  * this function. This index is needed to address a certain slave with the
  * commands readEcEntry() and writeEcEntry() (for use in realtime, since
  * these functions address the slave and entry arrays directly via indices).
  *
  * @note This is not the same as the entryIndex in EcAddEntryComplete().
  *
  * \todo Change confusing naming of this function. entryIndex and index of entry is
  * easily confused.
  *
  *  @param[in] slaveBusPosition Position of the EtherCAT slave on the bus.
  *    slaveBusPosition = -1: Used to address the simulation slave. Only two
  *                           entries are configured, "ZERO" with default
  *                           value 0 and "ONE" with default value 1.
  *    slaveBusPosition = 0..65535: Addressing of normal EtherCAT slaves.
  *  @param[in] entryIdString String for addressing purpose (see command
  *                      "Cfg.EcAddEntryComplete() for more information").
  *  @param[out] value Entry index (the order is was added).
  *
  * @return 0 if success or otherwise an error code.
  *
  * @note Example: Read a the index of an entry configured as "INPUT_0" on slave 1
  *  "ReadEcEntryIndexIDString(1,INPUT_0)" //Command string to ecmcCmdParser.c
  */
int readEcEntryIndexIDString(int   slavePosition,
                             char *entryIDString,
                             int  *value);

/** @brief Read the object Index of an slave addressed by slaveBusPosition.
  *
  * The first slave added in will receive index 0, then for each added
  * slave, its index will be incremented by one. This index will be returned by
  * this function. This index is needed to address a certain slave with the
  * commands readEcEntry() and writeEcEntry() (for use in realtime, since
  * these functions address the slave and entry arrays directly via indices).
  *
  * @note slaveIndex and slaveBusPosition is not the same.
  *
  *  @param[in] slaveBusPosition Position of the EtherCAT slave on the bus.
  *    slaveBusPosition = -1: Used to address the simulation slave. Only two
  *                           entries are configured, "ZERO" with default
  *                           value 0 and "ONE" with default value 1.
  *    slaveBusPosition = 0..65535: Addressing of normal EtherCAT slaves.
  *
  * @return 0 if success or otherwise an error code.
  *
  * @note Example: Read the slave index of the slave with bus position 5.
  *  "ReadEcSlaveIndex(5)" //Command string to ecmcCmdParser.c
  */
int readEcSlaveIndex(int  slavePosition,
                     int *value);


/** @brief Read EtherCAT memory map object.
 *
 * Fast access of EtherCAT data from EPICS records is possible by linking an
 * EtherCAT memory map to an ASYN parameter. The memory map objects is most
 * usefull for acquire arrays of data (waveforms).This function is called by
 * the iocsh command"ecmcAsynPortDriverAddParameter()". For more information
 * see documentation of ecmcAsynPortDriverAddParameter(), ecAddMemMap(),
 * readEcMemMap() and ecSetEntryUpdateInRealtime().
 *
 *  @param[in] memMapIDString String for addressing ethercat entry:
 *             ec.mm.<memory map id>
 *  @param[out] *data Output data buffer.*
 *  @param[in]  bytesToRead Output data buffer size.*
 *  @param[out] bytesRead Bytes read.*
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: memMapIDString for an memory map called "AI_1_ARRAY":
 * "ec.mm.AI_1_ARRAY".
 * @note There's no ascii command in ecmcCmdParser.c for this method.
 */
int readEcMemMap(const char *memMapIDString,
                 uint8_t    *data,
                 size_t      bytesToRead,
                 size_t     *bytesRead);

/** @brief Set update in realtime bit for an entry
 *
 * If set to zero the entry will not be updated during realtime operation.
 * Useful when accessing data with memory maps instead covering many entries
 * like oversampling arrays (it's then unnecessary to update each entry in
 * array).
 *
 *  @param[in] slavePosition Position of the EtherCAT slave on the bus.
 *    slavePosition = -1: Used to address the simulation slave. Only two
 *                           entries are configured, "ZERO" with default
 *                           value 0 and "ONE" with default value 1.
 *    slaveBusPosition = 0..65535: Addressing of normal EtherCAT slaves.
 *  @param[in] entryIdString String for addressing purpose (see command
 *                      "Cfg.EcAddEntryComplete() for more information").
 *  @param[in] updateInRealtime 1 for update of entry data in realtime and
 *                      0 not to update data in realtime.
 * @note Example: Disable update of value in realtime for entry with name "AI_1" on
 * bus position 5.
 *  "Cfg.EcSetEntryUpdateInRealtime(5,AI_1,0)" //Command string to ecmcCmdParser.c
 */
int ecSetEntryUpdateInRealtime(
  uint16_t slavePosition,
  char    *entryIDString,
  int      updateInRealtime);

/** @brief Enable EtherCAT bus diagnostics.
  *
  * Diagnostics are made at three different levels:
  * 1. Slave level.
  * 2. Domain level.
  * 3. Master level.
  *
  * All three levels of diagnostics are enabled or disabled with this command.
  * If the diagnostics are enabled the motion axes are interlocked if any of
  * the above levels report issues. If the diagnostics are disabled the motion
  * axes are not interlocked even if there's alarms on the EtherCAT bus.
  *
  * @param[in] enable Enable diagnostics.
  * @return 0 if success or otherwise an error code.
  *
  * @note Example: Enable EtherCAT diagnostics.
  *  "Cfg.EcSetDiagnostics(1)" //Command string to ecmcCmdParser.c
  */
int ecSetDiagnostics(int enable);

/** @brief Set allowed bus cycles in row of none complete domain
 * data transfer.
 *
 * Allows a certain number of bus cycles of non-complete domain data
 * transfer before alarming.
 *
 *  @note Normally the application is correct configured all domain data
 *  transfers should be complete. This command should therefor only be
 *  used for testing purpose when the final configuration is not yet made.
  *
  * @param[in] cycles Number of cycles.
  *
  * @return 0 if success or otherwise an error code.
  *
  * @note Example: Set the allowd bus cycles of non complete domain data to
  * 10.
  *  "Cfg.EcSetDomainFailedCyclesLimit(10)" //Command string to ecmcCmdParser.c
  */
int ecSetDomainFailedCyclesLimit(int cycles);

/** @brief Reset error on all EtherCat objects.
 *
 * Resets error on the following object types:
 * 1. ecmcEc().
 * 2. ecmcEcSlave().
 * 3. Sync manager is handled internally in ecmcEcSlave.
 * 4. PDO handling is internal in ecmcEcSlave.
 * 5. ecmcEcSDO().
 * 6. ecmcEcEntry().
 *
  * @return 0 if success or otherwise an error code.
  *
  * @note Example: Reset EtherCAT errors.
  *  "Cfg.EcResetError()" //Command string to ecmcCmdParser.c
  */
int ecResetError();

/** @brief Enable diagnostic printouts from EtherCAT objects.
 *
 * Enables/Disables diagnostic printouts from:
 * 1. ecmcEc().
 * 2. ecmcEcSlave().
 * 3. ecmcEcSyncManager().
 * 4. PDO handling is internal in ecmcEcSlave.
 * 5. ecmcEcSDO().
 * 6. ecmcEcEntry().
 *
 * @param[in] enable Enable diagnostic printouts.
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: Enable EtherCAT related diagnostic printouts.
 *  "Cfg.EcEnablePrintouts(1)" //Command string to ecmcCmdParser.c
 */
int ecEnablePrintouts(int value);

/** @brief Delay ethercat OK status for a time
 *
 * This can be usefull to allow extra time foir DC clocks to syncronize or
 * for slave that do not report correct data directlly when enter OP.
 *
 *
 * @param[in] milliseconds Delay time for ecOK status at startup (after slaves in OP).
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: Delay ethercat OK for 2000ms at startup
 *  "Cfg.ecSetDelayECOkAtStartup(2000)" //Command string to ecmcCmdParser.c
 */
int ecSetDelayECOkAtStartup(int milliseconds);

/** @brief Prints all hardware connected to selected master.
 *
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example:
 *  "EcPrintAllHardware()" //Command string to ecmcCmdParser.c
 */
int ecPrintAllHardware();

/** @brief Prints hardware configuration for a selected slave.
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example:Print hardware configuration for slave 1
 *  "EcPrintSlaveConfig(1)" //Command string to ecmcCmdParser.c
 */
int ecPrintSlaveConfig(int slaveIndex);

/** @brief Links an EtherCAT entry to the ethecat master object for hardware
 *   status output
 *
 *  The output will be high when the EtherCAT master is without error code and
 *  otherwise zero.
 *
 *  @param[in] slaveIndex Position of the EtherCAT slave on the bus.
 *    slaveIndex = -1: Used to address the simulation slave. Only two
 *                     entries are configured, "ZERO" with default
 *                     value 0 and "ONE" with default value 1.
 *    slaveIndex = 0..65535: Addressing of normal EtherCAT slaves.
 *  @param[in] entryIdString String for addressing purpose (see command
 *                      "Cfg.EcAddEntryComplete() for more information").
 *
 * @return 0 if success or otherwise an error code.
 *
 *  @note Example 1: Link an EtherCAT entry configured as "OUTPUT_0" in slave 1 as
 *  status output for ethercat master.
 *   "Cfg.LinkEcEntryToEcStatusOutput(1,"OUTPUT_0")" //Command string to ecmcCmdParser.c
 */
int linkEcEntryToEcStatusOutput(int   slaveIndex,
                                char *entryIDString);

/** @brief Verfy slave at position
 *
 *  The command verifys that the actual slave at a certain position\
 *  have the correct alias, position, vendor id, product code and revision number.
 *  Check of slave revsion number will be skipped if set to 0.
 *
 *  @param[in] alias Alias of slave. Set to zero to disable.
 *  @param[in] slaveIn Position of the EtherCAT slave on the bus.
 *    slaveIndex = 0..65535: Addressing of normal EtherCAT slaves.
 *  @param[in] vendorId Identification value for slave vendor.
 *    vendorId = 0x2: Beckhoff.
 *    vendorId = 0x48554B: Kendrion Kuhnke Automation GmbH.
 *  @param productCode Product identification code.
 *    productCode=0x13ed3052: EL5101 incremental encoder input.
 *  @param revisionNum Product revision number. The revsion number of the
 *    actual slave needs to equal or newer than that of the configuration.
 *    if revisionNum==0 then this function will not check revsionNum of 
*     the slave.
 *    revisionNum=0x04000000: EL5101 incremental encoder input.
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: Verify that slave 3 is an EL5101 with a revsion >= 0x04000000
 *   "Cfg.EcVerifySlave(0,3,0x2,0x13ed3052,0x04000000)" //Command string to ecmcCmdParser.c
 */
int ecVerifySlave(uint16_t alias,  /**< Slave alias. */
                  uint16_t slavePos,   /**< Slave position. */
                  uint32_t vendorId,   /**< Expected vendor ID. */
                  uint32_t productCode,  /**< Expected product code. */
                  uint32_t revisionNum /**< Revision number*/);

/** @brief Read vendor id of selected ethercat slave
 *
 *  @param[in] alias Alias of slave. Set to zero to disable.
 *  @param[in] slaveIn Position of the EtherCAT slave on the bus.
 *    slaveIndex = 0..65535: Addressing of normal EtherCAT slaves.
 *  @param[out] result Vendor id.
 *
 * @return 0 if success or otherwise an error code. *
 *
 * @note Example: Read vendor id of slave 3:
 *   "EcGetSlaveVendorId(0,3)" //Command string to ecmcCmdParser.c
 */
int ecGetSlaveVendorId(uint16_t  alias, /**< Slave alias. */
                       uint16_t  slavePos,  /**< Slave position. */
                       uint32_t *result);

/** @brief Read product code of selected ethercat slave
 *
 *  @param[in] alias Alias of slave. Set to zero to disable.
 *  @param[in] slaveIn Position of the EtherCAT slave on the bus.
 *    slaveIndex = 0..65535: Addressing of normal EtherCAT slaves.
 *  @param[out] result Product code.
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: Product Code of slave 3:
 *   "EcGetSlaveProductCode(0,3)" //Command string to ecmcCmdParser.c
 */
int ecGetSlaveProductCode(uint16_t  alias, /**< Slave alias. */
                          uint16_t  slavePos,  /**< Slave position. */
                          uint32_t *result);

/** @brief revision number id of selected ethercat slave
 *
 *  @param[in] alias Alias of slave. Set to zero to disable.
 *  @param[in] slaveIn Position of the EtherCAT slave on the bus.
 *    slaveIndex = 0..65535: Addressing of normal EtherCAT slaves.
 *  @param[out] result Revision number.
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: Read revision number of slave 3:
 *   "EcGetSlaveRevisionNum(0,3)" //Command string to ecmcCmdParser.c
 */
int ecGetSlaveRevisionNum(uint16_t  alias, /**< Slave alias. */
                          uint16_t  slavePos,  /**< Slave position. */
                          uint32_t *result);

/** @brief Read serial number of selected ethercat slave
 *
 *  @param[in] alias Alias of slave. Set to zero to disable.
 *  @param[in] slaveIn Position of the EtherCAT slave on the bus.
 *    slaveIndex = 0..65535: Addressing of normal EtherCAT slaves.
 *  @param[out] result Serial number.
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: Read serial number of slave 3:
 *   "EcGetSlaveSerialNum(0,3)" //Command string to ecmcCmdParser.c
 */
int ecGetSlaveSerialNum(uint16_t  alias, /**< Slave alias. */
                        uint16_t  slavePos,  /**< Slave position. */
                        uint32_t *result);

/** @brief Set bit that slave needs SDO settings
 *
 *  This slave needs SDO settings i.e. if a drive, then current must be set.
 *  If "Cfg.EcSetSlaveSDOSettingsDone()" has not been set at validation of
 *  the slave then the ioc will not start.
 *  General workflow:
 *  1. ecmccfg.addSlave.cmd hw snippet calls "Cfg.EcSetSlaveNeedSDOSettings()" 
 *     if needed for a slave (typically drive slaves)
 *  2. ecmccomp.applyComponent,cmd (or ecmccfg configureSlave.cmd, applySlaveConfig.cmd)
 *     calls "Cfg.EcSetSlaveSDOSettingsDone()" to tell ecmc that cfg have been
 *     performed, for ecmccomp this is performed for each channel (if needed).
 *  3. Vaildation that Cfg.EcSetSlaveSDOSettingsDone() has been executed for
 *     the slaves and channels that previously have been defined by 
 *     "Cfg.EcSetSlaveNeedSDOSettings()". If the SDO settings have not been 
 *     performed then ecmc will exit before going to realtime (before iocInit)
 *
 *  The functionallity can be disabled or enabled by the command
 *  "Cfg.EcSetSlaveEnableSDOCheck()". The check is default enabled for all slaves
 *  that have been linked to a motion axis.
 * 
 *  @param[in] slaveBusPosition Position of the EtherCAT slave on the bus.
 *    slaveIndex = 0..65535: Addressing of normal EtherCAT slaves.
 *  @param[in] channel channel id (starting at 1)
 *  @param[in] need Slave needs SDO setting (default 0)
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: Set that slave 3 need SDO settings:
 *   "Cfg.EcSetSlaveNeedSDOSettings(3,1,1)" //Command string to ecmcCmdParser.c
 */
int ecSetSlaveNeedSDOSettings(int slaveBusPosition, int channel, int need);

/** @brief Set bit that all SDOs has been set for this slave
 *
 *  After SDOs have been set this function should be called to tell ecmc that.
 *  Important SDOs have been set.
*  General workflow:
 *  1. ecmccfg.addSlave.cmd hw snippet calls "Cfg.EcSetSlaveNeedSDOSettings()" 
 *     if needed for a slave (typically drive slaves)
 *  2. ecmccomp.applyComponent,cmd (or ecmccfg configureSlave.cmd, applySlaveConfig.cmd)
 *     calls "Cfg.EcSetSlaveSDOSettingsDone()" to tell ecmc that cfg have been
 *     performed, for ecmccomp this is performed for each channel (if needed).
 *  3. Vaildation that Cfg.EcSetSlaveSDOSettingsDone() has been executed for
 *     the slaves and channels that previously have been defined by 
 *     "Cfg.EcSetSlaveNeedSDOSettings()". If the SDO settings have not been 
 *     performed then ecmc will exit before going to realtime (before iocInit)
 *
 *  The functionallity can be disabled or enabled by the command
 *  "Cfg.EcSetSlaveEnableSDOCheck()". The check is default enabled for all slaves
 *  that have been linked to a motion axis.
 *
 *  @param[in] slaveBusPosition Position of the EtherCAT slave on the bus.
 *    slaveIndex = 0..65535: Addressing of normal EtherCAT slaves.
 *  @param[in] channel channel id (starting at 1)
 *  @param[in] done SDO setting done (default 0)
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: Set that importantt SDO settings for slave 3 has been performed:
 *   "Cfg.EcSetSlaveSDOSettingsDone(3,1,1)" //Command string to ecmcCmdParser.c
 */
int ecSetSlaveSDOSettingsDone(int slaveBusPosition, int channel, int done);

/** @brief Enable SDO setting check for a slave
 *  SDO setting check is defualt true for slaves linked to a motion axis.
 *  For other slaves this command can be used to endforce a check.  
 *  Also see ecSetSlaveNeedSDOSettings and ecSetSlaveSDOSettingsDone.
 *
 *  @param[in] slaveBusPosition Position of the EtherCAT slave on the bus.
 *    slaveIndex = 0..65535: Addressing of normal EtherCAT slaves.
 *  @param[in] enable Enable check (default 0)
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: Enable SDO check for slave 3:
 *   "Cfg.EcSetSlaveEnableSDOCheck(3,1)" //Command string to ecmcCmdParser.c
 */
int ecSetSlaveEnableSDOCheck(int slaveBusPosition, int enable);

/** @brief Use CLOCK_REALTIME
 *
 *  Ecmc will as default use CLOCK_MONOTONIC as clock source both for
 *  the RT loop and the ethercat bus.
 *
 *  @param[in]  useClkRT Select clock
 *    useClkRT = 0: Use CLOCK_MONOTONIC.
 *    useClkRT = 1: Use CLOCK_REALTIME.
 *
 * @return 0 if success or otherwise an error code.
 *
 * @note Example: Use CLOCK_REALTIME:
 *   "Cfg.EcUseClockRealtime(1)" //Command string to ecmcCmdParser.c
 */
int ecUseClockRealtime(int useClkRT);

/** @brief Adds an EtherCAT simulation entry.
 *
 *  @param[in] slaveBusPosition Position of the EtherCAT slave on the bus.
 *    slaveBusPosition = -1: Used to address the simulation slave. Only two
 *                           entries are configured, "ZERO" with default
 *                           value 0 and "ONE" with default value 1.
 *    slaveBusPosition = 0..65535: Addressing of normal EtherCAT slaves.
 *  @param[in] entryIDString Identification string used for addressing the
 *  @param[in] dataType DataType of ethercat data:
 *                      B1:  1-bit
 *                      B2:  2-bits (lsb)
 *                      B3:  3-bits (lsb)
 *                      B4:  4-bits (lsb)
 *                      U8:  Unsigned 8-bit
 *                      S8:  Signed 8-bit
 *                      U16: Unsigned 16-bit
 *                      S16: Signed 16-bit
 *                      U32: Unsigned 32-bit
 *                      S32: Signed 32-bit
 *                      U64: Unsigned 64-bit
 *                      S64: Signed 64-bit
 *                      F32: Real 32-bit
 *                      F64: Double 64-bit
 *
 *
 *  @param[in] updateInRT    value
 *
 * @note Example: Add an EtherCAT simulation entry called "TEST" for slave 7.
 * "Cfg.EcAddSimEntry(7,TEST,U16,0)"
 */
int ecAddSimEntry(
  int position,  char *entryIDString, char *datatype, uint64_t value);

# ifdef __cplusplus
}
# endif  // ifdef __cplusplus

#endif  /* ECMC_ETHERCAT_H_ */
