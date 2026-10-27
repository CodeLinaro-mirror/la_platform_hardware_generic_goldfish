// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "goldfish/modem_simulator/modem_simulator_client.h"

#ifndef _WIN32
#include <netinet/in.h>
#endif

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"

#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"

namespace goldfish::modem_simulator {
namespace {

using ::testing::HasSubstr;
using Call = IModemSimulatorClient::Call;
using CallDirection = IModemSimulatorClient::CallDirection;
using CallState = IModemSimulatorClient::CallState;

int GetServerPort(const cuttlefish::SharedFD& server) {
    struct sockaddr_storage addr = {};
    socklen_t addr_len = 0;
    if (!server->Endpoint(&addr, &addr_len)) {
        return -1;
    }
    if (addr.ss_family == AF_INET) {
        return ntohs(reinterpret_cast<const struct sockaddr_in*>(&addr)->sin_port);
    }
    if (addr.ss_family == AF_INET6) {
        return ntohs(reinterpret_cast<const struct sockaddr_in6*>(&addr)->sin6_port);
    }
    return -1;
}

class FakeModemServer {
  public:
    FakeModemServer() {
        server_ = cuttlefish::SharedFD::SocketLocalServer();
        EXPECT_TRUE(static_cast<bool>(server_));
        port_ = GetServerPort(server_);
        EXPECT_GT(port_, 0);
        EXPECT_TRUE(cuttlefish::SharedFD::Pipe(&cancel_reader_, &cancel_writer_));

        accept_thread_ = std::thread([this]() {
            while (true) {
                cuttlefish::SharedFDSet read_set;
                read_set.Set(server_);
                read_set.Set(cancel_reader_);

                const int nfds = cuttlefish::Select(&read_set, nullptr, nullptr, nullptr);
                if (nfds < 0) {
                    break;
                }
                if (nfds == 0) {
                    continue;
                }
                if (read_set.IsSet(cancel_reader_)) {
                    break;
                }
                if (read_set.IsSet(server_)) {
                    cuttlefish::SharedFD conn = cuttlefish::SharedFD::Accept(*server_);
                    if (conn) {
                        connections_.push_back(std::move(conn));
                    }
                }
            }
        });
    }

    ~FakeModemServer() {
        cancel_writer_->Write("q", 1);
        if (accept_thread_.joinable()) {
            accept_thread_.join();
        }
    }

    int port() const { return port_; }

  private:
    cuttlefish::SharedFD server_;
    cuttlefish::SharedFD cancel_reader_;
    cuttlefish::SharedFD cancel_writer_;
    std::vector<cuttlefish::SharedFD> connections_;
    std::thread accept_thread_;
    int port_ = -1;
};

TEST(ModemSimulatorClientTest, GetCellInfoReturnsUnimplementedWithDetail) {
    ModemSimulatorClient client(/*serverPort=*/1);
    auto cell_info = client.GetCellInfo();
    ASSERT_FALSE(cell_info.ok());
    EXPECT_EQ(cell_info.status().code(), absl::StatusCode::kUnimplemented);
    EXPECT_THAT(std::string(cell_info.status().message()),
                HasSubstr("Failed to get cell info: GetCellInfo is not implemented"));
}

TEST(ModemSimulatorClientTest, UpdateCallUnspecifiedStateReturnsInvalidArgument) {
    ModemSimulatorClient client(/*serverPort=*/1);
    const Call call = {
        .number = "+15551234567",
        .direction = CallDirection::INBOUND,
        .state = CallState::UNSPECIFIED,
    };

    auto updated = client.UpdateCall(call);
    ASSERT_FALSE(updated.ok());
    EXPECT_EQ(updated.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_THAT(std::string(updated.status().message()),
                HasSubstr("Failed to update call: call state is CallState::UNSPECIFIED (0)"));
    EXPECT_THAT(std::string(updated.status().message()), HasSubstr("Specify an active call state"));
}

TEST(ModemSimulatorClientTest, ConnectToClosedPortReturnsUnavailableWithPortAndGuidance) {
    cuttlefish::SharedFD closed_server = cuttlefish::SharedFD::SocketLocalServer();
    ASSERT_TRUE(static_cast<bool>(closed_server));
    const int closed_port = GetServerPort(closed_server);
    ASSERT_GT(closed_port, 0);
    closed_server->Close();

    ModemSimulatorClient client(closed_port);
    const absl::Status status = client.UpdateClock();
    ASSERT_FALSE(status.ok());
    EXPECT_EQ(status.code(), absl::StatusCode::kUnavailable);
    EXPECT_THAT(std::string(status.message()),
                HasSubstr("Failed to connect to modem simulator on port " +
                          std::to_string(closed_port)));
    EXPECT_THAT(std::string(status.message()),
                HasSubstr("Verify that the modem simulator service is running"));
}

TEST(ModemSimulatorClientTest, CreateDuplicateCallReturnsAlreadyExists) {
    FakeModemServer server;
    ModemSimulatorClient client(server.port());
    const Call call = {
        .number = "+15551234567",
        .direction = CallDirection::INBOUND,
    };

    auto created = client.CreateCall(call);
    ASSERT_TRUE(created.ok()) << created.status();
    EXPECT_EQ(created->state, CallState::ACTIVE);

    auto duplicate = client.CreateCall(call);
    ASSERT_FALSE(duplicate.ok());
    EXPECT_EQ(duplicate.status().code(), absl::StatusCode::kAlreadyExists);
    EXPECT_THAT(std::string(duplicate.status().message()),
                HasSubstr("Failed to create incoming call for '+15551234567'"));
    EXPECT_THAT(std::string(duplicate.status().message()), HasSubstr("already in progress"));
    EXPECT_THAT(std::string(duplicate.status().message()), HasSubstr("Use updateCall"));
}

TEST(ModemSimulatorClientTest, UpdateUnknownCallReturnsNotFound) {
    FakeModemServer server;
    ModemSimulatorClient client(server.port());
    const Call call = {
        .number = "+15559990000",
        .direction = CallDirection::INBOUND,
        .state = CallState::HELD,
    };

    auto updated = client.UpdateCall(call);
    ASSERT_FALSE(updated.ok());
    EXPECT_EQ(updated.status().code(), absl::StatusCode::kNotFound);
    EXPECT_THAT(std::string(updated.status().message()),
                HasSubstr("Failed to update call for '+15559990000'"));
    EXPECT_THAT(std::string(updated.status().message()), HasSubstr("no active call found"));
    EXPECT_THAT(std::string(updated.status().message()), HasSubstr("Use createCall"));
}

TEST(ModemSimulatorClientTest, DeleteUnknownCallReturnsNotFound) {
    FakeModemServer server;
    ModemSimulatorClient client(server.port());
    const Call call = {
        .number = "+15559990000",
        .direction = CallDirection::INBOUND,
    };

    const absl::Status status = client.DeleteCall(call);
    ASSERT_FALSE(status.ok());
    EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);
    EXPECT_THAT(std::string(status.message()),
                HasSubstr("Failed to delete call for '+15559990000'"));
    EXPECT_THAT(std::string(status.message()), HasSubstr("no active call found"));
    EXPECT_THAT(std::string(status.message()), HasSubstr("Use listCalls"));
}

}  // namespace
}  // namespace goldfish::modem_simulator
