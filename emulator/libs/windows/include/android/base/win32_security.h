// Copyright (C) 2026 The Android Open Source Project
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
#pragma once

#ifndef _WIN32
#error "Only compile this file when targeting Windows!"
#endif

#include <windows.h>

#include <vector>

#include "absl/status/statusor.h"

#include "android/base/scoped_local_mem.h"

namespace android::base {

/**
 * @brief Utility class for Windows-specific security operations.
 */
class Win32Security {
  public:
    /**
     * @brief Retrieves the SID of the current process user.
     *
     * @return A vector containing the SID bytes, or an error status.
     */
    static absl::StatusOr<std::vector<uint8_t>> GetCurrentUserSid();

    /**
     * @brief Retrieves the SID for the Local System account.
     *
     * @return A vector containing the SID bytes, or an error status.
     */
    static absl::StatusOr<std::vector<uint8_t>> GetLocalSystemSid();

    /**
     * @brief Configures a Security Descriptor with a private DACL.
     *
     * The DACL grants GENERIC_ALL to the current user and Local System,
     * and is protected from inheritance.
     *
     * @param pSd Pointer to the SECURITY_DESCRIPTOR to initialize.
     * @return A ScopedLocalMem containing the PACL which must outlive the usage of pSd.
     */
    static absl::StatusOr<ScopedLocalMem> SetPrivateDacl(PSECURITY_DESCRIPTOR pSd);
};

}  // namespace android::base
