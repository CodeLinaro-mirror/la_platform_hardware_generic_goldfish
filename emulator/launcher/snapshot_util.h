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

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "android/goldfish/avd.h"
#include "goldfish/async/async_socket_factory.h"
#include "goldfish/async/event_loop.h"

namespace android::goldfish {

// Utility class containing helper methods to communicate with QEMU via QMP
// specifically for triggering a snapshot save before quitting the emulator.
class SnapshotUtil {
  public:
    // Initiates the snapshot save process. Posts a task to the event loop to connect to QMP,
    // trigger the snapshot, and eventually call kill_emulator() on error.
    static void save_snapshot_and_quit(::goldfish::async::EventLoop& event_loop,
                                       ::goldfish::async::AsyncSocketFactory& factory, int qmp_port,
                                       const std::string& snapshot_name, Avd* avd,
                                       std::function<void()> kill_emulator);
};

}  // namespace android::goldfish
