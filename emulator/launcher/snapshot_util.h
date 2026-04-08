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
#include <string_view>

#include "absl/status/status.h"

#include "android/goldfish/avd.h"
#include "goldfish/async/async_socket.h"
#include "goldfish/async/event_loop.h"

namespace android::goldfish {

// Tracks the state of the QMP (QEMU Machine Protocol) interaction during a snapshot save.
struct SnapshotState {
    // True if QMP capabilities have been successfully negotiated.
    bool capabilities_sent = false;

    // True if a command to quit QEMU has been sent.
    bool quit_sent = false;

    // A timer to prevent the snapshot operation from hanging indefinitely.
    std::shared_ptr<::goldfish::async::EventLoop::Timer> timer;

    // Accumulates incoming data from the QMP socket until a full line is received.
    std::string buffer;
};

// Utility class containing helper methods to communicate with QEMU via QMP
// specifically for triggering a snapshot save before quitting the emulator.
class SnapshotUtil {
  public:
    // Initiates the snapshot save process. Posts a task to the event loop to connect to QMP,
    // trigger the snapshot, and eventually call kill_emulator() on error.
    static void save_snapshot_and_quit(::goldfish::async::EventLoop& event_loop, int qmp_port,
                                       const std::string& snapshot_name, Avd* avd,
                                       std::function<void()> kill_emulator);

    // Callback when the QMP socket connection is established (or fails).
    static void on_qmp_connected(::goldfish::async::AsyncSocket& s, absl::Status err,
                                 std::shared_ptr<SnapshotState> state,
                                 std::shared_ptr<::goldfish::async::AsyncSocket> shared_socket,
                                 const std::string& snapshot_name, Avd* avd,
                                 std::function<void()> kill_emulator);

    // Processes a single newline-terminated JSON message from QEMU over QMP.
    // It handles the handshake, sends the 'savevm' command, and finally the 'quit' command.
    static void process_qmp_line_for_snapshot_save(
            std::string_view line, std::shared_ptr<SnapshotState> state,
            std::shared_ptr<::goldfish::async::AsyncSocket> shared_socket,
            const std::string& snapshot_name, Avd* avd);

    // Callback to handle raw data chunks received from the QMP socket.
    // It manages the connection state, buffers data into individual lines,
    // and delegates each line to process_qmp_line_for_snapshot_save.
    static void handle_qmp_read_for_snapshot_save(
            std::string_view data, absl::Status err, std::shared_ptr<SnapshotState> state,
            std::shared_ptr<::goldfish::async::AsyncSocket> shared_socket,
            const std::string& snapshot_name, Avd* avd);
};

}  // namespace android::goldfish
