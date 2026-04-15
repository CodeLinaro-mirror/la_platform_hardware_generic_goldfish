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

#include <string>
#include <string_view>

namespace android {
namespace base {

class ThreadUtils {
  public:
    /**
     * @brief Sets the name of the current thread.
     *
     * This name will appear in debuggers, stack traces, and system monitors.
     * Note that some platforms have strict limits on thread name length
     * (e.g., 15 characters on Linux).
     *
     * @param name The name to set for the current thread.
     */
    static void SetCurrentThreadName(std::string_view name);

    /**
     * @brief Gets the name of the current thread.
     *
     * @return The name of the current thread, or an empty string if it could not
     * be retrieved.
     */
    static std::string GetCurrentThreadName();
};

}  // namespace base
}  // namespace android
