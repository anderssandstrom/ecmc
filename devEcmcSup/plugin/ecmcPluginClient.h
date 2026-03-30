/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcPlugin.h
*
*  Created on: Mar 21, 2020
*      Author: anderssandstrom
*
* This file contains useful functions for use in plugins
*
\*************************************************************************/

#ifndef ECMC_PLUGIN_H_
#define ECMC_PLUGIN_H_

#include <stdint.h>

# ifdef __cplusplus
extern "C" {
# endif  // ifdef __cplusplus

/** \brief Get an ecmcDataItem obj by idStringWP
 *
 *  Allows access to all regsistered ecmcDataItems in ecmc.\n
 *  \param[in] idStringWP Identification string "with path".\n
 *                        examples: ec0.s1.AI_1\n
 *                                  ec0.s5.mm.CH1_ARRAY\n
 *                                  plcs.plc1.static.test\n
 *                                  ax1.enc.actpos\n
 *
 * \return ecmcDataItem (void*) object if success or otherwise NULL.\n
 *
 * \note Object returns a (void*) so that plugins can be compiled without\n
 *  asyn classes if they are not used.\n
 *
 * \note There's no ascii command in ecmcCmdParser.c for this method.\n
 */
void*  getEcmcDataItem(char *idStringWP);

/** \brief Get an ecmcAsynDataItem obj by idStringWP
 *
 *  Allows access to all regsistered ecmcAsynDataItems in ecmc.\n
 *  The ecmcAsynDataItems class is dervied of ecmcDataItem class.\n
 *
 *  \param[in] idStringWP Identification string "with path".\n
 *                        examples: ec0.s1.AI_1\n
 *                                  ec0.s5.mm.CH1_ARRAY\n
 *                                  plcs.plc1.static.test\n
 *                                  ax1.enc.actpos\n
 *
 * \return ecmcAsynDataItem (void*) object if success or otherwise NULL.\n
 *
 * \note Object returns a (void*) so that plugins can be compiled without\n
 *  asyn classes if they are not used.\n
 *
 * \note There's no ascii command in ecmcCmdParser.c for this method.\n
 */
void*  getEcmcAsynDataItem(char *idStringWP);

/** \brief Get ecmc ec master
 *
 * \return ecmcEc (void*) object if success or otherwise NULL.\n
 *
 * \note There's no ascii command in ecmcCmdParser.c for this method.\n
 */
void*  getEcMaster();

/** \brief Get current EtherCAT master index
 *
 * \return EtherCAT master index if success or otherwise -1.\n
 *
 * \note This helper exists so plugins can query the current master index
 *  without depending directly on ecmcEc.h / ecrt.h.\n
 *
 * \note There's no ascii command in ecmcCmdParser.c for this method.\n
 */
int    getEcmcMasterIndex();

/** \brief Get packed EtherCAT slave state for one slave on one master
 *
 * \param[in] masterIndex EtherCAT master index, or -1 for current/default.
 * \param[in] slaveIndex EtherCAT slave index.
 *
 * \return Packed status word:
 *         bit 0 valid
 *         bit 1 online
 *         bit 2 operational
 *         bit 3..6 AL state
 *
 * \note Returns 0 when the slave state cannot be resolved.
 */
uint32_t getEcmcSlaveStateWord(int masterIndex, int slaveIndex);

/** \brief Get packed EtherCAT master state for one master
 *
 * \param[in] masterIndex EtherCAT master index, or -1 for current/default.
 *
 * \return Packed status word:
 *         bit 0 valid
 *         bit 1 link up
 *         bit 2..5 AL state
 *         bit 16..31 slaves responding
 *
 * \note Returns 0 when the master state cannot be resolved.
 */
uint32_t getEcmcMasterStateWord(int masterIndex);

/** \brief Get ecmcAsynPortObject (as void*)
 *
 * \return ecmcAsynPortObject (void*) object if success or otherwise NULL.\n
*
 * \note Object returns a (void*) so that plugins can be compiled without\n
 *  asyn classes if they are not used.\n
 *
 * \note There's no ascii command in ecmcCmdParser.c for this method.\n
 */
void*  getEcmcAsynPortDriver();

/** \brief Get ecmc sample rate [Hz]
 *
 * \return Sample in Hz \n
 *
 * \note There's no ascii command in ecmcCmdParser.c for this method.\n
 */
double getEcmcSampleRate();

/** \brief Get ecmc sample time in [ms]
 *
 * \return Sample in milliseconds \n
 *
 * \note There's no ascii command in ecmcCmdParser.c for this method.\n
 */
double getEcmcSampleTimeMS();

/** \brief Get ecmc IOC state (hook)
 *
 * \return IOC state as int \n
 *
 * \note There's no ascii command in ecmcCmdParser.c for this method.\n
 */
int    getEcmcEpicsIOCState();

# ifdef __cplusplus
}
# endif  // ifdef __cplusplus

#endif  /* ECMC_PLUGIN_H_ */
