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
#include <stdexcept>

namespace {
int bitsOf(ecmcEcDataType type) {
  if (type >= ECMC_EC_B1 && type <= ECMC_EC_B4) return static_cast<int>(type);
  if (type >= ECMC_EC_S8_TO_U8 && type <= ECMC_EC_S64_TO_U64) return 8 << (type - ECMC_EC_S8_TO_U8);
  return static_cast<int>(ecmcDataAccess::sizeOf(type) * 8);
}
}

class ecmcSimulationAsyncSDO {
public:
  ecmcSimulationAsyncSDO(ecmcAsynPortDriver *port, int master, int slave,
                         ecmcEcDataType type, const char *alias, size_t bytes,
                         std::vector<uint8_t> *storage, bool array)
    : port_(port), type_(type), bytes_(bytes), storage_(storage), array_(array),
      read_(array ? bytes : sizeof(uint64_t), 0),
      write_(array ? bytes : sizeof(uint64_t), 0) {
    if (!port_) return;
    const std::string base = "ec" + std::to_string(master) + ".s" +
      std::to_string(slave) + ".sdo." + alias;
    value_ = add(base + ".value", array ? asynParamInt8Array : asynParamInt32,
                 read_.data(), array ? bytes : sizeof(uint64_t), !array);
    error_ = add(base + ".error", asynParamInt32,
                 reinterpret_cast<uint8_t *>(&errorValue_), sizeof(errorValue_), false);
    readCmd_ = add(base + ".readcmd", asynParamInt32,
                   reinterpret_cast<uint8_t *>(&readTrigger_), sizeof(readTrigger_), true);
    writeCmd_ = add(base + ".writecmd", asynParamInt32,
                    reinterpret_cast<uint8_t *>(&writeTrigger_), sizeof(writeTrigger_), true);
    busy_ = add(base + ".busy", asynParamInt32,
                reinterpret_cast<uint8_t *>(&busyValue_), sizeof(busyValue_), false);
    if (array) {
      writeValue_ = add(base + ".writevalue", asynParamInt8Array,
                        write_.data(), bytes, true);
      readBytes_ = static_cast<int32_t>(bytes);
      bytesParam_ = add(base + ".bytes", asynParamInt32,
                        reinterpret_cast<uint8_t *>(&readBytes_), sizeof(readBytes_), false);
      writeValue_->setExeCmdFunctPtr(writeArray, this);
    }
    if (!value_ || !error_ || !readCmd_ || !writeCmd_ || !busy_ ||
        (array && (!writeValue_ || !bytesParam_))) throw std::bad_alloc();
    value_->setEcmcDataSize(array ? bytes : bytes_);
    value_->setEcmcBitCount(bytes_ * 8);
    if (!array) {
      value_->addSupportedAsynType(asynParamFloat64);
      if (type != ECMC_EC_F32 && type != ECMC_EC_F64)
        value_->addSupportedAsynType(asynParamUInt32Digital);
#ifdef ECMC_ASYN_ASYNPARAMINT64
      if (bytes_ == sizeof(uint64_t)) value_->addSupportedAsynType(asynParamInt64);
#endif
    }
    readCmd_->setExeCmdFunctPtr(readCommand, this);
    writeCmd_->setExeCmdFunctPtr(writeCommand, this);
  }
  ~ecmcSimulationAsyncSDO() {
    for (auto *item : {value_, error_, readCmd_, writeCmd_, busy_, writeValue_, bytesParam_})
      if (item) item->setEcmcDataPointer(nullptr, 0);
  }

private:
  ecmcAsynDataItem *add(const std::string &name, asynParamType asynType,
                        uint8_t *data, size_t bytes, bool writable) {
    auto *item = port_->addNewAvailParam(name.c_str(), asynType, data, bytes, type_, 0);
    if (item) { item->setAllowWriteToEcmc(writable); item->refreshParam(1); }
    return item;
  }
  static asynStatus readCommand(void *data, size_t bytes, asynParamType type, void *object) {
    return static_cast<ecmcSimulationAsyncSDO *>(object)->command(data, bytes, type, false);
  }
  static asynStatus writeCommand(void *data, size_t bytes, asynParamType type, void *object) {
    return static_cast<ecmcSimulationAsyncSDO *>(object)->command(data, bytes, type, true);
  }
  static asynStatus writeArray(void *data, size_t bytes, asynParamType type, void *object) {
    auto *self = static_cast<ecmcSimulationAsyncSDO *>(object);
    if (!data || type != asynParamInt8Array || bytes != self->bytes_) return asynError;
    memcpy(self->write_.data(), data, bytes);
    return asynSuccess;
  }
  asynStatus command(void *data, size_t bytes, asynParamType type, bool write) {
    if (!data || bytes != sizeof(int32_t) || type != asynParamInt32) return asynError;
    int32_t trigger = 0; memcpy(&trigger, data, sizeof(trigger));
    int32_t &old = write ? writeTrigger_ : readTrigger_;
    const bool rising = trigger && !old; old = trigger;
    if (!rising) return asynSuccess;
    if (write) {
      if (array_) *storage_ = write_;
      else storage_->assign(read_.begin(), read_.begin() + bytes_);
    } else {
      if (storage_->size() != bytes_) { errorValue_ = ERROR_MAIN_ECMC_LINK_INVALID; return asynError; }
      memcpy(read_.data(), storage_->data(), bytes_);
      value_->refreshParam(1);
      if (bytesParam_) bytesParam_->refreshParam(1);
    }
    errorValue_ = 0; error_->refreshParam(1);
    return asynSuccess;
  }
  ecmcAsynPortDriver *port_;
  ecmcEcDataType type_; size_t bytes_; std::vector<uint8_t> *storage_; bool array_;
  std::vector<uint8_t> read_, write_;
  int32_t errorValue_ = 0, readTrigger_ = 0, writeTrigger_ = 0, busyValue_ = 0, readBytes_ = 0;
  ecmcAsynDataItem *value_ = nullptr, *error_ = nullptr, *readCmd_ = nullptr,
    *writeCmd_ = nullptr, *busy_ = nullptr, *writeValue_ = nullptr, *bytesParam_ = nullptr;
};

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
    registerDiagnostic(prefix + "ok", &statusOk_);
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
  if (simulationParameter) simulationParameter->setEcmcDataPointer(nullptr, 0);
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
  return addEntryInternal(position, vendor, product, direction, syncManager, pdo,
                          index, subIndex, type, name, realtime, nullptr);
}

int ecmcSimulationDriver::addDataItem(int position, const char *sourceName,
  size_t byteOffset, size_t bitOffset, int direction, ecmcEcDataType type,
  const char *name) {
  auto *device = findDevice(position);
  auto *source = findEntry(position, sourceName);
  if (!device || !source) return ERROR_MAIN_EC_ENTRY_NULL;

  // Simulated PDOs have independent backing words. An aligned view can
  // share bytes with its parent but cannot cross into another PDO entry.
  const size_t bytes = ecmcDataAccess::sizeOf(type);
  const size_t sourceBytes = source->access->getBits() / 8;
  if (!bytes || bitOffset || byteOffset > sourceBytes ||
      bytes > sourceBytes - byteOffset)
    return ERROR_MAIN_IO_CONFIG_UNSUPPORTED;
  if (source->direction != direction || !source->access->wordStorage ||
      source->access->data != reinterpret_cast<uint8_t *>(&source->value))
    return ERROR_MAIN_ECMC_LINK_INVALID;

  return addEntryInternal(position, device->vendor, device->product, direction,
                          source->syncManager, source->pdo, source->index,
                          source->subIndex, type, name, true,
                          source->access->data + byteOffset);
}

int ecmcSimulationDriver::addEntryInternal(int position, uint32_t vendor,
  uint32_t product, int direction, int syncManager, int pdo, int index,
  int subIndex, ecmcEcDataType type, const char *name, bool realtime,
  uint8_t *sharedData) {
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
  access.identity = entry.get();
  access.data = sharedData ? sharedData : reinterpret_cast<uint8_t *>(&entry->value);
  access.type = type; access.bits = bits;
  access.wordStorage = !sharedData; access.writable = true;
  access.finiteRequired = type == ECMC_EC_F32 || type == ECMC_EC_F64;
  access.validation = access.finiteRequired ? ecmcDataAccess::NATIVE_ITEM : ecmcDataAccess::DIRECT_STATUS;
  access.validityOwner = healthy_; access.validity = healthy_.get();
  if (port_ && realtime && (position >= 0 || id_ != 0)) {
    const std::string path = "ec" + std::to_string(id_) + ".s" + std::to_string(position) + "." + name;
    auto *parameter = port_->addNewAvailParam(path.c_str(), asynParamInt32,
      access.data, sharedData ? (bits + 7) / 8 : sizeof(entry->value), type, 0);
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
    // Cyclic publication belongs to the input/output phases below. Repeated
    // scalar writes must not advance asyn's cycle-based sampling divider.
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
  if (!alias || !*alias || strchr(alias, '.'))
    return ERROR_MAIN_ECMC_LINK_INVALID;
  auto *entry = findEntry(position, name);
  if (!entry) return ERROR_MAIN_EC_ENTRY_NULL;
  if (auto *existing = findEntry(position, alias))
    return existing == entry ? 0 : ERROR_MAIN_ECMC_LINK_INVALID;
  if (port_ && entry->parameter) {
    const std::string prefix = "ec" + std::to_string(id_) + ".s" + std::to_string(position) + ".";
    int error = port_->addParamAlias((prefix + name).c_str(), (prefix + alias).c_str());
    if (error) return error;
  }
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

int ecmcSimulationDriver::addSDOAsync(int position, int index, int subIndex,
                                     ecmcEcDataType type, const char *name,
                                     size_t arrayBytes) {
  auto *device = findDevice(position);
  const size_t bytes = arrayBytes ? arrayBytes : ecmcDataAccess::sizeOf(type);
  if (!device || index < 0 || index > 0xffff || subIndex < 0 || subIndex > 0xff ||
      !name || !*name || !bytes || bytes > 1024 * 1024)
    return ERROR_MAIN_ECMC_LINK_INVALID;
  try {
    auto &storage = device->objects[(static_cast<unsigned>(index) << 8) | subIndex];
    if (storage.empty()) storage.resize(bytes, 0);
    if (storage.size() != bytes) return ERROR_MAIN_ECMC_LINK_INVALID;
    device->asyncSdos.emplace_back(std::make_unique<ecmcSimulationAsyncSDO>(
      port_, id_, position, type, name, bytes, &storage, arrayBytes != 0));
  } catch (const std::exception &) {
    return ERROR_MAIN_ASYN_CREATE_PARAM_FAIL;
  }
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
  if (entry->access->wordStorage) {
    entry->access->store<uint64_t>(value);
  } else {
    // parseValue returns the native bit representation, including floats.
    std::memcpy(entry->access->data, &value, (entry->access->bits + 7) / 8);
  }
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

const char *ecmcSimulationDriver::portName() const { return port_ ? port_->portName : nullptr; }

const char *ecmcSimulationDriver::simulationPortName() const {
  return simulationPort_ ? simulationPort_->portName : nullptr;
}

int ecmcSimulationDriver::prepareSimulationParameter(int position, const char *name) {
  auto *entry = findEntry(position, name);
  if (!entry || !port_) return ERROR_MAIN_EC_ENTRY_NULL;
  if (entry->simulationParameter) return 0;
  const auto type = entry->access->type;
  asynParamType asynType = asynParamUInt32Digital;
  if (type == ECMC_EC_F32 || type == ECMC_EC_F64) asynType = asynParamFloat64;
  else if (entry->access->getBits() == 64) {
#ifdef ECMC_ASYN_ASYNPARAMINT64
    asynType = asynParamInt64;
#else
    return ERROR_MAIN_IO_CONFIG_UNSUPPORTED;
#endif
  } else if (type == ECMC_EC_S8 || type == ECMC_EC_S16 || type == ECMC_EC_S32)
    asynType = asynParamInt32;
  size_t count = 0;
  for (const auto &device : devices_) count += device.second.entries.size();
  // Reserve for the default access parameter and future record connections.
  if (count > (static_cast<size_t>(std::numeric_limits<int>::max()) - 2) / 2)
    return ERROR_MAIN_ASYN_CREATE_PARAM_FAIL;
  const std::string portName = std::string(port_->portName) + "_SIM" + std::to_string(id_);
  simulationPort_ = port_->createAuxiliaryPort(portName.c_str(), static_cast<int>(2 * count + 2));
  if (!simulationPort_) return ERROR_MAIN_ASYN_CREATE_PARAM_FAIL;
  const std::string path = "sim.ec" + std::to_string(id_) + ".s" + std::to_string(position) + "." + name;
  auto *parameter = simulationPort_->addNewAvailParam(path.c_str(), asynType,
    entry->access->data, entry->access->wordStorage ? sizeof(entry->value) :
      (entry->access->getBits() + 7) / 8, type, 0);
  if (!parameter) return ERROR_MAIN_ASYN_CREATE_PARAM_FAIL;
  entry->simulationParameter = parameter;
  parameter->setEcmcDataSize((entry->access->getBits() + 7) / 8);
  parameter->setEcmcBitCount(entry->access->getBits());
  parameter->setAllowWriteToEcmc(true); // Private simulation view; outputs have manual -Set records.
  parameter->refreshParam(1);
  active_ = false; // Rebuild the publication lists before the next RT entry.
  return 0;
}

int ecmcSimulationDriver::validate() {
  if (configurationError_) return configurationError_;
  if (active_) return 0;
  std::vector<Entry *> inputs, outputs;
  for (auto &device : devices_) for (auto &entry : device.second.entries) {
    if (!entry.second->realtime || (!entry.second->parameter && !entry.second->simulationParameter)) continue;
    if (entry.second->direction == 2) inputs.push_back(entry.second.get());
    else outputs.push_back(entry.second.get());
  }
  cyclicInputs_ = std::move(inputs);
  cyclicOutputs_ = std::move(outputs);
  cyclicDevices_.clear();
  for (int position : deviceOrder_) cyclicDevices_.push_back(&devices_.at(position));
  active_ = true;
  updateStatusWords();
  return 0;
}

void ecmcSimulationDriver::updateStatusWords() {
  const bool ready = active_ && *healthy_;
  statusOk_ = ready;
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
  if (statusOk_ != static_cast<uint32_t>(active_ && *healthy_)) updateStatusWords();
  if (active_ && !*healthy_ && failedCycles_ < std::numeric_limits<uint32_t>::max())
    ++failedCycles_;
  // refreshParamRT applies a divider expressed in RT cycles, not slow ticks.
  if (healthParameter_) healthParameter_->refreshParamRT(0);
  for (auto *parameter : diagnosticParameters_) parameter->refreshParamRT(0);
  return *healthy_;
}

void ecmcSimulationDriver::readInputs(timespec time, timespec offset) {
  inputTime_ = time; offset_ = offset;
  // Input storage is already updated by PV clients or a separately bound model.
  for (auto *entry : cyclicInputs_) {
    if (entry->parameter) entry->parameter->refreshParamRT(0);
    if (entry->simulationParameter) entry->simulationParameter->refreshParamRT(0);
  }
}

void ecmcSimulationDriver::writeOutputs(timespec offset) {
  offset_ = offset;
  for (auto *entry : cyclicOutputs_) {
    if (entry->parameter) entry->parameter->refreshParamRT(0);
    if (entry->simulationParameter) entry->simulationParameter->refreshParamRT(0);
  }
}
