// Copyright 2015 The Android Open Source Project
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

#include "android/crashreport/hang_detector.h"

namespace android::crashreport {

/**
 * @brief Singleton class tracking crashpad annotations
 *
 * Provides a method to track crashpad annotations.
 */
class CrashReporter {
  public:
    virtual ~CrashReporter() = default;

    /**
     * @brief Attaches data to the crash report.
     *
     * This results in the creation of an annotation object with the given data.
     * The annotation object will use str::size < 2^x bytes of memory in the minidump.
     * Note that the provided string should not be larger than 16384 bytes.
     *
     * @param name A description of the data.
     * @param data The data to attach as a string.
     * @param replace If true, replaces existing data with the same name.
     *                If false, appends the data.
     */
    virtual void AttachData(std::string name, std::string data, bool replace) = 0;

    /**
     * @brief Attaches data to the crash report without replacing existing data.
     *
     * @param name A description of the data.
     * @param data The data to attach as a string.
     */
    void AttachData(std::string name, std::string data) {
        AttachData(std::move(name), std::move(data), false);
    }

    /**
     * @brief Adds an addition message to the default `internal-msg` annotation.
     *
     * Note: this can hold at most 8kb of data.
     *
     * @param message The message.
     */
    virtual void AddMessage(std::string_view message) = 0;

    /**
     * @brief Generates a crash dump and terminates the pro cess.
     *
     * The message will be added to the 'internal-msg' annotation.
     *
     * @param message The message to include in the dump.
     */
    virtual void Die(std::string_view message) = 0;

    /**
     * @brief Gets the singleton instance of the CrashReporter.
     *
     * @return A pointer to the CrashReporter instance.
     */
    static CrashReporter& Get();

    static HangDetector& GetCrashingHangDetector();
};

}  // namespace android::crashreport
