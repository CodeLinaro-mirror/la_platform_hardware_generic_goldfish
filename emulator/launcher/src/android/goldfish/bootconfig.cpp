// Copyright 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "bootconfig.h"

#include <aemu/base/utils/status_macros.h>

#include <fstream>
#include <memory>
#include <numeric>

#include "absl/log/log.h"
#include "absl/status/status.h"

#include "android/base/system/File.h"

namespace goldfish::bootconfig {
using namespace std::literals;

constexpr std::string_view kBootconfigMagic = "#BOOTCONFIG\n"sv;
constexpr uint32_t kBootconfigAlign = 4;

void host2le32(const uint32_t v32, void* dst) {
    auto m8 = static_cast<uint8_t*>(dst);
    m8[0] = v32;
    m8[1] = v32 >> 8;
    m8[2] = v32 >> 16;
    m8[3] = v32 >> 24;
}

std::vector<char> flattenBootconfig(
        const std::vector<std::pair<std::string, std::string>>& bootconfig) {
    std::vector<char> bits;

    for (const auto& kv : bootconfig) {
        bits.insert(bits.end(), kv.first.begin(), kv.first.end());
        bits.push_back('=');
        bits.push_back('\"');
        bits.insert(bits.end(), kv.second.begin(), kv.second.end());
        bits.push_back('\"');
        bits.push_back('\n');
    }
    bits.push_back(0);  // it is ASCIIZ

    return bits;
}

absl::Status appendBootconfig(const std::vector<std::pair<std::string, std::string>>& bootconfig, fs::path dst) {
    ASSIGN_OR_RETURN(auto old_size, android::base::file::file_size(dst));
    std::vector<char> blob = buildBootconfigBlob(old_size.bytes(), bootconfig);

    std::ofstream out;
    out.open(dst, std::ios_base::app | std::ios_base::binary);
    if (!out) {
        return absl::InternalError("failed to open initrd for writing");
    }
    if(out << std::string_view(blob.data(), blob.size())) {
        return absl::OkStatus();
    }
    return absl::InternalError("failed to append bootconfig to initrd");
}

std::vector<char> buildBootconfigBlob(
        const size_t srcSize, const std::vector<std::pair<std::string, std::string>>& bootconfig) {
    std::vector<char> blob = flattenBootconfig(bootconfig);

    const size_t unaligend = (srcSize + blob.size()) % kBootconfigAlign;
    if (unaligend) {
        blob.insert(blob.end(), kBootconfigAlign - unaligend, '+');
    }

    const uint32_t csum = std::accumulate(
            blob.begin(), blob.end(), 0,
            [](const uint32_t z, const char c) { return z + static_cast<uint8_t>(c); });

    const size_t size = blob.size();

    blob.insert(blob.end(), 8, '+');  // size(u32, LE), csum(u32, LE)
    host2le32(size, &blob[blob.size() - 8]);
    host2le32(csum, &blob[blob.size() - 4]);

    blob.insert(blob.end(), kBootconfigMagic.begin(), kBootconfigMagic.end());

    return blob;
}

absl::Status createRamdiskWithBootconfig(fs::path srcRamdiskPath, fs::path dstRamdiskPath,
        const std::vector<std::pair<std::string, std::string>>& bootconfig) {
    RETURN_IF_ERROR(android::base::file::cp_file(srcRamdiskPath, dstRamdiskPath, /*overwrite=*/true));
    return appendBootconfig(bootconfig, dstRamdiskPath);
}

}  // namespace goldfish::bootconfig
