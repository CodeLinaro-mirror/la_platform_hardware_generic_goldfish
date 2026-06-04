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

#include "whpx.h"

#ifdef _WIN32
#include <windows.h>
#include <dismapi.h>

#include <sstream>

namespace android {
namespace {

using CommandReturn = std::pair<int, std::string>;

CommandReturn dismOpen(DismSession* session) {
    HRESULT hr;

    hr = DismInitialize(DismLogErrorsWarningsInfo, NULL, NULL);
    if (hr == __HRESULT_FROM_WIN32(ERROR_ELEVATION_REQUIRED))
        return std::make_pair(hr, "emulator-check whpx commands require administrator privilege.");
    else if (hr != S_OK)
        return std::make_pair(hr, "Could not initialize Dism API.");

    *session = DISM_SESSION_DEFAULT;
    hr = DismOpenSession(DISM_ONLINE_IMAGE, NULL, NULL, session);
    if (hr != S_OK) {
        DismShutdown();
        return std::make_pair(hr, "Could not open Dism session.");
    }
    return std::make_pair(0, "dismOpen succeeded.");
}

void dismClose(DismSession* session) {
    if (!session) return;

    DismCloseSession(*session);
    DismShutdown();
}

const char* dism_feature_state[] = {"NotPresent", "UninstallPending",  "Staged",
                                    "Removed",    "Installed",         "InstallPending",
                                    "Superseded", "PartiallyInstalled"};

}  // namespace

CommandReturn checkWHPX() {
    HRESULT hr;
    CommandReturn ret;
    DismSession Session = DISM_SESSION_DEFAULT;

    ret = dismOpen(&Session);
    if (ret.first != 0) return ret;

    DismFeatureInfo* FeatureInfo;
    hr = DismGetFeatureInfo(Session, L"HypervisorPlatform", NULL, DismPackageNone, &FeatureInfo);

    dismClose(&Session);

    if (hr == S_OK) {
        std::stringstream message;
        message << "Feature state of Windows Hypervisor Platform: " << FeatureInfo->FeatureState
                << "(" << dism_feature_state[FeatureInfo->FeatureState] << ").";
        return std::make_pair(0, message.str());
    } else
        return std::make_pair(hr, "DismGetFeatureInfo failed.");
}

CommandReturn enableWHPX() {
    HRESULT hr;
    CommandReturn ret;
    DismSession Session = DISM_SESSION_DEFAULT;

    ret = dismOpen(&Session);
    if (ret.first != 0) return ret;

    hr = DismEnableFeature(Session, L"HypervisorPlatform", NULL, DismPackageNone, FALSE, NULL, 0,
                           FALSE, NULL, NULL, NULL);

    dismClose(&Session);

    if (hr == S_OK || hr == (HRESULT)ERROR_SUCCESS_REBOOT_REQUIRED)
        return std::make_pair(0,
                              "Windows Hypervisor Platform is enabled in Windows Features. Reboot "
                              "is required to take effect.");
    else
        return std::make_pair(hr, "DismEnableFeature Failed.");
}

CommandReturn disableWHPX() {
    HRESULT hr;
    CommandReturn ret;
    DismSession Session = DISM_SESSION_DEFAULT;

    ret = dismOpen(&Session);
    if (ret.first != 0) return ret;

    hr = DismDisableFeature(Session, L"HypervisorPlatform", NULL, TRUE, NULL, NULL, NULL);

    dismClose(&Session);

    if (hr == S_OK || hr == (HRESULT)ERROR_SUCCESS_REBOOT_REQUIRED)
        return std::make_pair(0,
                              "Windows Hypervisor Platform is disabled in Windows Features. Reboot "
                              "is required to take effect.");
    else
        return std::make_pair(hr, "DismDisableFeature Failed.");
}

}  // namespace android
#endif
