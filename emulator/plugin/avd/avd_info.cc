// Copyright 2024 The Android Open Source Project
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

#include "goldfish/avd/avd_info.h"

#include <atomic>

#include "absl/log/check.h"

namespace goldfish::avd {

namespace {

/*
 * This variable IS nullptr:
 *  * before `avd_info_realize`.
 *  * after `avd_info_unrealize`, e.g. if a snapshot failed to load.
 */
std::atomic<AvdUniverse*> g_global_avd_universe_instance{nullptr};

}  // namespace

AvdUniverse* GetNullableAvd() {
    return g_global_avd_universe_instance;
}

AvdUniverse& GetAvd() {
    AvdUniverse* avd = GetNullableAvd();
    CHECK(avd);  // TODO(b/539388746): handle the nullptr cases
    return *avd;
}

void SetGlobalAvdUniverse(AvdUniverse* avd) {
    g_global_avd_universe_instance = avd;
}

}  // namespace goldfish::avd
