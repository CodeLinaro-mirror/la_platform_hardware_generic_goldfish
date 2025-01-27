// Copyright 2022 The Android Open Source Project
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

#include "absl/log/log.h"

#include "android/crashreport/crash-export.h"
#include "client/crash_report_database.h"

namespace android {
namespace crashreport {

using crashpad::CrashReportDatabase;

/**
 * @brief Interface for providing consent to upload crash reports.
 *
 * Implementors of this interface determine whether consent is required
 * to mark a crash report as ready for upload, and handle the consent
 * request process.
 */
class AEMU_CRASH_API CrashConsent {
  public:
    /**
     * @brief Virtual destructor.
     */
    virtual ~CrashConsent() = default;

    /**
     * @brief Consent options.
     */
    enum class Consent {
        ASK,     ///< Ask for consent.
        ALWAYS,  ///< Always grant consent.
        NEVER    ///< Never grant consent.
    };

    /**
     * @brief Actions to take on a crash report.
     */
    enum class ReportAction {
        UNDECIDED_KEEP,  ///< Consent is undecided, keep the report.
        UPLOAD_REMOVE,   ///< Upload the report and remove it from the database.
        REMOVE           ///< Remove the report without uploading.
    };

    /**
     * @brief Determines if consent is required to upload crash reports.
     *
     * @return The consent requirement.
     */
    virtual Consent consentRequired() = 0;

    /**
     * @brief Requests consent for a specific crash report.
     *
     * @param report The crash report.
     * @return The action to take on the report.
     */
    virtual ReportAction requestConsent(const CrashReportDatabase::Report& report) = 0;

    /**
     * @brief Called after a report has been processed (uploaded or removed).
     *
     * @param report The crash report.
     */
    virtual void reportCompleted(const CrashReportDatabase::Report& report) {
        LOG(INFO) << "Report " << report.uuid.ToString()
                  << " is available remotely as: " << report.id;
    }
};

/**
 * @brief Factory function to get a crash consent provider.
 *
 * @return A pointer to a CrashConsent object.
 */
AEMU_CRASH_API CrashConsent* consentProvider();

/**
 * @brief Injects a crash consent provider into the active crash system.
 *
 * This function re-initializes the crash system if it has already been
 * initialized.  This is primarily intended for unit testing purposes
 * to control and validate crash system behavior.
 *
 * @param myProvider A pointer to the CrashConsent object to inject.
 * @return True if the provider was successfully injected, false otherwise.
 */
bool inject_consent_provider(CrashConsent* myProvider);

}  // namespace crashreport
}  // namespace android
