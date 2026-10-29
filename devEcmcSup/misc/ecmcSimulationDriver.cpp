#include "ecmcSimulationDriver.h"
#include "ecmcAsynPortDriver.h"
#include "ecmcErrorsList.h"
#include "ecmcDataErrors.h"
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {
int bitsOf(ecmcEcDataType type) {
  if (type >= ECMC_EC_B1 && type <= ECMC_EC_B4) return static_cast<int>(type);
  if (type >= ECMC_EC_S8_TO_U8 && type <= ECMC_EC_S64_TO_U64) return 8 << (type - ECMC_EC_S8_TO_U8);
  return static_cast<int>(ecmcDataAccess::sizeOf(type) * 8);
}
}

ecmcSimulationDriver::ecmcSimulationDriver(int id, ecmcAsynPortDriver *port)
  : id_(id), port_(port), healthy_(std::make_shared<int>(1)) {
  if (port_) {
    const std::string name = "ec" + std::to_string(id_) + ".simulation.healthy";
    healthParameter_ = port_->addNewAvailParam(name.c_str(), asynParamInt32,
      reinterpret_cast<uint8_t *>(healthy_.get()), sizeof(int), ECMC_EC_S32, 0);
    if (!healthParameter_) { configurationError_ = ERROR_MAIN_ASYN_CREATE_PARAM_FAIL; return; }
    healthParameter_->setAllowWriteToEcmc(true);
    healthParameter_->setEcmcMinValueInt(0);
    healthParameter_->setEcmcMaxValueInt(1);
    healthParameter_->refreshParam(1);
    const std::string prefix = "ec" + std::to_string(id_) + ".";
    registerDiagnostic(prefix + "masterstatus", &masterStatus_);
    registerDiagnostic(prefix + "slavecounter", &slaveCount_);
    registerDiagnostic(prefix + "entrycounter", &entryCount_);
    registerDiagnostic(prefix + "memmapcounter", &memmapCount_);
    registerDiagnostic(prefix + "dom0.domainstatus", &domainStatus_);
    registerDiagnostic(prefix + "dom0.domainfailcountertotal", &failedCycles_);
  }
}

int ecmcSimulationDriver::registerDiagnostic(const std::string &name, uint32_t *value) {
  if (!port_) return 0;
  auto *parameter = port_->addNewAvailParam(name.c_str(), asynParamInt32,
    reinterpret_cast<uint8_t *>(value), sizeof(*value), ECMC_EC_U32, 0);
  if (!parameter) {
    configurationError_ = ERROR_MAIN_ASYN_CREATE_PARAM_FAIL;
    return configurationError_;
  }
  parameter->addSupportedAsynType(asynParamUInt32Digital);
  parameter->setAllowWriteToEcmc(false);
  parameter->refreshParam(1);
  diagnosticParameters_.push_back(parameter);
  return 0;
}

ecmcSimulationDriver::Entry::~Entry() {
  if (access) { access->data = nullptr; access->publish = nullptr; }
  if (parameter) parameter->setEcmcDataPointer(nullptr, 0);
}

ecmcSimulationDriver::~ecmcSimulationDriver() {
  *healthy_ = 0;
  if (healthParameter_) healthParameter_->setEcmcDataPointer(nullptr, 0);
  for (auto *parameter : diagnosticParameters_) parameter->setEcmcDataPointer(nullptr, 0);
}

int ecmcSimulationDriver::addDevice(int alias, int position, uint32_t vendor, uint32_t product) {
  if (position < -1) return ERROR_MAIN_ECMC_LINK_INVALID;
  auto found = devices_.find(position);
  if (found != devices_.end())
    return found->second.alias == alias && found->second.vendor == vendor &&
      found->second.product == product ? 0 : ERROR_MAIN_ECMC_LINK_INVALID;
  Device device;
  device.alias = alias; device.vendor = vendor; device.product = product;
  devices_.emplace(position, std::move(device));
  if (position >= 0) {
    deviceOrder_.push_back(position);
    slaveCount_ = static_cast<uint32_t>(deviceOrder_.size());
    int error = registerDiagnostic("ec" + std::to_string(id_) + ".s" +
      std::to_string(position) + ".slavestatus", &devices_.at(position).status);
    if (error) return error;
  }
  active_ = false;
  int error = addEntry(position, vendor, product, 2, 0, 0, 0, 0, ECMC_EC_U32, "ZERO", true);
  if (error) return error;
  error = addEntry(position, vendor, product, 2, 0, 0, 0, 0, ECMC_EC_U32, "ONE", true);
  return error ? error : setValue(position, "ONE", "0xffffffff");
}

ecmcSimulationDriver::Device *ecmcSimulationDriver::findDevice(int position) {
  auto found = devices_.find(position);
  return found == devices_.end() ? nullptr : &found->second;
}

ecmcSimulationDriver::Entry *ecmcSimulationDriver::findEntry(int position, const char *name) {
  if (!name) return nullptr;
  auto alias = aliases_.find({position, name});
  if (alias != aliases_.end()) return alias->second;
  auto *device = findDevice(position);
  if (!device) return nullptr;
  auto found = device->entries.find(name);
  return found == device->entries.end() ? nullptr : found->second.get();
}

int ecmcSimulationDriver::addEntry(int position, uint32_t vendor, uint32_t product,
  int direction, int syncManager, int pdo, int index, int subIndex,
  ecmcEcDataType type, const char *name, bool realtime) {
  const int bits = bitsOf(type);
  if (!name || !*name || strchr(name, '.') || !bits ||
      (direction != 1 && direction != 2)) return ERROR_EC_ENTRY_DATATYPE_INVALID;
  auto *device = findDevice(position);
  if (!device) {
    int error = addDevice(0, position, vendor, product);
    if (error) return error;
    device = findDevice(position);
  }
  if (device->vendor != vendor || device->product != product || findEntry(position, name))
    return ERROR_MAIN_ECMC_LINK_INVALID;
  auto entry = std::make_unique<Entry>();
  entry->direction = direction; entry->syncManager = syncManager;
  entry->pdo = pdo; entry->index = index; entry->subIndex = subIndex;
  entry->realtime = realtime;
  entry->access = std::make_shared<ecmcDataAccess>();
  auto &access = *entry->access;
  access.identity = entry.get(); access.data = reinterpret_cast<uint8_t *>(&entry->value);
  access.type = type; access.bits = bits;
  access.wordStorage = true; access.writable = true;
  access.finiteRequired = type == ECMC_EC_F32 || type == ECMC_EC_F64;
  access.validation = access.finiteRequired ? ecmcDataAccess::NATIVE_ITEM : ecmcDataAccess::DIRECT_STATUS;
  access.validityOwner = healthy_; access.validity = healthy_.get();
  if (port_ && realtime && (position >= 0 || id_ != 0)) {
    const std::string path = "ec" + std::to_string(id_) + ".s" + std::to_string(position) + "." + name;
    auto *parameter = port_->addNewAvailParam(path.c_str(), asynParamInt32,
      access.data, sizeof(entry->value), type, 0);
    if (!parameter) return ERROR_MAIN_ASYN_CREATE_PARAM_FAIL;
    entry->parameter = parameter;
    parameter->addSupportedAsynType(asynParamFloat64);
    if (type != ECMC_EC_F32 && type != ECMC_EC_F64)
      parameter->addSupportedAsynType(asynParamUInt32Digital);
#ifdef ECMC_ASYN_ASYNPARAMINT64
    if (type == ECMC_EC_U64 || type == ECMC_EC_S64)
      parameter->addSupportedAsynType(asynParamInt64);
#endif
    parameter->setEcmcDataSize((bits + 7) / 8);
    parameter->setEcmcBitCount(bits);
    parameter->setAllowWriteToEcmc(true); // Inputs may be injected by an external model.
    parameter->refreshParam(1);
    access.publishContext = parameter;
    access.publish = [](void *context) {
      static_cast<ecmcAsynDataItem *>(context)->refreshParamRT(0);
      return 0;
    };
  }
  auto *stored = entry.get();
  device->entries.emplace(name, std::move(entry));
  device->entryOrder.push_back(stored);
  if (position >= 0 && strcmp(name, "ZERO") && strcmp(name, "ONE")) ++entryCount_;
  active_ = false;
  return 0;
}

int ecmcSimulationDriver::importEntry(int position, const char *name,
                                      const std::shared_ptr<ecmcDataAccess> &source) {
  auto *entry = findEntry(position, name);
  if (!entry || !source || !source->data) return ERROR_MAIN_EC_ENTRY_NULL;
  auto view = std::make_shared<ecmcDataAccess>(*source);
  view->originOwner = source;
  view->validation = ecmcDataAccess::ORIGIN_STATUS;
  view->validity = nullptr;
  entry->access = view;
  return 0;
}

int ecmcSimulationDriver::addAlias(int position, const char *name, const char *alias) {
  if (!alias || !*alias || strchr(alias, '.') || findEntry(position, alias))
    return ERROR_MAIN_ECMC_LINK_INVALID;
  auto *entry = findEntry(position, name);
  if (!entry) return ERROR_MAIN_EC_ENTRY_NULL;
  aliases_[{position, alias}] = entry;
  return 0;
}

ecmcSimulationDriver::Entry *ecmcSimulationDriver::getEntry(int slaveIndex, int entryIndex) {
  auto *device = findDevice(positionByIndex(slaveIndex));
  return device && entryIndex >= 0 && static_cast<size_t>(entryIndex) < device->entryOrder.size()
    ? device->entryOrder[entryIndex] : nullptr;
}
int ecmcSimulationDriver::findEntryIndex(int position, const char *name) {
  auto *device = findDevice(position);
  auto *entry = findEntry(position, name);
  if (!device || !entry) return -1;
  for (size_t i = 0; i < device->entryOrder.size(); ++i)
    if (device->entryOrder[i] == entry) return static_cast<int>(i);
  return -1;
}

int ecmcSimulationDriver::setObject(int position, int index, int subIndex, const void *data, size_t size) {
  auto *device = findDevice(position);
  if (!device || !data || !size || size > 65536 || index < 0 || index > 65535 || subIndex < 0 || subIndex > 255)
    return ERROR_MAIN_ECMC_LINK_INVALID;
  const auto *bytes = static_cast<const uint8_t *>(data);
  device->objects[(static_cast<unsigned>(index) << 8) | subIndex] = {bytes, bytes + size};
  return 0;
}

int ecmcSimulationDriver::getObject(int position, int index, int subIndex, void *data, size_t size) {
  auto *device = findDevice(position);
  if (!device || !data) return ERROR_MAIN_EC_SLAVE_NULL;
  auto found = device->objects.find((static_cast<unsigned>(index) << 8) | subIndex);
  if (found == device->objects.end() || found->second.size() != size)
    return ERROR_MAIN_ECMC_LINK_INVALID;
  memcpy(data, found->second.data(), size);
  return 0;
}

int ecmcSimulationDriver::setSetting(int position, const std::string &name, std::vector<int64_t> values) {
  auto *device = findDevice(position);
  if (!device || name.empty()) return ERROR_MAIN_ECMC_LINK_INVALID;
  device->settings[name] = std::move(values);
  return 0;
}

int ecmcSimulationDriver::positionByIndex(int index) const {
  return index >= 0 && static_cast<size_t>(index) < deviceOrder_.size() ? deviceOrder_[index] : -2;
}

int ecmcSimulationDriver::parseValue(ecmcEcDataType type, const char *text, uint64_t *result) {
  uint64_t word = 0;
  char *end = nullptr;
  errno = 0;
  const int bits = bitsOf(type);
  if (!bits || !text || !*text || !result) return ERROR_EC_ENTRY_DATATYPE_INVALID;
  if (type == ECMC_EC_F32 || type == ECMC_EC_F64) {
    const double value = strtod(text, &end);
    if (!std::isfinite(value) || (type == ECMC_EC_F32 && !std::isfinite(static_cast<float>(value))))
      return ERROR_EC_ENTRY_VALUE_OUT_OF_RANGE;
    if (type == ECMC_EC_F32) { const float small = value; memcpy(&word, &small, sizeof(small)); }
    else memcpy(&word, &value, sizeof(value));
  } else if (type == ECMC_EC_S8 || type == ECMC_EC_S16 || type == ECMC_EC_S32 || type == ECMC_EC_S64) {
    const int64_t value = strtoll(text, &end, 0);
    if (bits < 64 && (value < -(int64_t(1) << (bits - 1)) || value >= (int64_t(1) << (bits - 1))))
      return ERROR_EC_ENTRY_VALUE_OUT_OF_RANGE;
    word = static_cast<uint64_t>(value);
  } else {
    const char *start = text;
    while (std::isspace(static_cast<unsigned char>(*start))) ++start;
    if (*start == '-') return ERROR_EC_ENTRY_VALUE_OUT_OF_RANGE;
    word = strtoull(text, &end, 0);
    if (bits < 64 && word >= (uint64_t(1) << bits)) return ERROR_EC_ENTRY_VALUE_OUT_OF_RANGE;
  }
  while (end && std::isspace(static_cast<unsigned char>(*end))) ++end;
  if (errno || end == text || !end || *end) return ERROR_EC_ENTRY_VALUE_OUT_OF_RANGE;
  *result = word;
  return 0;
}

int ecmcSimulationDriver::setValue(int position, const char *name, const char *text) {
  auto *entry = findEntry(position, name);
  if (!entry) return ERROR_MAIN_EC_ENTRY_NULL;
  uint64_t value = 0;
  int error = parseValue(entry->access->type, text, &value);
  if (error) return error;
  entry->access->store<uint64_t>(value);
  if (entry->parameter) entry->parameter->refreshParam(1);
  else if (entry->access->publish) return entry->access->publish(entry->access->publishContext);
  return 0;
}

int ecmcSimulationDriver::setObjectHex(int position, int index, int subIndex, const char *text, size_t size) {
  if (!text || !size || size > 65536) return ERROR_MAIN_ECMC_COMMAND_FORMAT_ERROR;
  std::vector<uint8_t> bytes;
  bytes.reserve(size);
  while (*text) {
    while (std::isspace(static_cast<unsigned char>(*text))) ++text;
    if (!*text) break;
    char *end = nullptr;
    errno = 0;
    if (*text == '-') return ERROR_MAIN_ECMC_COMMAND_FORMAT_ERROR;
    unsigned long value = strtoul(text, &end, 16);
    if (errno || end == text || value > 255 || (*end && !std::isspace(static_cast<unsigned char>(*end))))
      return ERROR_MAIN_ECMC_COMMAND_FORMAT_ERROR;
    bytes.push_back(static_cast<uint8_t>(value));
    if (bytes.size() > size) return ERROR_MAIN_ECMC_COMMAND_FORMAT_ERROR;
    text = end;
  }
  return bytes.size() == size ? setObject(position, index, subIndex, bytes.data(), bytes.size())
    : ERROR_MAIN_ECMC_COMMAND_FORMAT_ERROR;
}

int ecmcSimulationDriver::setObjectTyped(int position, int index, int subIndex,
                                        ecmcEcDataType type, const char *text) {
  uint64_t word = 0;
  int error = parseValue(type, text, &word);
  return error ? error : setObject(position, index, subIndex, &word, (bitsOf(type) + 7) / 8);
}

int ecmcSimulationDriver::validate() {
  if (configurationError_) return configurationError_;
  if (active_) return 0;
  std::vector<Entry *> entries;
  for (auto &device : devices_) for (auto &entry : device.second.entries)
    if (entry.second->realtime) entries.push_back(entry.second.get());
  cyclicEntries_ = std::move(entries);
  cyclicDevices_.clear();
  for (int position : deviceOrder_) cyclicDevices_.push_back(&devices_.at(position));
  active_ = true;
  updateStatusWords();
  return 0;
}

void ecmcSimulationDriver::updateStatusWords() {
  const bool ready = active_ && *healthy_;
  // Existing record layouts, describing the memory backend's synthetic state.
  // No measured working counter or distributed-clock state is claimed.
  masterStatus_ = ready ? 1u | (8u << 1) | ((slaveCount_ & 0xffffu) << 16) : 0;
  domainStatus_ = ready ? 1u << 3 : 1u << 1;
  for (auto *device : cyclicDevices_) {
    const uint32_t entries = static_cast<uint32_t>(device->entryOrder.size() - 2);
    device->status = ((entries & 0xffffu) << 16) | (ready ? 1u | 2u | (8u << 2) : 0);
  }
}

int ecmcSimulationDriver::updateIoStatus() {
  if (active_ && !*healthy_ && failedCycles_ < std::numeric_limits<uint32_t>::max())
    ++failedCycles_;
  return *healthy_;
}

void ecmcSimulationDriver::readInputs(timespec time, timespec offset) {
  inputTime_ = time; offset_ = offset;
  // Input storage is already updated by PV clients or a separately bound model.
}

void ecmcSimulationDriver::slowExecute() {
  updateStatusWords();
  if (healthParameter_) healthParameter_->refreshParamRT(0);
  for (auto *parameter : diagnosticParameters_) parameter->refreshParamRT(0);
  for (auto *entry : cyclicEntries_) if (entry->parameter) entry->parameter->refreshParamRT(0);
}
