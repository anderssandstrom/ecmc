#ifndef ECMC_DATA_ACCESS_H_
#define ECMC_DATA_ACCESS_H_

#include <cmath>
#include <cstring>
#include <memory>
#include "ecmcDataItem.h"
#include "ecmcSignalTiming.h"

using ecmcDataType = ecmcEcDataType; // Existing enum values and configuration encodings retained.

// Resolved scalar access. No owned value, driver object, lookup or allocation.
// Source owners guarantee stable storage/metadata and validity until unbound.
// Mutable access is on the cyclic thread or under existing external exclusion.
struct ecmcDataAccess {
  std::shared_ptr<const ecmcSignalTiming> timing;
  void *identity = nullptr;
  uint8_t *data = nullptr;
  const int *validity = nullptr;
  std::shared_ptr<const ecmcDataAccess> originOwner; // Source descriptor invalidation/lifetime.
  std::shared_ptr<const int> validityOwner; // Optional owner-lifetime token; never copied during access.
  ecmcEcDataType type = ECMC_EC_NONE;
  int bits = 0;
  int sourceId = -1000;
  enum Validation { NATIVE_ITEM, DIRECT_STATUS, CUSTOM_STATUS, ORIGIN_STATUS };
  Validation validation = NATIVE_ITEM;
  int (*customValidity)(void *) = nullptr;
  void *validityContext = nullptr;
  int selector = 0; // Provider-local selection for computed values.
  int (*numberReader)(const ecmcDataAccess &, double *) = nullptr;
  bool wordStorage = false; // Existing entries store all values in a 64-bit word.
  bool writable = false;
  bool positiveBoolean = false;
  bool exactUnsigned = false;
  bool finiteRequired = false;
  int (*publish)(void *) = nullptr; // Existing write publication, never transport.
  void *publishContext = nullptr;

  bool valid() const {
    if (!data || (validity && !*validity)) return false;
    return validation == DIRECT_STATUS || validExtended();
  }
  ecmcEcDataType getDataType() const { return positiveBoolean ? ECMC_EC_U8 : exactUnsigned ? ECMC_EC_U64 : type; }
  int getBits() const { return positiveBoolean ? 1 : exactUnsigned ? 64 : bits; }
  int getSourceId() const { return sourceId; } // Legacy metadata, not required for access.
  int isValid() const { return valid(); }

  static size_t sizeOf(ecmcEcDataType type) {
    switch (type) {
      case ECMC_EC_U8: case ECMC_EC_S8: return 1;
      case ECMC_EC_U16: case ECMC_EC_S16: return 2;
      case ECMC_EC_U32: case ECMC_EC_S32: case ECMC_EC_F32: return 4;
      case ECMC_EC_U64: case ECMC_EC_S64: case ECMC_EC_F64: return 8;
      default: return 0;
    }
  }

  static bool bind(ecmcDataItem &item, const int *health, int conversion,
                    ecmcDataAccess *out) {
    const auto *info = item.getDataItemInfo();
    const size_t bytes = info ? sizeOf(info->dataType) : 0;
    if (conversion < 0 || conversion > 2) return false;
    if (!out || !info || !info->dataPointerValid || !info->data || !bytes ||
        info->dataSize != bytes ||
        (info->dataElementSize && info->dataElementSize != bytes) ||
        (info->dataBitCount && info->dataBitCount != bytes * 8) ||
        (info->dataDirection != ECMC_DIR_READ && info->dataDirection != ECMC_DIR_WRITE))
      return false;
    ecmcDataAccess result;
    result.identity = &item; result.data = info->data;
    result.type = info->dataType; result.bits = static_cast<int>(bytes * 8);
    result.validity = health; result.positiveBoolean = conversion == 1;
    result.exactUnsigned = conversion == 2;
    result.finiteRequired = info->dataType == ECMC_EC_F32 || info->dataType == ECMC_EC_F64;
    *out = result;
    return true;
  }

  template <typename T> T load() const {
    T value; std::memcpy(&value, data, sizeof(value)); return value;
  }
  template <typename T> void store(T value) const {
    std::memcpy(data, &value, sizeof(value));
  }
  int readValue(uint64_t *out) const {
    if (!out || !data) return 1;
    if (wordStorage && !positiveBoolean && !exactUnsigned) {
      *out = load<uint64_t>();
      return 0;
    }
    return readExtendedValue(out);
  }
  int readNumber(double *out) const {
    if (!out || !data) return 1;
    if (numberReader) return numberReader(*this, out);
    switch (type) {
      case ECMC_EC_S8: *out = load<int8_t>(); break;
      case ECMC_EC_S16: *out = load<int16_t>(); break;
      case ECMC_EC_S32: *out = load<int32_t>(); break;
      case ECMC_EC_S64: *out = static_cast<double>(load<int64_t>()); break;
      case ECMC_EC_F32: *out = load<float>(); break;
      case ECMC_EC_F64: *out = load<double>(); break;
      case ECMC_EC_U8: *out = wordStorage ? static_cast<double>(load<uint64_t>()) : load<uint8_t>(); break;
      case ECMC_EC_U16: *out = wordStorage ? static_cast<double>(load<uint64_t>()) : load<uint16_t>(); break;
      case ECMC_EC_U32: *out = wordStorage ? static_cast<double>(load<uint64_t>()) : load<uint32_t>(); break;
      case ECMC_EC_U64: *out = static_cast<double>(load<uint64_t>()); break;
      default:
        if (!wordStorage) return 1;
        *out = static_cast<double>(load<uint64_t>());
    }
    return 0;
  }
  int readDouble(double *out) const {
    if (!out) return 1;
    if (!positiveBoolean) return readNumber(out);
    double value;
    if (readNumber(&value) || !std::isfinite(value)) return 1;
    *out = value > 0; return 0;
  }
  int readBit(int bit, uint64_t *out) const {
    uint64_t raw;
    if (bit < 0 || bit >= getBits() || readValue(&raw)) return 1;
    *out = (raw >> bit) & 1; return 0;
  }
  int readBits(int start, int count, uint64_t *out) const {
    if (count <= 0 || start < 0 || start >= 64 || count > 64 || start + count > 64)
      return wordStorage ? 0 : 1; // Preserve existing entry no-op boundaries.
    uint64_t raw;
    if (readValue(&raw)) return 1;
    *out = (raw >> start) & (count == 64 ? ~uint64_t(0) : (uint64_t(1) << count) - 1);
    return 0;
  }
  int writeValue(uint64_t value) const {
    if (!writable || !data) return 1;
    if (!wordStorage) return writeNativeValue(value, true);
    store(value); return publish ? publish(publishContext) : 0;
  }
  int writeDouble(double value) const {
    if (!writable || !data) return 1;
    if (!wordStorage) return writeNativeNumber(value);
    switch (type) {
      case ECMC_EC_S8: store(static_cast<int8_t>(value)); break;
      case ECMC_EC_S16: store(static_cast<int16_t>(value)); break;
      case ECMC_EC_S32: store(static_cast<int32_t>(value)); break;
      case ECMC_EC_S64: store(static_cast<int64_t>(value)); break;
      case ECMC_EC_F32: store(static_cast<float>(value)); break;
      case ECMC_EC_F64: store(value); break;
      default: store(static_cast<uint64_t>(value)); break;
    }
    return publish ? publish(publishContext) : 0;
  }
  int writeBit(int bit, uint64_t value) const {
    if (!writable || !data || bit < 0 || bit >= 64) return 1;
    if (!wordStorage) return writeNativeBits(bit, 1, value ? 1 : 0);
    uint64_t raw = load<uint64_t>(), mask = uint64_t(1) << bit;
    store(value ? raw | mask : raw & ~mask); return 0;
  }
  int writeBits(int start, int count, uint64_t value) const {
    if (!writable || !data) return 1;
    if (!wordStorage) return writeNativeBits(start, count, value);
    if (count <= 0 || start < 0 || start >= 64 || count > 64 || start + count > 64) return 0;
    const uint64_t low = count == 64 ? ~uint64_t(0) : (uint64_t(1) << count) - 1;
    const uint64_t mask = low << start;
    store((load<uint64_t>() & ~mask) | ((value & low) << start)); return 0;
  }
private:
  int writeNativeValue(uint64_t value, bool notify) const;
  int writeNativeNumber(double value) const;
  int writeNativeBits(int start, int count, uint64_t value) const;
  bool validExtended() const;
  int readExtendedValue(uint64_t *out) const;
};
#endif
