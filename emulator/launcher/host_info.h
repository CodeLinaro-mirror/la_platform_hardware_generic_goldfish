// Copyright (C) 2026 the Android Open Source Project
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

#include "android/goldfish/avd.h"
#include "studio_stats.pb.h"

namespace android::goldfish {

void FillEmulatorHostEvent(android_studio::AndroidStudioEvent& event, const Avd& avd,
                           long launcher_pid, long qemu_pid, bool metrics_collection_opt,
                           bool fuchsia_opt);

}  // namespace android::goldfish