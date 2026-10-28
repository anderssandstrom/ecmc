#ifndef ECMC_DATA_READ_BINDING_H_
#define ECMC_DATA_READ_BINDING_H_

#include <cstring>
#include "ecmcDataItem.h"

// Experimental, read-only scalar binding. No IOC registration or RT integration.
// Resolve/validate once; preserve native values without a double intermediate.
namespace ecmcDataBindingPrototype {
template <typename T> struct NativeType;
#define ECMC_BINDING_NATIVE_TYPE(type, datatype) \
  template <> struct NativeType<type> { \
    static constexpr ecmcEcDataType value = datatype; \
  }
ECMC_BINDING_NATIVE_TYPE(uint8_t, ECMC_EC_U8);
ECMC_BINDING_NATIVE_TYPE(int8_t, ECMC_EC_S8);
ECMC_BINDING_NATIVE_TYPE(uint16_t, ECMC_EC_U16);
ECMC_BINDING_NATIVE_TYPE(int16_t, ECMC_EC_S16);
ECMC_BINDING_NATIVE_TYPE(uint32_t, ECMC_EC_U32);
ECMC_BINDING_NATIVE_TYPE(int32_t, ECMC_EC_S32);
ECMC_BINDING_NATIVE_TYPE(uint64_t, ECMC_EC_U64);
ECMC_BINDING_NATIVE_TYPE(int64_t, ECMC_EC_S64);
ECMC_BINDING_NATIVE_TYPE(float, ECMC_EC_F32);
ECMC_BINDING_NATIVE_TYPE(double, ECMC_EC_F64);
#undef ECMC_BINDING_NATIVE_TYPE

template <typename T>
class ReadBinding {
public:
  // Borrowed storage: item data and optional validity flag must remain at stable
  // addresses until the binding is discarded. Metadata/type/size must be fixed.
  // The flag models source health, separately from dataPointerValid. Its owner
  // and reader must use the same cyclic thread or provide external exclusion.
  // Omitting it means no health observation, not proof of healthy I/O.
  bool bind(ecmcDataItem &item, const bool *sourceValid = nullptr) {
    data_ = nullptr;
    sourceValid_ = nullptr;
    const ecmcDataItemInfo *info = item.getDataItemInfo();
    if (!info || !info->dataPointerValid || !info->data ||
        info->dataType != NativeType<T>::value ||
        info->dataSize != sizeof(T) ||
        (info->dataElementSize && info->dataElementSize != sizeof(T)) ||
        (info->dataBitCount && info->dataBitCount != sizeof(T) * 8) ||
        (info->dataDirection != ECMC_DIR_READ &&
         info->dataDirection != ECMC_DIR_WRITE)) {
      return false;
    }
    data_ = info->data;
    sourceValid_ = sourceValid;
    return true;
  }

  // A failed/unhealthy read leaves the destination unchanged. memcpy supports
  // unaligned byte storage and avoids imposing T's aliasing rules on producers.
  // No lookup, allocation, callbacks or backend dispatch during access.
  bool read(T &value) const {
    if (!data_ || (sourceValid_ && !*sourceValid_)) {
      return false;
    }
    std::memcpy(&value, data_, sizeof(T));
    return true;
  }

private:
  const uint8_t *data_ = nullptr;
  const bool *sourceValid_ = nullptr;
};
}  // namespace ecmcDataBindingPrototype
#endif
