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

#include "snapshot_util.h"

#include <chrono>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"

#include "goldfish/async/libuv_socket_factory.h"
#include "goldfish/network/endpoint.h"

namespace android::goldfish {
namespace {

// Helper to remove the incomplete snapshot directory in case of a failure or timeout.
void delete_snapshot_dir(const std::string& snapshot_name, Avd* avd) {
    std::error_code ec;
    std::filesystem::remove_all(avd->GetContentPath() / "snapshots" / snapshot_name, ec);
}

// Helper to safely cancel the timeout timer, mark the quit state, and send the quit command.
void send_quit_command(std::shared_ptr<SnapshotState> state,
                       std::shared_ptr<::goldfish::async::AsyncSocket> shared_socket) {
    if (state->timer) {
        state->timer->Cancel();
    }
    state->quit_sent = true;
    std::string cmd = "{\"execute\": \"quit\"}\n";
    (void)shared_socket->Send(cmd.c_str(), cmd.size());
}

}  // namespace

void SnapshotUtil::save_snapshot_and_quit(::goldfish::async::EventLoop& event_loop, int qmp_port,
                                          const std::string& snapshot_name, Avd* avd,
                                          std::function<void()> kill_emulator) {
    event_loop
            .Post([&event_loop, qmp_port, snapshot_name, avd, kill_emulator]() {
                auto factory = std::make_unique<::goldfish::async::LibuvAsyncSocketFactory>();
                auto socket = factory->CreateSocket(
                        &event_loop,
                        ::goldfish::network::ToEndpoint(
                                ::goldfish::network::ToIpv4Address(127, 0, 0, 1), qmp_port));

                if (!socket) {
                    LOG(ERROR) << "Failed to create QMP socket, killing emulator";
                    kill_emulator();
                    return;
                }

                auto state = std::make_shared<SnapshotState>();
                auto shared_socket =
                        std::shared_ptr<::goldfish::async::AsyncSocket>(std::move(socket));

                shared_socket->SetOnConnectedCallback(
                        [shared_socket, state, snapshot_name, avd, kill_emulator](
                                ::goldfish::async::AsyncSocket& s, absl::Status err) {
                            on_qmp_connected(s, err, state, shared_socket, snapshot_name, avd,
                                             kill_emulator);
                        });

                if (auto status = shared_socket->Connect(); !status.ok()) {
                    LOG(ERROR) << "Failed to initiate QMP connection: " << status
                               << ", killing emulator";
                    kill_emulator();
                }
            })
            .IgnoreError();
}

void SnapshotUtil::on_qmp_connected(::goldfish::async::AsyncSocket& s, absl::Status err,
                                    std::shared_ptr<SnapshotState> state,
                                    std::shared_ptr<::goldfish::async::AsyncSocket> shared_socket,
                                    const std::string& snapshot_name, Avd* avd,
                                    std::function<void()> kill_emulator) {
    if (!err.ok()) {
        LOG(ERROR) << "Failed to connect to QMP: " << err << ", killing emulator";
        kill_emulator();
        return;
    }
    LOG(INFO) << "Connected to QMP for snapshot save";

    s.SetOnReadCallbackNoFlowControl([shared_socket, state, snapshot_name, avd](
                                             std::string_view data, absl::Status err) {
        handle_qmp_read_for_snapshot_save(data, err, state, shared_socket, snapshot_name, avd);
    });
}

void SnapshotUtil::process_qmp_line_for_snapshot_save(
        std::string_view line, std::shared_ptr<SnapshotState> state,
        std::shared_ptr<::goldfish::async::AsyncSocket> shared_socket,
        const std::string& snapshot_name, Avd* avd) {
    if (line.find("\"QMP\"") != std::string::npos) {
        // Greeting received, send qmp_capabilities to enter command mode.
        std::string cmd = "{\"execute\": \"qmp_capabilities\"}\n";
        (void)shared_socket->Send(cmd.c_str(), cmd.size());
    } else if (!state->capabilities_sent && line.find("\"return\": {}") != std::string::npos) {
        state->capabilities_sent = true;

        // Capabilities accepted, start the actual snapshot save command.
        std::string cmd = absl::StrFormat(
                "{\"execute\": \"human-monitor-command\", "
                "\"arguments\": {\"command-line\": \"savevm %s\"}}\n",
                snapshot_name);
        LOG(INFO) << "Triggering snapshot save: " << snapshot_name;
        (void)shared_socket->Send(cmd.c_str(), cmd.size());

        // Set a timeout of 5 minutes for the snapshot save.
        state->timer =
                shared_socket->GetLoop()->CreateTimer([shared_socket, state, snapshot_name, avd]() {
                    LOG(ERROR) << "Snapshot save timed out after 5 minutes, quitting anyway";
                    delete_snapshot_dir(snapshot_name, avd);
                    send_quit_command(state, shared_socket);
                });
        state->timer->Schedule(std::chrono::minutes(5));
    } else if (state->capabilities_sent && !state->quit_sent) {
        if (line.find("\"return\":") != std::string::npos) {
            // human-monitor-command returned successfully
            LOG(INFO) << "Snapshot save complete, quitting QEMU";
            send_quit_command(state, shared_socket);
        } else if (line.find("\"error\":") != std::string::npos) {
            // human-monitor-command returned an error
            LOG(ERROR) << "Snapshot save failed: " << line;
            delete_snapshot_dir(snapshot_name, avd);
            send_quit_command(state, shared_socket);
        }
    }
}

void SnapshotUtil::handle_qmp_read_for_snapshot_save(
        std::string_view data, absl::Status err, std::shared_ptr<SnapshotState> state,
        std::shared_ptr<::goldfish::async::AsyncSocket> shared_socket,
        const std::string& snapshot_name, Avd* avd) {
    if (!err.ok()) {
        if (state->quit_sent) {
            VLOG(1) << "QMP connection closed as expected after quit";
        } else {
            LOG(ERROR) << "QMP connection closed unexpectedly: " << err;
            delete_snapshot_dir(snapshot_name, avd);
        }
        return;
    }
    state->buffer.append(data);

    // Simple line-based JSON processing. Each line contains a complete QMP JSON response.
    size_t pos;
    while ((pos = state->buffer.find('\n')) != std::string::npos) {
        std::string line = state->buffer.substr(0, pos);
        state->buffer.erase(0, pos + 1);
        VLOG(2) << "QMP << " << line;
        process_qmp_line_for_snapshot_save(line, state, shared_socket, snapshot_name, avd);
    }
}

}  // namespace android::goldfish
