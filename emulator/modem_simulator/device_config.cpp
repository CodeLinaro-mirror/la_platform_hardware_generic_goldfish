/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "host/commands/modem_simulator/device_config.h"

#define DEFAULT_IPV4_ADDRESS_AND_PREFIX "10.0.2.15/24"
#define DEFAULT_IPV6_ADDRESS_AND_PREFIX "2001:02::/24"
#define DEFAULT_IPV4_GATEWAY "10.0.2.2"
#define DEFAULT_IPV6_GATEWAY "2001:02::2"
#define DEFAULT_IPV4_DNS "10.0.2.3"
#define DEFAULT_IPV6_DNS "2001:02::3"

namespace cuttlefish {
namespace modem {
namespace {
std::filesystem::path g_base_path;
int g_modem_port = 0;
bool g_use_ipv6 = true;
}  // namespace

void DeviceConfig::Init(int argc, char** argv) {
}

void DeviceConfig::SetBasePath(std::filesystem::path base_path) {
    g_base_path = std::move(base_path);
}

int DeviceConfig::host_id() {
    return g_modem_port;
}

std::filesystem::path DeviceConfig::GetFilePath(const char* file_name) {
    return g_base_path / file_name;
}

std::string DeviceConfig::ril_address_and_prefix() {
    return g_use_ipv6 ? DEFAULT_IPV6_ADDRESS_AND_PREFIX : DEFAULT_IPV4_ADDRESS_AND_PREFIX;
};

std::string DeviceConfig::ril_gateway() {
    return g_use_ipv6 ? DEFAULT_IPV6_GATEWAY : DEFAULT_IPV4_GATEWAY;
}

std::string DeviceConfig::ril_dns() {
    return g_use_ipv6 ? DEFAULT_IPV6_DNS :  DEFAULT_IPV4_DNS;
}

std::ifstream DeviceConfig::open_ifstream_crossplat(const std::filesystem::path& filename) {
    return std::ifstream(filename);
}

std::ofstream DeviceConfig::open_ofstream_crossplat(const std::filesystem::path& filename,
                                                    std::ios_base::openmode mode) {
    return std::ofstream(filename, mode);
}

}  // namespace modem
}  // namespace cuttlefish
