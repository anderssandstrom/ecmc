/*************************************************************************\
* Copyright (c) 2026 Paul Scherrer Institute
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
*
*  ecmcCppPersist.hpp
*
\*************************************************************************/

#pragma once

#include <fstream>
#include <string>
#include <type_traits>
#include <utility>

namespace ecmcCpp {

template <typename T>
class RetainedValue {
  static_assert(std::is_trivially_copyable<T>::value,
                "RetainedValue requires a trivially copyable type");

 public:
  T Value {};
  bool Loaded {false};
  bool SaveOk {false};
  bool LoadOk {false};

  RetainedValue() = default;
  explicit RetainedValue(std::string path) : path_(std::move(path)) {}

  void setPath(std::string path) { path_ = std::move(path); }
  const std::string& path() const { return path_; }

  bool load() { return load(path_); }

  bool load(const std::string& path) {
    Loaded = false;
    LoadOk = false;
    if (path.empty()) {
      return false;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
      return false;
    }

    T temp {};
    stream.read(reinterpret_cast<char*>(&temp), sizeof(T));
    if (!stream.good() && !stream.eof()) {
      return false;
    }
    if (stream.gcount() != static_cast<std::streamsize>(sizeof(T))) {
      return false;
    }

    Value = temp;
    Loaded = true;
    LoadOk = true;
    path_ = path;
    return true;
  }

  bool save() const { return save(path_); }

  bool save(const std::string& path) const {
    if (path.empty()) {
      return false;
    }

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
      return false;
    }

    stream.write(reinterpret_cast<const char*>(&Value), sizeof(T));
    return stream.good();
  }

  bool store() {
    SaveOk = save(path_);
    return SaveOk;
  }

  bool restore() {
    const bool ok = load(path_);
    LoadOk = ok;
    return ok;
  }

 private:
  std::string path_;
};

}  // namespace ecmcCpp
