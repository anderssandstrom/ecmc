/*************************************************************************\
* Copyright (c) 2026 Paul Scherrer Institute
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcCppLogic.h
*
* Preferred public C/C++ logic interface header.
* Compatibility note: this wraps the additive native-logic ABI.
*
\*************************************************************************/

#ifndef ECMC_CPP_LOGIC_H_
#define ECMC_CPP_LOGIC_H_

#include "ecmcNativeLogic.h"

typedef enum ecmcNativeValueType ecmcCppLogicValueType;
typedef struct ecmcNativeLogicItemBinding ecmcCppLogicItemBinding;
typedef struct ecmcNativeLogicExportedVar ecmcCppLogicExportedVar;
typedef struct ecmcNativeLogicHostServices ecmcCppLogicHostServices;
typedef struct ecmcNativeLogicApi ecmcCppLogicApi;
typedef ecmcNativeLogicPrepareItemBindingFn ecmcCppLogicPrepareItemBindingFn;

#define ECMC_CPP_LOGIC_ABI_VERSION ECMC_NATIVE_LOGIC_ABI_VERSION
#define ECMC_CPP_BIND_FLAG_NONE ECMC_NATIVE_BIND_FLAG_NONE
#define ECMC_CPP_BIND_FLAG_AUTO_SIZE ECMC_NATIVE_BIND_FLAG_AUTO_SIZE

#define ECMC_CPP_TYPE_BOOL ECMC_NATIVE_TYPE_BOOL
#define ECMC_CPP_TYPE_S8 ECMC_NATIVE_TYPE_S8
#define ECMC_CPP_TYPE_U8 ECMC_NATIVE_TYPE_U8
#define ECMC_CPP_TYPE_S16 ECMC_NATIVE_TYPE_S16
#define ECMC_CPP_TYPE_U16 ECMC_NATIVE_TYPE_U16
#define ECMC_CPP_TYPE_S32 ECMC_NATIVE_TYPE_S32
#define ECMC_CPP_TYPE_U32 ECMC_NATIVE_TYPE_U32
#define ECMC_CPP_TYPE_F32 ECMC_NATIVE_TYPE_F32
#define ECMC_CPP_TYPE_F64 ECMC_NATIVE_TYPE_F64
#define ECMC_CPP_TYPE_U64 ECMC_NATIVE_TYPE_U64
#define ECMC_CPP_TYPE_S64 ECMC_NATIVE_TYPE_S64

#endif
