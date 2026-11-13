#ifndef ECMC_MOTION_DIAG_SNAPSHOT_H_
#define ECMC_MOTION_DIAG_SNAPSHOT_H_

#include <cstring>
#include <cstdint>
#include <string>
#include <limits>
#include <ostream>
#include <type_traits>
#include <vector>

// Stack-only field paths: no streams, locale access or heap growth during capture.
// An overlong path fails the snapshot rather than silently truncating a key.
class ecmcDiagPath {
public:
  ecmcDiagPath(const char *text = "") { *this << text; }
  ecmcDiagPath& operator<<(const char *text) {
    const size_t length = std::strlen(text);
    if (length >= sizeof(text_) - size_) { valid_ = false; return *this; }
    std::memcpy(text_ + size_, text, length + 1);
    size_ += length;
    return *this;
  }
  ecmcDiagPath& operator<<(const ecmcDiagPath& path) {
    valid_ = valid_ && path.valid_;
    return *this << path.c_str();
  }
  ecmcDiagPath& operator<<(size_t index) {
    char digits[3 * sizeof(index) + 1];
    char *end = digits + sizeof(digits) - 1;
    *end = 0;
    do { *--end = '0' + index % 10; index /= 10; } while (index);
    return *this << end;
  }
  const ecmcDiagPath& str() const { return *this; }
  const char *c_str() const { return text_; }
  size_t size() const { return size_; }
  bool valid() const { return valid_; }
private:
  char text_[512] = {};
  size_t size_ = 0;
  bool valid_ = true;
};

// Owns copies, never references into live motion objects. Allocate/touch storage
// before locking; append only performs bounded bookkeeping and memcpy. If the
// buffer is too small, count the required bytes and retry outside the lock.
class ecmcMotionDiagSnapshot {
public:
  explicit ecmcMotionDiagSnapshot(size_t capacity = 1024 * 1024)
    : bytes_(capacity, 0) {}
  void reset() { used_ = required_ = 0; valid_ = true; }
  bool complete() const { return valid_ && used_ == required_; }
  bool valid() const { return valid_; }
  size_t required() const { return required_; }
  void resize(size_t capacity) { bytes_.resize(capacity, 0); }

  template<typename T, typename std::enable_if<
    std::is_arithmetic<T>::value || std::is_enum<T>::value, int>::type = 0>
  void value(const ecmcDiagPath& name, const T& v) {
    static_assert(std::is_arithmetic<T>::value || std::is_enum<T>::value,
                  "Diagnostic scalars must be owned plain values");
    append(name, &v, sizeof(v), &writeScalar<T>);
  }
  void value(const ecmcDiagPath& name, bool v) {
    append(name, &v, sizeof(v), &writeBool);
  }
  template<typename T, typename std::enable_if<
    std::is_same<T, char *>::value || std::is_same<T, const char *>::value,
    int>::type = 0>
  void value(const ecmcDiagPath& name, const T& v) {
    string(name, v, v ? std::strlen(v) : 0);
  }
  void value(const ecmcDiagPath& name, const std::string& v) {
    // Preserve the original C-string output semantics.
    value(name, v.c_str());
  }
  template<size_t N>
  void value(const ecmcDiagPath& name, const char (&v)[N]) {
    size_t size = 0;
    while (size < N && v[size]) ++size;
    string(name, v, size);
  }
  template<typename T>
  void pointer(const ecmcDiagPath& name, T *v) {
    const uintptr_t address = reinterpret_cast<uintptr_t>(v);
    append(name, &address, sizeof(address), &writePointer);
  }

  bool write(std::ostream& out) const {
    if (!complete()) return false;
    size_t offset = 0;
    while (offset < used_) {
      Record record;
      std::memcpy(&record, bytes_.data() + offset, sizeof(record));
      offset += sizeof(record);
      out.write(bytes_.data() + offset, record.keyBytes);
      out << ": ";
      offset += record.keyBytes;
      record.write(out, bytes_.data() + offset, record.valueBytes);
      offset += record.valueBytes;
      out << '\n';
    }
    return out.good();
  }

private:
  using Write = void (*)(std::ostream&, const char *, size_t);
  struct Record { size_t keyBytes, valueBytes; Write write; };
  std::vector<char> bytes_;
  size_t used_ = 0, required_ = 0;
  bool valid_ = true;

  void append(const ecmcDiagPath& name, const void *data, size_t size, Write write) {
    if (!valid_) return;
    const size_t overhead = sizeof(Record) + name.size();
    const size_t max = std::numeric_limits<size_t>::max();
    if (!name.valid() || size > max - overhead ||
        required_ > max - overhead - size) { valid_ = false; return; }
    const size_t offset = required_;
    required_ += overhead + size;
    if (required_ > bytes_.size()) return;
    Record record = {name.size(), size, write};
    std::memcpy(bytes_.data() + offset, &record, sizeof(record));
    std::memcpy(bytes_.data() + offset + sizeof(record), name.c_str(), name.size());
    if (size) std::memcpy(bytes_.data() + offset + overhead, data, size);
    used_ = required_;
  }
  void string(const ecmcDiagPath& name, const char *v, size_t size) {
    append(name, v, size, &writeString);
  }
  template<typename T>
  static void writeScalar(std::ostream& out, const char *data, size_t) {
    T value;
    std::memcpy(&value, data, sizeof(value));
    out << value;
  }
  static void writeBool(std::ostream& out, const char *data, size_t) {
    bool value;
    std::memcpy(&value, data, sizeof(value));
    out << (value ? "true" : "false");
  }
  static void writePointer(std::ostream& out, const char *data, size_t) {
    uintptr_t value;
    std::memcpy(&value, data, sizeof(value));
    if (!value) out << "null";
    else out << "\"0x" << std::hex << value << std::dec << '"';
  }
  static void writeString(std::ostream& out, const char *data, size_t size) {
    out << '"';
    for (size_t i = 0; i < size; ++i) {
      const char c = data[i];
      if (c == '\\' || c == '"') out << '\\';
      if (c == '\n') out << "\\n";
      else if (c == '\r') out << "\\r";
      else out << c;
    }
    out << '"';
  }
};

#endif
