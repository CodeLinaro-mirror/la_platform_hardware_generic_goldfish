// Copyright (C) 2025 The Android Open Source Project
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

#include "android/emulation/control/utils/emulator_grpc_client.h"

#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

#include "aemu/base/sockets/ScopedSocket.h"
#include "aemu/base/sockets/SocketUtils.h"
#include "emulator_controller.grpc.pb.h"

using android::emulation::control::BlockingEmulatorGrpcClient;
using android::emulation::control::CallbackEmulatorGrpcClient;
using android::emulation::control::ConnectionState;
using android::emulation::control::EmulatorController;
using android::emulation::control::EmulatorGrpcClientBuilder;
using android::emulation::control::EmulatorStatus;
using android::emulation::control::Endpoint;
using ::google::protobuf::Empty;

namespace {

namespace fs = std::filesystem;

// A mock implementation of the EmulatorController service for testing.
class MockEmulatorController final : public EmulatorController::Service {
  public:
    grpc::Status getStatus(grpc::ServerContext* context, const Empty* request,
                           EmulatorStatus* response) override {
        response->set_version("test-version");
        return grpc::Status::OK;
    }
};

// A test fixture for managing the gRPC server and client instances.
class EmulatorGrpcClientTest : public ::testing::Test {
  protected:
    void TearDown() override {
        if (server) {
            server->Shutdown();
        }
    }

    void StartServer() {
        server_address = "localhost:50051";
        grpc::ServerBuilder builder;
        builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
        builder.RegisterService(&service);
        server = builder.BuildAndStart();
        ASSERT_NE(server, nullptr);
    }

    // RAII helper for creating and cleaning up a temporary discovery file.
    class TmpDiscoveryFile {
      public:
        explicit TmpDiscoveryFile(const std::string& content) {
            std::string file_name = absl::StrCat(
                    "grpc_test_",
                    absl::Hex(absl::Uniform<uint64_t>(absl::BitGen()), absl::kSpacePad16));
            mPath = fs::temp_directory_path() / file_name;
            std::ofstream out(mPath);
            out << content;
        }
        ~TmpDiscoveryFile() {
            std::error_code ec;
            fs::remove(mPath, ec);
        }
        const fs::path& path() const { return mPath; }

      private:
        fs::path mPath;
    };

    MockEmulatorController service;
    std::unique_ptr<grpc::Server> server;
    std::string server_address;
};

// --- Builder Tests ---

TEST_F(EmulatorGrpcClientTest, Builder_CanBuildBlockingClient) {
    auto clientOrStatus = EmulatorGrpcClientBuilder().withEndpoint(Endpoint()).buildBlocking();
    ASSERT_TRUE(clientOrStatus.ok());
    ASSERT_NE(*clientOrStatus, nullptr);
}

TEST_F(EmulatorGrpcClientTest, Builder_CanBuildCallbackClient) {
    auto clientOrStatus = EmulatorGrpcClientBuilder().withEndpoint(Endpoint()).buildCallback();
    ASSERT_TRUE(clientOrStatus.ok());
    ASSERT_NE(*clientOrStatus, nullptr);
}

TEST_F(EmulatorGrpcClientTest, Builder_ValidDiscoveryFile_Succeeds) {
    TmpDiscoveryFile tmpFile("grpc.port = 8554");
    auto clientOrStatus =
            EmulatorGrpcClientBuilder().withDiscoveryFile(tmpFile.path()).buildBlocking();
    ASSERT_TRUE(clientOrStatus.ok());
    auto client = std::move(*clientOrStatus);
    ASSERT_NE(client, nullptr);
    EXPECT_EQ(client->getEndpoint().target(), "localhost:8554");
}

TEST_F(EmulatorGrpcClientTest, Builder_NonExistentDiscoveryFile_Fails) {
    auto clientOrStatus =
            EmulatorGrpcClientBuilder().withDiscoveryFile("/no/such/file.ini").buildBlocking();
    ASSERT_FALSE(clientOrStatus.ok());
}

// --- Blocking Client Tests ---

class BlockingClientTest : public EmulatorGrpcClientTest {
  protected:
    std::unique_ptr<BlockingEmulatorGrpcClient> CreateClient(const std::string& target) {
        Endpoint endpoint;
        endpoint.set_target(target);
        return EmulatorGrpcClientBuilder().withEndpoint(endpoint).buildBlocking().value();
    }
};

TEST_F(BlockingClientTest, InitialState_IsDisconnected) {
    auto client = CreateClient("localhost:12345");
    EXPECT_EQ(client->getConnectionState(), ConnectionState::Disconnected);
}

TEST_F(BlockingClientTest, Connect_WithLiveServer_Succeeds) {
    StartServer();
    auto client = CreateClient(server_address);
    absl::Status status = client->connect(absl::Seconds(5));
    ASSERT_TRUE(status.ok());
    EXPECT_EQ(client->getConnectionState(), ConnectionState::Connected);
}

TEST_F(BlockingClientTest, Connect_WithNoServer_Fails) {
    auto client = CreateClient("localhost:12345");
    absl::Status status = client->connect(absl::Milliseconds(100));
    EXPECT_EQ(status.code(), absl::StatusCode::kDeadlineExceeded);
    EXPECT_EQ(client->getConnectionState(), ConnectionState::Disconnected);
}

TEST_F(BlockingClientTest, Rpc_WithValidStub_Succeeds) {
    StartServer();
    auto client = CreateClient(server_address);
    ASSERT_TRUE(client->connect(absl::Seconds(5)).ok());
    auto stubOrStatus = client->stub<EmulatorController>();
    ASSERT_TRUE(stubOrStatus.ok());
    auto stub = std::move(*stubOrStatus);
    Empty request;
    EmulatorStatus response;
    absl::StatusOr<std::unique_ptr<grpc::ClientContext>> contextOrStatus = client->newContext();
    ASSERT_TRUE(contextOrStatus.ok());
    auto context = std::move(*contextOrStatus);
    grpc::Status status = stub->getStatus(context.get(), request, &response);
    ASSERT_TRUE(status.ok());
    EXPECT_EQ(response.version(), "test-version");
}

TEST_F(BlockingClientTest, Connect_WithNonLocalAndNoTls_FailsWithInvalidArgument) {
    auto client = CreateClient("8.8.8.8:12345");
    absl::Status status = client->connect(absl::Milliseconds(100));
    EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

// --- Callback Client Tests ---

class CallbackClientTest : public EmulatorGrpcClientTest {
  protected:
    std::unique_ptr<CallbackEmulatorGrpcClient> CreateClient(const std::string& target) {
        Endpoint endpoint;
        endpoint.set_target(target);
        return EmulatorGrpcClientBuilder().withEndpoint(endpoint).buildCallback().value();
    }
};

TEST_F(CallbackClientTest, ConnectAsync_WithLiveServer_Succeeds) {
    StartServer();
    auto client = CreateClient(server_address);
    auto future = client->connectAsync(absl::Seconds(5));
    EXPECT_EQ(client->getConnectionState(), ConnectionState::Connecting);

    ASSERT_EQ(future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    absl::Status status = future.get();
    ASSERT_TRUE(status.ok()) << "Expected ok, not " << status;
    EXPECT_EQ(client->getConnectionState(), ConnectionState::Connected);
}

// flaky
TEST_F(CallbackClientTest, DISABLED_ConnectAsync_WithNoServer_Fails) {
    auto client = CreateClient("localhost:12345");
    auto future = client->connectAsync(absl::Seconds(1));
    EXPECT_EQ(client->getConnectionState(), ConnectionState::Connecting);

    ASSERT_EQ(future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    absl::Status status = future.get();
    EXPECT_EQ(status.code(), absl::StatusCode::kUnavailable);
    EXPECT_EQ(client->getConnectionState(), ConnectionState::Disconnected);
    VLOG(1) << "Test: Finished. Client will be destroyed now.";
}

// TODO FIX this test is flakey due to a race in the connectAsync callback handling - the fix will
// require changing how the grpc connection monitor works.
TEST_F(CallbackClientTest, DISABLED_Disconnect_DuringAsyncConnection_Cancels) {
    StartServer();
    VLOG(1) << "Test: Creating client.";
    auto client = CreateClient(server_address);
    VLOG(1) << "Test: Calling connectAsync.";
    auto future = client->connectAsync(absl::Seconds(10));
    VLOG(1) << "Test: Calling disconnect.";
    client->disconnect();

    VLOG(1) << "Test: Waiting for future.";
    ASSERT_EQ(future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    VLOG(1) << "Test: Future is ready. Getting status.";
    absl::Status status = future.get();
    EXPECT_EQ(status.code(), absl::StatusCode::kCancelled);
    EXPECT_EQ(client->getConnectionState(), ConnectionState::Disconnected);
    VLOG(1) << "Test: Finished. Client will be destroyed now.";
}

// TODO FIX this test is flakey
TEST_F(CallbackClientTest, DISABLED_ConnectAsync_WithLiveServer_CanReconnect) {
    StartServer();
    VLOG(1) << "Test: Creating client.";
    auto client = CreateClient(server_address);
    VLOG(1) << "Test: Calling connectAsync.";
    auto future = client->connectAsync(absl::Seconds(10));
    ASSERT_EQ(future.wait_for(std::chrono::seconds(1)), std::future_status::ready);

    VLOG(1) << "Test: Calling disconnect.";
    client->disconnect();
    auto snd = client->connectAsync(absl::Seconds(10));
    ASSERT_EQ(snd.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    EXPECT_EQ(snd.get(), absl::OkStatus());
}

TEST_F(CallbackClientTest, Destructor_DuringAsyncConnection_Cancels) {
    // StartServer();
    android::base::ScopedSocket s0;
    // Find a free TCP IPv4 port and bind to it.
    int port;
    for (port = 1024; port < 65536; ++port) {
        s0.reset(android::base::socketTcp4LoopbackServer(port));
        if (s0.valid()) {
            break;
        }
    }
    auto future = std::async(std::launch::async, [this, port] {
                      auto client = CreateClient("localhost:" + std::to_string(port));
                      return client->connectAsync(absl::Seconds(10));
                  }).get();

    ASSERT_EQ(future.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    absl::Status status = future.get();
    EXPECT_EQ(status.code(), absl::StatusCode::kCancelled);
}

TEST_F(CallbackClientTest, EventSource_FiresCorrectStates) {
    StartServer();
    auto client = CreateClient(server_address);
    std::vector<ConnectionState> received_states;
    auto handle = android::base::eventing::makeScopedCallback(
            client->connectionStateChanges(),
            [&](ConnectionState s) { received_states.push_back(s); });

    auto future = client->connectAsync(absl::Seconds(1));
    ASSERT_EQ(future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    future.get();

    ASSERT_EQ(received_states.size(), 2);
    EXPECT_EQ(received_states[0], ConnectionState::Connecting);
    EXPECT_EQ(received_states[1], ConnectionState::Connected);

    client->disconnect();
    ASSERT_EQ(received_states.size(), 3);
    EXPECT_EQ(received_states[2], ConnectionState::Disconnected);
    VLOG(1) << "Test: Finished. Client will be destroyed now.";
}

TEST_F(CallbackClientTest, LivenessMonitor_DetectsServerShutdown) {
    StartServer();
    auto client = CreateClient(server_address);

    absl::Mutex m;
    absl::CondVar cv;
    std::vector<ConnectionState> received_states;
    auto handle = android::base::eventing::makeScopedCallback(client->connectionStateChanges(),
                                                              [&](ConnectionState s) {
                                                                  absl::MutexLock lock(&m);
                                                                  received_states.push_back(s);
                                                                  cv.Signal();
                                                              });

    auto future = client->connectAsync(absl::Seconds(5));
    ASSERT_EQ(future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    ASSERT_TRUE(future.get().ok());
    ASSERT_EQ(client->getConnectionState(), ConnectionState::Connected);

    server->Shutdown();

    absl::MutexLock lock(&m);
    while (client->getConnectionState() != ConnectionState::Disconnected) {
        if (cv.WaitWithTimeout(&m, absl::Seconds(1))) {
            break;
        }
    }

    EXPECT_EQ(client->getConnectionState(), ConnectionState::Disconnected);
    ASSERT_EQ(received_states.size(), 3);
    EXPECT_EQ(received_states[0], ConnectionState::Connecting);
    EXPECT_EQ(received_states[1], ConnectionState::Connected);
    EXPECT_EQ(received_states[2], ConnectionState::Disconnected);
}

TEST_F(CallbackClientTest, Disconnect_FromCallback_DoesNotDeadlock) {
    StartServer();
    auto client = CreateClient(server_address);

    absl::Mutex m;
    absl::CondVar cv;
    bool disconnected_event_fired = false;

    auto handle = android::base::eventing::makeScopedCallback(
            client->connectionStateChanges(), [&](ConnectionState s) {
                if (s == ConnectionState::Connected) {
                    client->disconnect();
                }
                if (s == ConnectionState::Disconnected) {
                    absl::MutexLock lock(&m);
                    disconnected_event_fired = true;
                    cv.Signal();
                }
            });

    auto future = client->connectAsync(absl::Seconds(5));
    ASSERT_EQ(future.wait_for(std::chrono::seconds(5)), std::future_status::ready);

    // The future should be OK because the connection succeeded before disconnect.
    EXPECT_TRUE(future.get().ok());

    // Wait for the disconnect to complete.
    absl::MutexLock lock(&m);
    while (!disconnected_event_fired) {
        if (cv.WaitWithTimeout(&m, absl::Seconds(1))) {
            break;
        }
    }
    EXPECT_TRUE(disconnected_event_fired) << "Timed out waiting for disconnect event.";

    EXPECT_EQ(client->getConnectionState(), ConnectionState::Disconnected);
}

}  // namespace
