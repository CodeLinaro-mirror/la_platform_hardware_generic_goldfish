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

#include <functional>
#include <iosfwd>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "aemu/base/Compiler.h"
#include "android/crashreport/AnnotationStreambuf.h"
#include "android/crashreport/HangDetector.h"
#include "base/files/file_path.h"
#include "client/annotation.h"
#include "host-common/crash-handler.h"

namespace crashpad {
class Annotation;
}  // namespace crashpad

using base::FilePath;

namespace android {
namespace crashreport {

using crashpad::Annotation;

/**
 * @brief Singleton class tracking crashpad annotations
 *
 * Provides a method to track crashpad annotations.
 */
class CrashReporter {
  public:
    /**
     * @brief Constructor.
     */
    CrashReporter();

    /**
     * @brief Destructor.
     */
    ~CrashReporter() = default;

    /**
     * @brief Gets the singleton instance of the CrashReporter.
     *
     * @return A pointer to the CrashReporter instance.
     */
    static CrashReporter* get();

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
     *                If false, appends the data. Defaults to false.
     */
    void attachData(std::string name, std::string data, bool replace = false);

    /**
     * @brief Adds an addition message to the default `internal-msg` annotation.
     *
     * Note: this can hold at most 8kb of data.
     *
     * @param message The message.
     */
    void addMessage(const char* message);

    /**
     * @brief Generates a crash dump and terminates the pro cess.
     *
     * The message will be added to the 'internal-msg' annotation.
     *
     * @param message The message to include in the dump.
     */
    ANDROID_NORETURN void die(const char* message);

    /**
     * @brief Uploads all pending crash reports.
     */
    void uploadEntries();

    /**
     * @brief Returns a reference to the hang detector.
     *
     * @return Reference to the hang detector.
     */
    HangDetector& hangDetector();

    /**
     * @brief Returns the path to the crash report database directory.
     *
     * @return The database directory as a FilePath.
     */
    static FilePath databaseDirectory();

    /**
     * @brief Returns the path to the crashpad handler executable.
     *
     * @return The handler executable path as a FilePath.
     */
    static FilePath handlerExe();

  private:
    DISALLOW_COPY_AND_ASSIGN(CrashReporter);

    std::unique_ptr<HangDetector> mHangDetector;
    std::vector<std::unique_ptr<Annotation>> mAnnotations;
    DefaultAnnotationStreambuf mAnnotationBuf{"internal-msg"};
    std::ostream mAnnotationLog{&mAnnotationBuf};
};

}  // namespace crashreport
}  // namespace android
