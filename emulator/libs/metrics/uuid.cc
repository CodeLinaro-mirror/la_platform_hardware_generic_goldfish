// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS);
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "goldfish/metrics/uuid.h"

#include <string_view>

#ifdef _WIN32
#include <rpc.h>
#else
#include <uuid/uuid.h>
#endif

#include "absl/strings/str_cat.h"

namespace goldfish::metrics {

namespace {
constexpr std::string_view kNullUuidStr = "00000000-0000-0000-0000-000000000000";
}

absl::StatusOr<Uuid> Uuid::FromString(std::string_view uuid_str) {
    Uuid uuid;
#ifdef _WIN32
    if (::UuidFromStringA(reinterpret_cast<RPC_CSTR>(const_cast<char*>(uuid_str.data())),
                          &uuid.data_) != RPC_S_OK) {
        return absl::InvalidArgumentError(
                absl::StrCat("couldn't parse string as uuid: ", uuid_str));
    }
#else
    if (uuid_parse_range(uuid_str.data(), uuid_str.data() + uuid_str.size(), uuid.data_.data()) <
        0) {
        return absl::InvalidArgumentError(
                absl::StrCat("couldn't parse string as uuid: ", uuid_str));
    }
#endif
    return uuid;
}

Uuid Uuid::Generate() {
    Uuid res;
#ifdef _WIN32
    ::UuidCreate(&res.data_);
#else
    uuid_generate(res.data_.data());
#endif
    return res;
}

Uuid Uuid::Zero() {
    static const Uuid kZero = *FromString(kNullUuidStr);
    return kZero;
}

std::string Uuid::ToString() const {
#ifdef _WIN32
    unsigned char* str = nullptr;
    if (::UuidToStringA(&data_, &str) != RPC_S_OK || !str) {
        return std::string(kNullUuidStr);
    }

    std::string res(reinterpret_cast<char*>(str));
    ::RpcStringFree(&str);
#else
    std::string res(kNullUuidStr.size() + 1, '\0');
    uuid_unparse_lower(data_.data(), &res[0]);
    res.pop_back();
#endif
    return res;
}

}  // namespace goldfish::metrics
