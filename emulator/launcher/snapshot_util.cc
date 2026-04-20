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
#include <string_view>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"

#include "goldfish/network/endpoint.h"

namespace android::goldfish {

namespace {

/**
 * @brief Manages the multi-step asynchronous process of saving a snapshot and quitting.
 *
 * This class implements a state machine to handle the QMP protocol exchange:
 * 1. Connect to the QMP port.
 * 2. Wait for the QMP greeting.
 * 3. Send and wait for 'qmp_capabilities'.
 * 4. Send 'savevm' command via 'human-monitor-command'.
 * 5. Wait for the result and then send 'quit'.
 * 6. Finally, kill the emulator process (via kill_emulator callback).
 *
 * It inherits from std::enable_shared_from_this to ensure its lifetime persists
 * while asynchronous callbacks are pending. Circular shared_ptr references in
 * callbacks are avoided by explicitly resetting members in Finish().
 */
class SnapshotSaveOperation : public std::enable_shared_from_this<SnapshotSaveOperation> {
  public:
    SnapshotSaveOperation(::goldfish::async::EventLoop& event_loop,
                          ::goldfish::async::AsyncSocketFactory& factory, int qmp_port,
                          std::string snapshot_name, Avd* avd, std::function<void()> kill_emulator)
            : event_loop_(event_loop)
            , factory_(factory)
            , qmp_port_(qmp_port)
            , snapshot_name_(std::move(snapshot_name))
            , avd_(avd)
            , kill_emulator_(std::move(kill_emulator)) {}

    void Start() {
        socket_ = factory_.CreateSocket(
                &event_loop_, ::goldfish::network::ToEndpoint(
                                      ::goldfish::network::ToIpv4Address(127, 0, 0, 1), qmp_port_));

        if (!socket_) {
            LOG(ERROR) << "Failed to create QMP socket, killing emulator";
            Finish();
            return;
        }

        socket_->SetOnConnectedCallback(
                [self = shared_from_this()](::goldfish::async::AsyncSocket& s, absl::Status err) {
                    self->OnConnected(s, err);
                });

        if (auto status = socket_->Connect(); !status.ok()) {
            LOG(ERROR) << "Failed to initiate QMP connection: " << status << ", killing emulator";
            Finish();
        }
    }

  private:
    enum class State {
        kConnecting,
        kWaitingForGreeting,
        kWaitingForCapabilitiesReturn,
        kWaitingForSnapshotReturn,
        kWaitingForQuitReturn,
        kFinished
    };

    void OnConnected(::goldfish::async::AsyncSocket& s, absl::Status err) {
        if (!err.ok()) {
            LOG(ERROR) << "Failed to connect to QMP: " << err << ", killing emulator";
            Finish();
            return;
        }
        VLOG(1) << "Connected to QMP for snapshot save";
        state_ = State::kWaitingForGreeting;

        socket_->SetOnReadCallbackNoFlowControl(
                [self = shared_from_this()](std::string_view data, absl::Status err) {
                    self->OnRead(data, err);
                });
    }

    void OnRead(std::string_view data, absl::Status err) {
        if (!err.ok()) {
            if (state_ != State::kFinished) {
                LOG(ERROR) << "QMP connection closed unexpectedly in state "
                           << static_cast<int>(state_) << ": " << err;
                DeleteSnapshotDir();
            }
            Finish();
            return;
        }

        buffer_.append(data);

        size_t pos;
        while ((pos = buffer_.find('\n')) != std::string::npos) {
            std::string line = buffer_.substr(0, pos);
            buffer_.erase(0, pos + 1);
            VLOG(2) << "QMP << " << line;
            ProcessLine(line);
        }
    }

    void ProcessLine(std::string_view line) {
        switch (state_) {
        case State::kWaitingForGreeting:
            if (line.find("\"QMP\"") != std::string::npos) {
                SendCommand(R"({"execute": "qmp_capabilities"})");
                state_ = State::kWaitingForCapabilitiesReturn;
            }
            break;

        case State::kWaitingForCapabilitiesReturn:
            if (line.find("\"return\": {}") != std::string::npos) {
                TriggerSnapshot();
            }
            break;

        case State::kWaitingForSnapshotReturn:
            if (line.find("\"return\":") != std::string::npos) {
                LOG(INFO) << "Snapshot save complete, quitting QEMU";
                SendQuit();
            } else if (line.find("\"error\":") != std::string::npos) {
                LOG(ERROR) << "Snapshot save failed: " << line;
                DeleteSnapshotDir();
                SendQuit();
            }
            break;

        case State::kWaitingForQuitReturn:
            // We don't usually get a return from 'quit' as QEMU exits,
            // but if we do, we can finish.
            Finish();
            break;

        default:
            break;
        }
    }

    void TriggerSnapshot() {
        LOG(INFO) << "Triggering snapshot save: " << snapshot_name_;
        SendCommand(
                absl::StrFormat("{\"execute\": \"human-monitor-command\", "
                                "\"arguments\": {\"command-line\": \"savevm %s\"}}",
                                snapshot_name_));
        state_ = State::kWaitingForSnapshotReturn;

        // Set a timeout of 5 minutes for the snapshot save.
        timer_ = event_loop_.ScheduleDelayed(
                [self = shared_from_this()]() {
                    LOG(ERROR) << "Snapshot save timed out after 5 minutes, quitting anyway";
                    self->DeleteSnapshotDir();
                    self->SendQuit();
                },
                std::chrono::minutes(5));
    }

    void SendQuit() {
        if (timer_) {
            timer_->Cancel();
        }
        state_ = State::kWaitingForQuitReturn;
        SendCommand(R"({"execute": "quit"})");
    }

    void SendCommand(const std::string& cmd) {
        if (socket_) {
            std::string raw = cmd + "\n";
            (void)socket_->Send(raw.c_str(), raw.size());
        }
    }

    void DeleteSnapshotDir() {
        std::error_code ec;
        std::filesystem::remove_all(avd_->GetContentPath() / "snapshots" / snapshot_name_, ec);
    }

    void Finish() {
        state_ = State::kFinished;
        if (timer_) {
            timer_->Cancel();
            timer_.reset();
        }
        if (socket_) {
            socket_->Close();
            socket_.reset();
        }
        kill_emulator_();
    }

    ::goldfish::async::EventLoop& event_loop_;
    ::goldfish::async::AsyncSocketFactory& factory_;
    int qmp_port_;
    std::string snapshot_name_;
    Avd* avd_;
    std::function<void()> kill_emulator_;

    std::shared_ptr<::goldfish::async::AsyncSocket> socket_;
    std::shared_ptr<::goldfish::async::EventLoop::Timer> timer_;

    State state_ = State::kConnecting;
    std::string buffer_;
};

}  // namespace

void SnapshotUtil::save_snapshot_and_quit(::goldfish::async::EventLoop& event_loop,
                                          ::goldfish::async::AsyncSocketFactory& factory,
                                          int qmp_port, const std::string& snapshot_name, Avd* avd,
                                          std::function<void()> kill_emulator) {
    event_loop
            .Post([&event_loop, &factory, qmp_port, snapshot_name, avd,
                   kill_emulator = std::move(kill_emulator)]() mutable {
                auto op = std::make_shared<SnapshotSaveOperation>(event_loop, factory, qmp_port,
                                                                  snapshot_name, avd,
                                                                  std::move(kill_emulator));
                op->Start();
            })
            .IgnoreError();
}

}  // namespace android::goldfish
