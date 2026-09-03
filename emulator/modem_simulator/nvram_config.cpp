//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "host/commands/modem_simulator/nvram_config.h"

#include <fstream>
#include <mutex>

#include "absl/log/log.h"
#include "json/json.h"

#include "common/libs/utils/files.h"
#include "host/commands/modem_simulator/device_config.h"
#include "host/commands/modem_simulator/network_service.h"

namespace cuttlefish {

static constexpr char kInstances[] = "instances";
static constexpr char kNetworkSelectionMode[] = "network_selection_mode";
static constexpr char kOperatorNumeric[] = "operator_numeric";
static constexpr char kModemTechnoloy[] = "modem_technoloy";
static constexpr char kPreferredNetworkMode[] = "preferred_network_mode";
static constexpr char kEmergencyMode[] = "emergency_mode";

static constexpr int kDefaultNetworkSelectionMode = 0;     // AUTOMATIC
static constexpr int kDefaultModemTechnoloy = NetworkService::M_MODEM_TECH_LTE;
static constexpr int kDefaultPreferredNetworkMode =
    NetworkService::M_MODEM_TECH_LTE | NetworkService::M_MODEM_TECH_WCDMA |
    NetworkService::M_MODEM_TECH_GSM;
static constexpr bool kDefaultEmergencyMode = false;

/**
 * Creates the (initially empty) config object and populates it with values from
 * the config file "modem_nvram.json" located in the cuttlefish instance path,
 * or uses the default value if the config file not exists,
 * Returns nullptr if there was an error loading from file
 */
NvramConfig* NvramConfig::BuildConfigImpl(
    const size_t num_instances,
    const std::optional<std::filesystem::path>& icc_profile_override) {
  auto ret = new NvramConfig(num_instances, icc_profile_override);
  if (ret) {
    const auto nvram_config_path = ConfigFileLocation();
    if (!cuttlefish::FileExists(nvram_config_path) ||
        !cuttlefish::FileHasContent(nvram_config_path.c_str())) {
      ret->InitDefaultNvramConfig();
    } else {
      auto loaded = ret->LoadFromFile(nvram_config_path);
      if (!loaded) {
        /** Bug: (b/315167296)
         * Fall back to default nvram config if LoadFromFile fails.
         */
        ret->InitDefaultNvramConfig();
      }
    }
  }
  return ret;
}

std::unique_ptr<NvramConfig> NvramConfig::s_nvram_config;

void NvramConfig::InitNvramConfigService(
    const size_t num_instances,
    const std::optional<std::filesystem::path>& icc_profile_override) {
  static std::once_flag once_flag;

  std::call_once(once_flag, [num_instances, &icc_profile_override]() {
    NvramConfig::s_nvram_config.reset(
        BuildConfigImpl(num_instances, icc_profile_override));
  });
}

/* static */ const NvramConfig* NvramConfig::Get() {
  return s_nvram_config.get();
}

void NvramConfig::SaveToFile() {
  auto nvram_config = Get();
  const auto nvram_config_file = ConfigFileLocation();
  nvram_config->SaveToFile(nvram_config_file);
}

NvramConfig::NvramConfig(
    const size_t num_instances,
    std::optional<std::filesystem::path> icc_profile_override)
  : total_instances_(num_instances),
    icc_profile_override_(std::move(icc_profile_override)),
    dictionary_(new Json::Value()) {}
// Can't use '= default' on the header because the compiler complains of
// Json::Value being an incomplete type
NvramConfig::~NvramConfig() = default;

NvramConfig::NvramConfig(NvramConfig&&) = default;
NvramConfig& NvramConfig::operator=(NvramConfig&&) = default;

NvramConfig::InstanceSpecific NvramConfig::ForInstance(int num) const {
  return InstanceSpecific(this, std::to_string(num));
}

/* static */ std::filesystem::path NvramConfig::ConfigFileLocation() {
  return cuttlefish::AbsolutePath(
      cuttlefish::modem::DeviceConfig::GetFilePath("modem_nvram.json"));
}

bool NvramConfig::LoadFromFile(const std::filesystem::path& file) {
  auto real_file_path = cuttlefish::AbsolutePath(file);
  if (real_file_path.empty()) {
    LOG(ERROR) << "Could not get real path for file " << file;
    return false;
  }

  Json::CharReaderBuilder builder;
  std::ifstream ifs = modem::DeviceConfig::open_ifstream_crossplat(real_file_path);
  std::string errorMessage;
  if (!Json::parseFromStream(builder, ifs, dictionary_.get(), &errorMessage)) {
    LOG(ERROR) << "Could not read config file " << file << ": "
               << errorMessage;
    return false;
  }
  return true;
}

bool NvramConfig::SaveToFile(const std::filesystem::path& file) const {
  std::ofstream ofs = modem::DeviceConfig::open_ofstream_crossplat(file);
  if (!ofs.is_open()) {
    LOG(ERROR) << "Unable to write to file " << file;
    return false;
  }
  ofs << *dictionary_;
  return !ofs.fail();
}

void NvramConfig::InitDefaultNvramConfig() {
  for (size_t num = 0; num < total_instances_; num++) {
    auto instance = ForInstance(num);
    instance.set_modem_technoloy(kDefaultModemTechnoloy);
    instance.set_network_selection_mode(kDefaultNetworkSelectionMode);
    instance.set_preferred_network_mode(kDefaultPreferredNetworkMode);
    instance.set_emergency_mode(kDefaultEmergencyMode);
  }
}

const Json::Value* NvramConfig::InstanceSpecific::Dictionary() const {
  return &(*config_->dictionary_)[kInstances][id_];
}

Json::Value* NvramConfig::InstanceSpecific::Dictionary() {
  return &(*config_->dictionary_)[kInstances][id_];
}

int NvramConfig::InstanceSpecific::network_selection_mode() const {
  return (*Dictionary())[kNetworkSelectionMode].asInt();
}

void NvramConfig::InstanceSpecific::set_network_selection_mode(int mode) {
  (*Dictionary())[kNetworkSelectionMode] = mode;
}

std::string NvramConfig::InstanceSpecific::operator_numeric() const {
  return (*Dictionary())[kOperatorNumeric].asString();
}

void NvramConfig::InstanceSpecific::set_operator_numeric(std::string& operator_numeric) {
  (*Dictionary())[kOperatorNumeric] = operator_numeric;
}

int NvramConfig::InstanceSpecific::modem_technoloy() const {
  return (*Dictionary())[kModemTechnoloy].asInt();
}

void NvramConfig::InstanceSpecific::set_modem_technoloy(int technoloy) {
  (*Dictionary())[kModemTechnoloy] = technoloy;
}

int NvramConfig::InstanceSpecific::preferred_network_mode() const {
  return (*Dictionary())[kPreferredNetworkMode].asInt();
}

void NvramConfig::InstanceSpecific::set_preferred_network_mode(int mode) {
  (*Dictionary())[kPreferredNetworkMode] = mode;
}

bool NvramConfig::InstanceSpecific::emergency_mode() const {
  return (*Dictionary())[kEmergencyMode].asBool();
}

void NvramConfig::InstanceSpecific::set_emergency_mode(bool mode) {
  (*Dictionary())[kEmergencyMode] = mode;
}

const std::optional<std::filesystem::path>& NvramConfig::icc_profile_override() const {
  return icc_profile_override_;
}

}  // namespace cuttlefish
