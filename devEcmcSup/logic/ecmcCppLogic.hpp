/*************************************************************************\
* Copyright (c) 2026 Paul Scherrer Institute
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcCppLogic.hpp
*
\*************************************************************************/

#pragma once

#include "ecmcCppLogic.h"

#include <array>
#include <cstdint>
#include <cstddef>
#include <string>
#include <type_traits>
#include <vector>

namespace ecmcCpp {

inline const ecmcCppLogicHostServices* g_hostServices = nullptr;

inline void setHostServices(const ecmcCppLogicHostServices* services) {
  g_hostServices = services;
}

inline const ecmcCppLogicHostServices* hostServices() {
  return g_hostServices;
}

template <typename T>
struct ValueType;

template <typename T, typename = void>
struct HasContiguousDataAndSize : std::false_type {};

template <typename T>
struct HasContiguousDataAndSize<
  T,
  std::void_t<
    decltype(std::declval<T&>().data()),
    decltype(std::declval<T&>().size())>> : std::true_type {};

template <typename T>
inline int prepareAutoVectorBinding(ecmcCppLogicItemBinding* binding, uint32_t sourceBytes) {
  if (!binding || !binding->prepareContext) {
    return -1;
  }
  if ((sourceBytes % sizeof(T)) != 0u) {
    return -1;
  }

  auto* values = static_cast<std::vector<T>*>(binding->prepareContext);
  values->resize(sourceBytes / sizeof(T));
  binding->data = values->data();
  binding->bytes = sourceBytes;
  return 0;
}

template <>
struct ValueType<bool> {
  static constexpr uint32_t value = ECMC_CPP_TYPE_BOOL;
};
template <>
struct ValueType<int8_t> {
  static constexpr uint32_t value = ECMC_CPP_TYPE_S8;
};
template <>
struct ValueType<uint8_t> {
  static constexpr uint32_t value = ECMC_CPP_TYPE_U8;
};
template <>
struct ValueType<int16_t> {
  static constexpr uint32_t value = ECMC_CPP_TYPE_S16;
};
template <>
struct ValueType<uint16_t> {
  static constexpr uint32_t value = ECMC_CPP_TYPE_U16;
};
template <>
struct ValueType<int32_t> {
  static constexpr uint32_t value = ECMC_CPP_TYPE_S32;
};
template <>
struct ValueType<uint32_t> {
  static constexpr uint32_t value = ECMC_CPP_TYPE_U32;
};
template <>
struct ValueType<float> {
  static constexpr uint32_t value = ECMC_CPP_TYPE_F32;
};
template <>
struct ValueType<double> {
  static constexpr uint32_t value = ECMC_CPP_TYPE_F64;
};
template <>
struct ValueType<uint64_t> {
  static constexpr uint32_t value = ECMC_CPP_TYPE_U64;
};
template <>
struct ValueType<int64_t> {
  static constexpr uint32_t value = ECMC_CPP_TYPE_S64;
};

template <typename T>
inline ecmcCppLogicItemBinding ecmcInput(const char* item_name, T* value) {
  using RawT = std::remove_cv_t<T>;
  return {item_name,
          value,
          ValueType<RawT>::value,
          static_cast<uint32_t>(sizeof(RawT)),
          0u,
          ECMC_CPP_BIND_FLAG_NONE,
          nullptr,
          nullptr};
}

template <typename T>
inline ecmcCppLogicItemBinding ecmcOutput(const char* item_name, T* value) {
  using RawT = std::remove_cv_t<T>;
  return {item_name,
          value,
          ValueType<RawT>::value,
          static_cast<uint32_t>(sizeof(RawT)),
          1u,
          ECMC_CPP_BIND_FLAG_NONE,
          nullptr,
          nullptr};
}

template <typename T>
inline ecmcCppLogicExportedVar epicsReadOnly(const char* name, T* value) {
  using RawT = std::remove_cv_t<T>;
  return {name, value, ValueType<RawT>::value, static_cast<uint32_t>(sizeof(RawT)), 0u};
}

template <typename T>
inline ecmcCppLogicExportedVar epicsWritable(const char* name, T* value) {
  using RawT = std::remove_cv_t<T>;
  return {name, value, ValueType<RawT>::value, static_cast<uint32_t>(sizeof(RawT)), 1u};
}

template <typename T>
inline ecmcCppLogicItemBinding ecmcInputArray(const char* item_name,
                                                 T*          values,
                                                 size_t      count) {
  using RawT = std::remove_cv_t<T>;
  return {
    item_name,
    values,
    ValueType<RawT>::value,
    static_cast<uint32_t>(sizeof(RawT) * count),
    0u,
    ECMC_CPP_BIND_FLAG_NONE,
    nullptr,
    nullptr,
  };
}

template <typename T, size_t N>
inline ecmcCppLogicItemBinding ecmcInputArray(const char* item_name, T (&values)[N]) {
  return ecmcInputArray(item_name, values, N);
}

template <typename T, size_t N>
inline ecmcCppLogicItemBinding ecmcInputArray(const char* item_name,
                                                 std::array<T, N>& values) {
  return ecmcInputArray(item_name, values.data(), values.size());
}

template <typename Container,
          typename ValueT = std::remove_pointer_t<decltype(std::declval<Container&>().data())>,
          std::enable_if_t<HasContiguousDataAndSize<Container>::value, int> = 0>
inline ecmcCppLogicItemBinding ecmcInputArray(const char* item_name, Container& values) {
  return ecmcInputArray(item_name, values.data(), values.size());
}

template <typename T>
inline ecmcCppLogicItemBinding ecmcOutputArray(const char* item_name,
                                                  T*          values,
                                                  size_t      count) {
  using RawT = std::remove_cv_t<T>;
  return {
    item_name,
    values,
    ValueType<RawT>::value,
    static_cast<uint32_t>(sizeof(RawT) * count),
    1u,
    ECMC_CPP_BIND_FLAG_NONE,
    nullptr,
    nullptr,
  };
}

template <typename T, size_t N>
inline ecmcCppLogicItemBinding ecmcOutputArray(const char* item_name, T (&values)[N]) {
  return ecmcOutputArray(item_name, values, N);
}

template <typename T, size_t N>
inline ecmcCppLogicItemBinding ecmcOutputArray(const char* item_name,
                                                  std::array<T, N>& values) {
  return ecmcOutputArray(item_name, values.data(), values.size());
}

template <typename Container,
          typename ValueT = std::remove_pointer_t<decltype(std::declval<Container&>().data())>,
          std::enable_if_t<HasContiguousDataAndSize<Container>::value, int> = 0>
inline ecmcCppLogicItemBinding ecmcOutputArray(const char* item_name, Container& values) {
  return ecmcOutputArray(item_name, values.data(), values.size());
}

template <typename T>
inline ecmcCppLogicItemBinding ecmcInputAutoArray(const char* item_name,
                                                     std::vector<T>& values) {
  return {item_name,
          nullptr,
          ValueType<std::remove_cv_t<T>>::value,
          0u,
          0u,
          ECMC_CPP_BIND_FLAG_AUTO_SIZE,
          &prepareAutoVectorBinding<T>,
          &values};
}

template <typename T>
inline ecmcCppLogicItemBinding ecmcOutputAutoArray(const char* item_name,
                                                      std::vector<T>& values) {
  return {item_name,
          nullptr,
          ValueType<std::remove_cv_t<T>>::value,
          0u,
          1u,
          ECMC_CPP_BIND_FLAG_AUTO_SIZE,
          &prepareAutoVectorBinding<T>,
          &values};
}

inline ecmcCppLogicItemBinding ecmcInputBytes(const char* item_name,
                                                 void*       data,
                                                 uint32_t    bytes,
                                                 uint32_t    type = ECMC_CPP_TYPE_U8) {
  return {item_name, data, type, bytes, 0u, ECMC_CPP_BIND_FLAG_NONE, nullptr, nullptr};
}

inline ecmcCppLogicItemBinding ecmcOutputBytes(const char* item_name,
                                                  void*       data,
                                                  uint32_t    bytes,
                                                  uint32_t    type = ECMC_CPP_TYPE_U8) {
  return {item_name, data, type, bytes, 1u, ECMC_CPP_BIND_FLAG_NONE, nullptr, nullptr};
}

template <typename T>
inline ecmcCppLogicExportedVar epicsReadOnlyArray(const char* name,
                                                     T*          values,
                                                     size_t      count) {
  using RawT = std::remove_cv_t<T>;
  return {
    name,
    values,
    ValueType<RawT>::value,
    static_cast<uint32_t>(sizeof(RawT) * count),
    0u,
  };
}

template <typename T, size_t N>
inline ecmcCppLogicExportedVar epicsReadOnlyArray(const char* name, T (&values)[N]) {
  return epicsReadOnlyArray(name, values, N);
}

template <typename T, size_t N>
inline ecmcCppLogicExportedVar epicsReadOnlyArray(const char* name,
                                                     std::array<T, N>& values) {
  return epicsReadOnlyArray(name, values.data(), values.size());
}

template <typename Container,
          typename ValueT = std::remove_pointer_t<decltype(std::declval<Container&>().data())>,
          std::enable_if_t<HasContiguousDataAndSize<Container>::value, int> = 0>
inline ecmcCppLogicExportedVar epicsReadOnlyArray(const char* name, Container& values) {
  return epicsReadOnlyArray(name, values.data(), values.size());
}

template <typename T>
inline ecmcCppLogicExportedVar epicsWritableArray(const char* name,
                                                     T*          values,
                                                     size_t      count) {
  using RawT = std::remove_cv_t<T>;
  return {
    name,
    values,
    ValueType<RawT>::value,
    static_cast<uint32_t>(sizeof(RawT) * count),
    1u,
  };
}

template <typename T, size_t N>
inline ecmcCppLogicExportedVar epicsWritableArray(const char* name, T (&values)[N]) {
  return epicsWritableArray(name, values, N);
}

template <typename T, size_t N>
inline ecmcCppLogicExportedVar epicsWritableArray(const char* name,
                                                     std::array<T, N>& values) {
  return epicsWritableArray(name, values.data(), values.size());
}

template <typename Container,
          typename ValueT = std::remove_pointer_t<decltype(std::declval<Container&>().data())>,
          std::enable_if_t<HasContiguousDataAndSize<Container>::value, int> = 0>
inline ecmcCppLogicExportedVar epicsWritableArray(const char* name, Container& values) {
  return epicsWritableArray(name, values.data(), values.size());
}

inline ecmcCppLogicExportedVar epicsReadOnlyBytes(const char* name,
                                                     void*       data,
                                                     uint32_t    bytes,
                                                     uint32_t    type = ECMC_CPP_TYPE_U8) {
  return {name, data, type, bytes, 0u};
}

inline ecmcCppLogicExportedVar epicsWritableBytes(const char* name,
                                                     void*       data,
                                                     uint32_t    bytes,
                                                     uint32_t    type = ECMC_CPP_TYPE_U8) {
  return {name, data, type, bytes, 1u};
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

  template <typename T>
  EcmcItems& inputArray(const char* item_name, T* values, size_t count) {
    bindings_.push_back(ecmcInputArray(item_name, values, count));
    return *this;
  }

  template <typename T, size_t N>
  EcmcItems& inputArray(const char* item_name, T (&values)[N]) {
    bindings_.push_back(ecmcInputArray(item_name, values));
    return *this;
  }

  template <typename T, size_t N>
  EcmcItems& inputArray(const char* item_name, std::array<T, N>& values) {
    bindings_.push_back(ecmcInputArray(item_name, values));
    return *this;
  }

  template <typename Container,
            std::enable_if_t<HasContiguousDataAndSize<Container>::value, int> = 0>
  EcmcItems& inputArray(const char* item_name, Container& values) {
    bindings_.push_back(ecmcInputArray(item_name, values));
    return *this;
  }

  template <typename T>
  EcmcItems& outputArray(const char* item_name, T* values, size_t count) {
    bindings_.push_back(ecmcOutputArray(item_name, values, count));
    return *this;
  }

  template <typename T, size_t N>
  EcmcItems& outputArray(const char* item_name, T (&values)[N]) {
    bindings_.push_back(ecmcOutputArray(item_name, values));
    return *this;
  }

  template <typename T, size_t N>
  EcmcItems& outputArray(const char* item_name, std::array<T, N>& values) {
    bindings_.push_back(ecmcOutputArray(item_name, values));
    return *this;
  }

  template <typename Container,
            std::enable_if_t<HasContiguousDataAndSize<Container>::value, int> = 0>
  EcmcItems& outputArray(const char* item_name, Container& values) {
    bindings_.push_back(ecmcOutputArray(item_name, values));
    return *this;
  }

  template <typename T>
  EcmcItems& inputAutoArray(const char* item_name, std::vector<T>& values) {
    bindings_.push_back(ecmcInputAutoArray(item_name, values));
    return *this;
  }

  template <typename T>
  EcmcItems& outputAutoArray(const char* item_name, std::vector<T>& values) {
    bindings_.push_back(ecmcOutputAutoArray(item_name, values));
    return *this;
  }

  EcmcItems& inputBytes(const char* item_name,
                        void*       data,
                        uint32_t    bytes,
                        uint32_t    type = ECMC_CPP_TYPE_U8) {
    bindings_.push_back(ecmcInputBytes(item_name, data, bytes, type));
    return *this;
  }

  EcmcItems& outputBytes(const char* item_name,
                         void*       data,
                         uint32_t    bytes,
                         uint32_t    type = ECMC_CPP_TYPE_U8) {
    bindings_.push_back(ecmcOutputBytes(item_name, data, bytes, type));
    return *this;
  }

  const ecmcCppLogicItemBinding* data() const {
    return bindings_.data();
  }

  uint32_t count() const {
    return static_cast<uint32_t>(bindings_.size());
  }

 private:
  std::vector<ecmcCppLogicItemBinding> bindings_;
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

  template <typename T>
  EpicsExports& readOnlyArray(const char* name, T* values, size_t count) {
    exports_.push_back(epicsReadOnlyArray(name, values, count));
    return *this;
  }

  template <typename T, size_t N>
  EpicsExports& readOnlyArray(const char* name, T (&values)[N]) {
    exports_.push_back(epicsReadOnlyArray(name, values));
    return *this;
  }

  template <typename T, size_t N>
  EpicsExports& readOnlyArray(const char* name, std::array<T, N>& values) {
    exports_.push_back(epicsReadOnlyArray(name, values));
    return *this;
  }

  template <typename Container,
            std::enable_if_t<HasContiguousDataAndSize<Container>::value, int> = 0>
  EpicsExports& readOnlyArray(const char* name, Container& values) {
    exports_.push_back(epicsReadOnlyArray(name, values));
    return *this;
  }

  template <typename T>
  EpicsExports& writableArray(const char* name, T* values, size_t count) {
    exports_.push_back(epicsWritableArray(name, values, count));
    return *this;
  }

  template <typename T, size_t N>
  EpicsExports& writableArray(const char* name, T (&values)[N]) {
    exports_.push_back(epicsWritableArray(name, values));
    return *this;
  }

  template <typename T, size_t N>
  EpicsExports& writableArray(const char* name, std::array<T, N>& values) {
    exports_.push_back(epicsWritableArray(name, values));
    return *this;
  }

  template <typename Container,
            std::enable_if_t<HasContiguousDataAndSize<Container>::value, int> = 0>
  EpicsExports& writableArray(const char* name, Container& values) {
    exports_.push_back(epicsWritableArray(name, values));
    return *this;
  }

  EpicsExports& readOnlyBytes(const char* name,
                              void*       data,
                              uint32_t    bytes,
                              uint32_t    type = ECMC_CPP_TYPE_U8) {
    exports_.push_back(epicsReadOnlyBytes(name, data, bytes, type));
    return *this;
  }

  EpicsExports& writableBytes(const char* name,
                              void*       data,
                              uint32_t    bytes,
                              uint32_t    type = ECMC_CPP_TYPE_U8) {
    exports_.push_back(epicsWritableBytes(name, data, bytes, type));
    return *this;
  }

  const ecmcCppLogicExportedVar* data() const {
    return exports_.data();
  }

  uint32_t count() const {
    return static_cast<uint32_t>(exports_.size());
  }

 private:
  std::vector<ecmcCppLogicExportedVar> exports_;
};

class LogicBase {
 public:
  virtual ~LogicBase() = default;
  virtual void run() = 0;

  EcmcItems ecmc;
  EpicsExports epics;
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
inline void setHostServicesAdapter(const ecmcCppLogicHostServices* services) {
  static_assert(std::is_base_of_v<LogicBase, LogicT>,
                "LogicT must derive from ecmcCpp::LogicBase");
  ecmcCpp::setHostServices(services);
}

template <typename LogicT>
inline void* createInstanceAdapter() {
  static_assert(std::is_base_of_v<LogicBase, LogicT>,
                "LogicT must derive from ecmcCpp::LogicBase");
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
inline const ecmcCppLogicItemBinding* getItemBindingsAdapter(void* instance) {
  return static_cast<LogicT*>(instance)->ecmc.data();
}

template <typename LogicT>
inline uint32_t getItemBindingCountAdapter(void* instance) {
  return static_cast<LogicT*>(instance)->ecmc.count();
}

template <typename LogicT>
inline const ecmcCppLogicExportedVar* getExportedVarsAdapter(void* instance) {
  return static_cast<LogicT*>(instance)->epics.data();
}

template <typename LogicT>
inline uint32_t getExportedVarCountAdapter(void* instance) {
  return static_cast<LogicT*>(instance)->epics.count();
}

}  // namespace detail

}  // namespace ecmcCpp

#define ECMC_CPP_LOGIC_REGISTER(LOGIC_TYPE, LOGIC_NAME)                             \
  extern "C" const ecmcCppLogicApi* ecmc_cpp_logic_get_api(void) {                  \
    static_assert(std::is_base_of_v<ecmcCpp::LogicBase, LOGIC_TYPE>,                \
                  #LOGIC_TYPE " must derive from ecmcCpp::LogicBase");              \
    static const ecmcCppLogicApi kApi = {                                           \
      ECMC_CPP_LOGIC_ABI_VERSION,                                                    \
      LOGIC_NAME,                                                                    \
      &ecmcCpp::detail::setHostServicesAdapter<LOGIC_TYPE>,                          \
      &ecmcCpp::detail::createInstanceAdapter<LOGIC_TYPE>,                           \
      &ecmcCpp::detail::destroyInstanceAdapter<LOGIC_TYPE>,                          \
      &ecmcCpp::detail::runCycleAdapter<LOGIC_TYPE>,                                 \
      &ecmcCpp::detail::getItemBindingsAdapter<LOGIC_TYPE>,                          \
      &ecmcCpp::detail::getItemBindingCountAdapter<LOGIC_TYPE>,                      \
      &ecmcCpp::detail::getExportedVarsAdapter<LOGIC_TYPE>,                          \
      &ecmcCpp::detail::getExportedVarCountAdapter<LOGIC_TYPE>,                      \
    };                                                                               \
    return &kApi;                                                                    \
  }

#define ECMC_CPP_LOGIC_REGISTER_DEFAULT(LOGIC_TYPE) \
  ECMC_CPP_LOGIC_REGISTER(LOGIC_TYPE, #LOGIC_TYPE)
