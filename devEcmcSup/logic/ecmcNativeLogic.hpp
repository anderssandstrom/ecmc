/*************************************************************************\
* Copyright (c) 2026 Paul Scherrer Institute
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcNativeLogic.hpp
*
\*************************************************************************/

#pragma once

#include "ecmcNativeLogic.h"

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace ecmcNative {

inline const ecmcNativeLogicHostServices* g_hostServices = nullptr;

inline void setHostServices(const ecmcNativeLogicHostServices* services) {
  g_hostServices = services;
}

inline const ecmcNativeLogicHostServices* hostServices() {
  return g_hostServices;
}

template <typename T>
struct ValueType;

template <>
struct ValueType<bool> {
  static constexpr uint32_t value = ECMC_NATIVE_TYPE_BOOL;
};
template <>
struct ValueType<int8_t> {
  static constexpr uint32_t value = ECMC_NATIVE_TYPE_S8;
};
template <>
struct ValueType<uint8_t> {
  static constexpr uint32_t value = ECMC_NATIVE_TYPE_U8;
};
template <>
struct ValueType<int16_t> {
  static constexpr uint32_t value = ECMC_NATIVE_TYPE_S16;
};
template <>
struct ValueType<uint16_t> {
  static constexpr uint32_t value = ECMC_NATIVE_TYPE_U16;
};
template <>
struct ValueType<int32_t> {
  static constexpr uint32_t value = ECMC_NATIVE_TYPE_S32;
};
template <>
struct ValueType<uint32_t> {
  static constexpr uint32_t value = ECMC_NATIVE_TYPE_U32;
};
template <>
struct ValueType<float> {
  static constexpr uint32_t value = ECMC_NATIVE_TYPE_F32;
};
template <>
struct ValueType<double> {
  static constexpr uint32_t value = ECMC_NATIVE_TYPE_F64;
};

template <typename T>
inline ecmcNativeLogicItemBinding ecmcInput(const char* item_name, T* value) {
  using RawT = std::remove_cv_t<T>;
  return {item_name, value, ValueType<RawT>::value, static_cast<uint32_t>(sizeof(RawT)), 0u};
}

template <typename T>
inline ecmcNativeLogicItemBinding ecmcOutput(const char* item_name, T* value) {
  using RawT = std::remove_cv_t<T>;
  return {item_name, value, ValueType<RawT>::value, static_cast<uint32_t>(sizeof(RawT)), 1u};
}

template <typename T>
inline ecmcNativeLogicExportedVar epicsReadOnly(const char* name, T* value) {
  using RawT = std::remove_cv_t<T>;
  return {name, value, ValueType<RawT>::value, 0u};
}

template <typename T>
inline ecmcNativeLogicExportedVar epicsWritable(const char* name, T* value) {
  using RawT = std::remove_cv_t<T>;
  return {name, value, ValueType<RawT>::value, 1u};
}

class EcmcItems {
 public:
  template <typename T>
  EcmcItems& input(const char* item_name, T& value) {
    bindings_.push_back(ecmcInput(item_name, &value));
    return *this;
  }

  template <typename T>
  EcmcItems& output(const char* item_name, T& value) {
    bindings_.push_back(ecmcOutput(item_name, &value));
    return *this;
  }

  const ecmcNativeLogicItemBinding* data() const {
    return bindings_.data();
  }

  uint32_t count() const {
    return static_cast<uint32_t>(bindings_.size());
  }

 private:
  std::vector<ecmcNativeLogicItemBinding> bindings_;
};

class EpicsExports {
 public:
  template <typename T>
  EpicsExports& readOnly(const char* name, T& value) {
    exports_.push_back(epicsReadOnly(name, &value));
    return *this;
  }

  template <typename T>
  EpicsExports& writable(const char* name, T& value) {
    exports_.push_back(epicsWritable(name, &value));
    return *this;
  }

  const ecmcNativeLogicExportedVar* data() const {
    return exports_.data();
  }

  uint32_t count() const {
    return static_cast<uint32_t>(exports_.size());
  }

 private:
  std::vector<ecmcNativeLogicExportedVar> exports_;
};

class LogicBase {
 public:
  virtual ~LogicBase() = default;
  virtual void run() = 0;

  EcmcItems ecmc;
  EpicsExports epics;
  EcmcItems& io {ecmc};
  EpicsExports& pv {epics};
};

inline double getCycleTimeS() {
  return (g_hostServices && g_hostServices->get_cycle_time_s)
           ? g_hostServices->get_cycle_time_s()
           : 0.0;
}

inline uint32_t getEcMasterStateWord(int32_t master_index = -1) {
  return (g_hostServices && g_hostServices->get_ec_master_state_word)
           ? g_hostServices->get_ec_master_state_word(master_index)
           : 0u;
}

inline uint32_t getEcSlaveStateWord(int32_t slave_index, int32_t master_index = -1) {
  return (g_hostServices && g_hostServices->get_ec_slave_state_word)
           ? g_hostServices->get_ec_slave_state_word(master_index, slave_index)
           : 0u;
}

inline int32_t axisUseInternalTraj(int32_t axis_index) {
  return (g_hostServices && g_hostServices->set_axis_traj_source)
           ? g_hostServices->set_axis_traj_source(axis_index, 0)
           : -1;
}

inline int32_t axisUseExternalTraj(int32_t axis_index) {
  return (g_hostServices && g_hostServices->set_axis_traj_source)
           ? g_hostServices->set_axis_traj_source(axis_index, 1)
           : -1;
}

inline int32_t axisUseInternalEnc(int32_t axis_index) {
  return (g_hostServices && g_hostServices->set_axis_enc_source)
           ? g_hostServices->set_axis_enc_source(axis_index, 0)
           : -1;
}

inline int32_t axisUseExternalEnc(int32_t axis_index) {
  return (g_hostServices && g_hostServices->set_axis_enc_source)
           ? g_hostServices->set_axis_enc_source(axis_index, 1)
           : -1;
}

inline double axisGetActualPos(int32_t axis_index) {
  return (g_hostServices && g_hostServices->get_axis_actual_pos)
           ? g_hostServices->get_axis_actual_pos(axis_index)
           : 0.0;
}

inline double axisGetSetpointPos(int32_t axis_index) {
  return (g_hostServices && g_hostServices->get_axis_setpoint_pos)
           ? g_hostServices->get_axis_setpoint_pos(axis_index)
           : 0.0;
}

inline double axisGetActualVel(int32_t axis_index) {
  return (g_hostServices && g_hostServices->get_axis_actual_vel)
           ? g_hostServices->get_axis_actual_vel(axis_index)
           : 0.0;
}

inline double axisGetSetpointVel(int32_t axis_index) {
  return (g_hostServices && g_hostServices->get_axis_setpoint_vel)
           ? g_hostServices->get_axis_setpoint_vel(axis_index)
           : 0.0;
}

inline int32_t axisIsEnabled(int32_t axis_index) {
  return (g_hostServices && g_hostServices->get_axis_enabled)
           ? g_hostServices->get_axis_enabled(axis_index)
           : 0;
}

inline int32_t axisIsBusy(int32_t axis_index) {
  return (g_hostServices && g_hostServices->get_axis_busy)
           ? g_hostServices->get_axis_busy(axis_index)
           : 0;
}

inline int32_t axisHasError(int32_t axis_index) {
  return (g_hostServices && g_hostServices->get_axis_error)
           ? g_hostServices->get_axis_error(axis_index)
           : 0;
}

inline int32_t axisGetErrorId(int32_t axis_index) {
  return (g_hostServices && g_hostServices->get_axis_error_id)
           ? g_hostServices->get_axis_error_id(axis_index)
           : 0;
}

inline int32_t axisSetExternalSetpointPos(int32_t axis_index, double value) {
  return (g_hostServices && g_hostServices->set_axis_ext_set_pos)
           ? g_hostServices->set_axis_ext_set_pos(axis_index, value)
           : -1;
}

inline int32_t axisSetExternalEncoderPos(int32_t axis_index, double value) {
  return (g_hostServices && g_hostServices->set_axis_ext_act_pos)
           ? g_hostServices->set_axis_ext_act_pos(axis_index, value)
           : -1;
}

inline int32_t getIocState() {
  return (g_hostServices && g_hostServices->get_ioc_state)
           ? g_hostServices->get_ioc_state()
           : -1;
}

inline void publishDebugText(const char* message) {
  if (g_hostServices && g_hostServices->publish_debug_text) {
    g_hostServices->publish_debug_text(message);
  }
}

inline void publishDebugText(const std::string& message) {
  publishDebugText(message.c_str());
}

namespace detail {

template <typename LogicT>
inline void setHostServicesAdapter(const ecmcNativeLogicHostServices* services) {
  static_assert(std::is_base_of_v<LogicBase, LogicT>,
                "LogicT must derive from ecmcNative::LogicBase");
  ecmcNative::setHostServices(services);
}

template <typename LogicT>
inline void* createInstanceAdapter() {
  static_assert(std::is_base_of_v<LogicBase, LogicT>,
                "LogicT must derive from ecmcNative::LogicBase");
  return new LogicT();
}

template <typename LogicT>
inline void destroyInstanceAdapter(void* instance) {
  delete static_cast<LogicT*>(instance);
}

template <typename LogicT>
inline void runCycleAdapter(void* instance) {
  static_cast<LogicT*>(instance)->run();
}

template <typename LogicT>
inline const ecmcNativeLogicItemBinding* getItemBindingsAdapter(void* instance) {
  return static_cast<LogicT*>(instance)->ecmc.data();
}

template <typename LogicT>
inline uint32_t getItemBindingCountAdapter(void* instance) {
  return static_cast<LogicT*>(instance)->ecmc.count();
}

template <typename LogicT>
inline const ecmcNativeLogicExportedVar* getExportedVarsAdapter(void* instance) {
  return static_cast<LogicT*>(instance)->epics.data();
}

template <typename LogicT>
inline uint32_t getExportedVarCountAdapter(void* instance) {
  return static_cast<LogicT*>(instance)->epics.count();
}

}  // namespace detail

}  // namespace ecmcNative

#define ECMC_NATIVE_LOGIC_REGISTER(LOGIC_TYPE, LOGIC_NAME)                           \
  extern "C" const ecmcNativeLogicApi* ecmc_native_logic_get_api(void) {             \
    static_assert(std::is_base_of_v<ecmcNative::LogicBase, LOGIC_TYPE>,              \
                  #LOGIC_TYPE " must derive from ecmcNative::LogicBase");            \
    static const ecmcNativeLogicApi kApi = {                                         \
      ECMC_NATIVE_LOGIC_ABI_VERSION,                                                  \
      LOGIC_NAME,                                                                     \
      &ecmcNative::detail::setHostServicesAdapter<LOGIC_TYPE>,                        \
      &ecmcNative::detail::createInstanceAdapter<LOGIC_TYPE>,                         \
      &ecmcNative::detail::destroyInstanceAdapter<LOGIC_TYPE>,                        \
      &ecmcNative::detail::runCycleAdapter<LOGIC_TYPE>,                               \
      &ecmcNative::detail::getItemBindingsAdapter<LOGIC_TYPE>,                        \
      &ecmcNative::detail::getItemBindingCountAdapter<LOGIC_TYPE>,                    \
      &ecmcNative::detail::getExportedVarsAdapter<LOGIC_TYPE>,                        \
      &ecmcNative::detail::getExportedVarCountAdapter<LOGIC_TYPE>,                    \
    };                                                                                \
    return &kApi;                                                                     \
  }

#define ECMC_NATIVE_LOGIC_REGISTER_DEFAULT(LOGIC_TYPE) \
  ECMC_NATIVE_LOGIC_REGISTER(LOGIC_TYPE, #LOGIC_TYPE)
