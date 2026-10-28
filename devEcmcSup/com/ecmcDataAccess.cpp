#include "ecmcDataAccess.h"

// Extended/native-source checks are separate from the short entry hot path.
bool ecmcDataAccess::validExtended() const {
    // Transformed provider views check lifetime before asking the original
    // descriptor for health; copied raw domain pointers may already be gone.
    if (originOwner && (!originOwner->data ||
        (validation == ORIGIN_STATUS && !originOwner->valid()))) return false;
    if (validation == CUSTOM_STATUS) return customValidity && customValidity(validityContext);
    if (exactUnsigned && !finiteRequired) {
      double value; // Only a sign check; integer reads below retain all 64 bits.
      if (readNumber(&value) || value < 0) return false;
    }
    if (!finiteRequired) return true;
    double value;
    if (readNumber(&value)) return false;
    if (!std::isfinite(value)) return false;
    return !exactUnsigned || (value >= 0 && value < 18446744073709551616.0 && std::trunc(value) == value);
  }

int ecmcDataAccess::readExtendedValue(uint64_t *out) const {
    if (!out || !data) return 1;
    if (positiveBoolean) {
      double value;
      if (readNumber(&value) || !std::isfinite(value)) return 1;
      *out = value > 0; return 0;
    }
    if (exactUnsigned && (type == ECMC_EC_F32 || type == ECMC_EC_F64)) {
      double value;
      if (readNumber(&value) || !std::isfinite(value) || value < 0 ||
          value >= 18446744073709551616.0 || std::trunc(value) != value) return 1;
      *out = static_cast<uint64_t>(value); return 0;
    }
    if (numberReader) return 1; // Computed numeric sources require explicit integer conversion.
    if (exactUnsigned && !finiteRequired) {
      double value;
      if (readNumber(&value) || value < 0) return 1;
    }
    if (wordStorage) { *out = load<uint64_t>(); return 0; }
    switch (type) {
      case ECMC_EC_U8: *out = load<uint8_t>(); break;
      case ECMC_EC_S8: *out = static_cast<uint64_t>(load<int8_t>()); break;
      case ECMC_EC_U16: *out = load<uint16_t>(); break;
      case ECMC_EC_S16: *out = static_cast<uint64_t>(load<int16_t>()); break;
      case ECMC_EC_U32: *out = load<uint32_t>(); break;
      case ECMC_EC_S32: *out = static_cast<uint64_t>(load<int32_t>()); break;
      case ECMC_EC_U64: *out = load<uint64_t>(); break;
      case ECMC_EC_S64: *out = static_cast<uint64_t>(load<int64_t>()); break;
      default: return 1; // Floating values require numeric access or explicit bool conversion.
    }
    return 0;
  }
