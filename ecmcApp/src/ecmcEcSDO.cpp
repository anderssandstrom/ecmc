/*
 * ecmcEcSDO.cpp
 *
 *  Created on: Dec 15, 2015
 *      Author: anderssandstrom
 */

#include "ecmcEcSDO.h"

ecmcEcSDO::ecmcEcSDO(ec_master_t *master,uint16_t slavePosition,uint16_t sdoIndex,uint8_t sdoSubIndex, int byteSize)
{
  initVars();
  master_=master;
  slavePosition_=slavePosition;
  sdoIndex_=sdoIndex;
  sdoSubIndex_=sdoSubIndex;
  byteSize_=byteSize;
  if(byteSize_>4){
    setErrorID(__FILE__,__FUNCTION__,__LINE__,ERROR_EC_SDO_SIZE_TO_LARGE);
    byteSize_=4;
  }
}

ecmcEcSDO::ecmcEcSDO(ec_master_t *master,uint16_t slavePosition,uint16_t sdoIndex,uint8_t sdoSubIndex, uint32_t value, int byteSize)
{
  LOGINFO5("%s/%s:%d: INFO: Creating SDO object (slave position %d, SDO index 0x%x, SDO sub index 0x%x, size %d, value %d).\n",__FILE__, __FUNCTION__, __LINE__, slavePosition,sdoIndex,sdoSubIndex,byteSize,value);
  initVars();
  master_=master;
  slavePosition_=slavePosition;
  sdoIndex_=sdoIndex;
  sdoSubIndex_=sdoSubIndex;
  byteSize_=byteSize;
  if(byteSize_>4){
    setErrorID(__FILE__,__FUNCTION__,__LINE__,ERROR_EC_SDO_SIZE_TO_LARGE);
    byteSize_=4;
  }
  writeValue_=value;
}

ecmcEcSDO::~ecmcEcSDO()
{
  ;
}

void ecmcEcSDO::initVars()
{
  errorReset();
  master_=NULL;
  slavePosition_=0;
  sdoIndex_=0;
  sdoSubIndex_=0;
  writeValue_=0;
  readValue_=0;
  byteSize_=0;
  memset(&writeBuffer_[0],0,4);
  memset(&readBuffer_[0],0,4);
}

int ecmcEcSDO::write(uint32_t value)
{
  uint32_t abortCode=0;
  if(byteSize_>4){
    setErrorID(__FILE__,__FUNCTION__,__LINE__,ERROR_EC_SDO_SIZE_TO_LARGE);
    byteSize_=4;
  }
  writeValue_=value;
  memset(&writeBuffer_[0],0,4);
  memcpy(&writeBuffer_[0],&writeValue_,4);//TODO ENDIANS
  int errorCode=ecrt_master_sdo_download(master_,slavePosition_, sdoIndex_, sdoSubIndex_, &writeBuffer_[0], byteSize_, &abortCode); //TODO check abortCode
  if(errorCode){
    LOGERR("%s/%s:%d: ERROR: SDO object 0x%x:%x at slave position %d: Write failed with sdo error code %d, abort code 0x%x (0x%x).\n",__FILE__, __FUNCTION__, __LINE__,sdoIndex_,sdoSubIndex_,slavePosition_,errorCode,abortCode,ERROR_EC_SDO_WRITE_FAILED);
  }
  return errorCode;
}

int ecmcEcSDO::write()
{
  uint32_t abortCode=0;
  memset(&writeBuffer_[0],0,4);
  memcpy(&writeBuffer_[0],&writeValue_,4);//TODO ENDIANS
  int errorCode=ecrt_master_sdo_download(master_,slavePosition_, sdoIndex_, sdoSubIndex_, &writeBuffer_[0], byteSize_, &abortCode);
  if(errorCode){
    LOGERR("%s/%s:%d: ERROR: SDO object 0x%x:%x at slave position %d: Write failed with sdo error code %d, abort code 0x%x (0x%x).\n",__FILE__, __FUNCTION__, __LINE__,sdoIndex_,sdoSubIndex_,slavePosition_,errorCode,abortCode,ERROR_EC_SDO_WRITE_FAILED);
  }
  return errorCode;
}

int ecmcEcSDO::read()
{
  size_t bytes=0;
  uint32_t abortCode=0;
  memset(&readBuffer_[0],0,4);
  int errorCode= ecrt_master_sdo_upload(master_, slavePosition_, sdoIndex_,sdoSubIndex_, &readBuffer_[0],4,&bytes, &abortCode);
  memcpy(&readValue_,&readBuffer_[0],4); //TODO ENDIANS
  if(errorCode){
    LOGERR("%s/%s:%d: ERROR: SDO object 0x%x:%x at slave position %d: Read failed with sdo error code %d, abort code 0x%x (0x%x).\n",__FILE__, __FUNCTION__, __LINE__,sdoIndex_,sdoSubIndex_,slavePosition_,errorCode,abortCode,ERROR_EC_SDO_WRITE_FAILED);
  }
  return errorCode;
}

int ecmcEcSDO::setWriteValue(uint32_t value)
{
  writeValue_=value;
  return 0;
}

uint32_t ecmcEcSDO::getWriteValue()
{
  return writeValue_;
}

uint32_t ecmcEcSDO::getReadValue()
{
  return readValue_;
}

int ecmcEcSDO::writeAndVerify()
{
  if(write()){
    LOGERR("%s/%s:%d: ERROR: SDO object 0x%x:%x at slave position %d: Write failed (0x%x).\n",__FILE__, __FUNCTION__, __LINE__,sdoIndex_,sdoSubIndex_,slavePosition_,ERROR_EC_SDO_WRITE_FAILED);
    return setErrorID(__FILE__,__FUNCTION__,__LINE__,ERROR_EC_SDO_WRITE_FAILED);
  }
  if(read()){
    LOGERR("%s/%s:%d: ERROR: SDO object 0x%x:%x at slave position %d: Read failed (0x%x).\n",__FILE__, __FUNCTION__, __LINE__,sdoIndex_,sdoSubIndex_,slavePosition_,ERROR_EC_SDO_READ_FAILED);
    return setErrorID(__FILE__,__FUNCTION__,__LINE__,ERROR_EC_SDO_READ_FAILED);
  }
  if(writeValue_!=readValue_){
    LOGERR("%s/%s:%d: ERROR: SDO object 0x%x:%x at slave position %d: Verification failed (0x%x).\n",__FILE__, __FUNCTION__, __LINE__,sdoIndex_,sdoSubIndex_,slavePosition_,ERROR_EC_SDO_VERIFY_FAILED);
    return setErrorID(__FILE__,__FUNCTION__,__LINE__,ERROR_EC_SDO_VERIFY_FAILED);
  }
  return 0;
}

uint16_t ecmcEcSDO::getSlaveBusPosition()
{
  return slavePosition_;
}

uint16_t ecmcEcSDO::getSdoIndex()
{
  return sdoIndex_;
}

uint8_t ecmcEcSDO::getSdoSubIndex()
{
  return sdoSubIndex_;
}
