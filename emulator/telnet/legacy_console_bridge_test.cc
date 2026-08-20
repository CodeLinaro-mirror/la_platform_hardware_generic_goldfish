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

#include "legacy_console_bridge.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "android/base/system.h"
#include "android/base/testing/test_system.h"
#include "android/base/testing/test_temp_dir.h"
#include "emulator_controller.grpc.pb.h"
#include "emulator_controller_mock.grpc.pb.h"
#include "goldfish/discovery/emulator_advertisement.h"
#include "goldfish/file/file.h"
#include "modem_service_mock.grpc.pb.h"
#include "snapshot_service.grpc.pb.h"
#include "snapshot_service_mock.grpc.pb.h"
#include "telnet_auth.h"

namespace goldfish::telnet {
namespace {

using testing::_;

struct MockConsoleContext : public LegacyConsoleBridge::ConsoleContext {
    using ConsoleContext::ConsoleContext;

    absl::StatusOr<std::unique_ptr<android::emulation::control::EmulatorController::StubInterface>>
    EmulatorControllerStub() override {
        if (mock_stub) {
            return std::move(mock_stub);
        }
        return ConsoleContext::EmulatorControllerStub();
    }

    absl::StatusOr<std::unique_ptr<android::emulation::control::incubating::Modem::StubInterface>>
    ModemStub() override {
        if (mock_modem_stub) {
            return std::move(mock_modem_stub);
        }
        return ConsoleContext::ModemStub();
    }

    absl::StatusOr<std::unique_ptr<android::emulation::control::SnapshotService::StubInterface>>
    SnapshotStub() override {
        if (mock_snapshot_stub) {
            return std::move(mock_snapshot_stub);
        }
        return ConsoleContext::SnapshotStub();
    }

    absl::StatusOr<std::unique_ptr<grpc::ClientContext>> NewContext(
            std::chrono::time_point<std::chrono::system_clock> deadline =
                    std::chrono::system_clock::now() + std::chrono::milliseconds(500)) override {
        auto ctx = std::make_unique<grpc::ClientContext>();
        ctx->set_deadline(deadline);
        return ctx;
    }

    absl::StatusOr<std::vector<std::filesystem::path>> DiscoverRunningEmulators() override {
        if (mock_discovered_emulators) {
            return *mock_discovered_emulators;
        }
        return ConsoleContext::DiscoverRunningEmulators();
    }

    absl::StatusOr<LegacyConsoleBridge::DiscoveredEmulator> DiscoverEmulatorWithProperties(
            const absl::flat_hash_map<std::string, std::string>& props) override {
        if (mock_discovery) {
            return *mock_discovery;
        }
        return ConsoleContext::DiscoverEmulatorWithProperties(props);
    }

    std::unique_ptr<android::emulation::control::EmulatorController::StubInterface> mock_stub;
    std::unique_ptr<android::emulation::control::incubating::Modem::StubInterface> mock_modem_stub;
    std::unique_ptr<android::emulation::control::SnapshotService::StubInterface> mock_snapshot_stub;
    std::optional<absl::StatusOr<LegacyConsoleBridge::DiscoveredEmulator>> mock_discovery;
    std::optional<absl::StatusOr<std::vector<std::filesystem::path>>> mock_discovered_emulators;
};

class LegacyConsoleBridgeTest : public ::testing::Test {
  protected:
    void SetUp() override {
        test_home_ = tmpdir_.Path() / "test_home_auth";
        auto discovery_dir = test_home_ / "Library/Caches/TemporaryItems/avd/running";
        auto status = android::base::file::mkdir_recursive(discovery_dir, 0700);
        ASSERT_TRUE(status.ok()) << "Failed to create test home directory: " << status.message();
        test_system_.SetHomeDirectory(test_home_);
        token_path_ = (test_home_ / ".emulator_console_auth_token").string();

        WriteToken("valid_token_123");

        bridge_ = std::make_unique<LegacyConsoleBridge>(5554, token_path_);
    }

    std::unique_ptr<MockConsoleContext> CreateContext(int port = 5554) {
        return std::make_unique<MockConsoleContext>(port);
    }

    void WriteToken(const std::string& token) {
        std::ofstream ofs(token_path_);
        ofs << token;
    }

    android::base::TestTempDir tmpdir_{"LegacyConsoleBridgeTest"};
    android::base::TestSystem test_system_{"/foo/bar"};
    std::filesystem::path test_home_;
    std::string token_path_;
    std::unique_ptr<LegacyConsoleBridge> bridge_;
};

TEST_F(LegacyConsoleBridgeTest, AuthSucceedsWithValidToken) {
    LegacyConsoleBridge::ConsoleContext ctx(5554);
    EXPECT_FALSE(ctx.authenticated);

    auto result = (*bridge_)("auth valid_token_123", ctx);

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(*result, "Android Console: type 'help' for a list of commands");
    EXPECT_TRUE(ctx.authenticated);
}

TEST_F(LegacyConsoleBridgeTest, AuthFailsWithInvalidToken) {
    LegacyConsoleBridge::ConsoleContext ctx(5554);
    EXPECT_FALSE(ctx.authenticated);

    auto result = (*bridge_)("auth wrong_token", ctx);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(result.status().message().find("authentication token does not match") !=
                std::string::npos);
    EXPECT_FALSE(ctx.authenticated);
}

TEST_F(LegacyConsoleBridgeTest, HelpReturnsDifferentListAfterAuth) {
    LegacyConsoleBridge::ConsoleContext ctx(5554);

    auto result_before = (*bridge_)("help", ctx);
    ASSERT_TRUE(result_before.ok());

    auto auth_result = (*bridge_)("auth valid_token_123", ctx);
    ASSERT_TRUE(auth_result.ok());

    auto result_after = (*bridge_)("help", ctx);
    ASSERT_TRUE(result_after.ok());

    EXPECT_NE(*result_before, *result_after);
}

TEST_F(LegacyConsoleBridgeTest, PingFailsWhenNoEmulatorFound) {
    LegacyConsoleBridge::ConsoleContext ctx(5554);
    ctx.authenticated = true;  // Safe to call commands

    auto result = (*bridge_)("ping", ctx);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(LegacyConsoleBridgeTest, PingSucceedsAndReturnsAlive) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("ping", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "I am alive!");
}

TEST_F(LegacyConsoleBridgeTest, GeoFixFailsWhenNoEmulatorFound) {
    LegacyConsoleBridge::ConsoleContext ctx(5554);
    ctx.authenticated = true;  // Safe to call commands

    auto result = (*bridge_)("geo fix 12.3 45.6", ctx);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(LegacyConsoleBridgeTest, GeoFixFailsWithInvalidCoordinates) {
    LegacyConsoleBridge::ConsoleContext ctx(5554);
    ctx.authenticated = true;  // Safe to call commands

    auto result = (*bridge_)("geo fix 200.0 45.6", ctx);  // Invalid longitude

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(result.status().message().find("Longitude must be between -180 and 180") !=
                std::string::npos);

    result = (*bridge_)("geo fix 12.3 100.0", ctx);  // Invalid latitude

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(result.status().message().find("Latitude must be between -90 and 90") !=
                std::string::npos);
}

TEST_F(LegacyConsoleBridgeTest, GeoFixSucceedsWithValidCoordinates) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, setGps(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::GpsState& request,
                         google::protobuf::Empty* response) {
                EXPECT_DOUBLE_EQ(request.longitude(), 12.3);
                EXPECT_DOUBLE_EQ(request.latitude(), 45.6);
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("geo fix 12.3 45.6", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(LegacyConsoleBridgeTest, FingerTouchSendsGrpcRequest) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, sendFingerprint(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::Fingerprint& request,
                         google::protobuf::Empty* response) {
                EXPECT_TRUE(request.istouching());
                EXPECT_EQ(request.touchid(), 1);
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("finger touch 1", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(LegacyConsoleBridgeTest, FingerRemoveSendsGrpcRequest) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, sendFingerprint(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::Fingerprint& request,
                         google::protobuf::Empty* response) {
                EXPECT_FALSE(request.istouching());
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("finger remove", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(LegacyConsoleBridgeTest, HeartbeatFailsWhenNotAuthenticated) {
    LegacyConsoleBridge::ConsoleContext ctx(5554);
    EXPECT_FALSE(ctx.authenticated);

    auto result = (*bridge_)("avd heartbeat", ctx);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
    EXPECT_TRUE(result.status().message().find("unknown command") != std::string::npos);
}

TEST_F(LegacyConsoleBridgeTest, HeartbeatFailsWhenNoEmulatorFound) {
    LegacyConsoleBridge::ConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto result = (*bridge_)("avd heartbeat", ctx);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(LegacyConsoleBridgeTest, HeartbeatSucceedsAndReturnsStatus) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, getStatus(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const google::protobuf::Empty& request,
                         android::emulation::control::EmulatorStatus* response) {
                response->set_heartbeat(42);
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("avd heartbeat", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "heartbeat: 42");
}

TEST_F(LegacyConsoleBridgeTest, NameFailsWhenNoEmulatorFound) {
    LegacyConsoleBridge::ConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto result = (*bridge_)("avd name", ctx);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(LegacyConsoleBridgeTest, NameSucceedsAndReturnsAvdName) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, getStatus(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const google::protobuf::Empty& request,
                         android::emulation::control::EmulatorStatus* response) {
                auto* config = response->mutable_platformconfig();
                (*config)["avd.name"] = "Pixel_9_Pro";
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("avd name", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "Pixel_9_Pro");
}

TEST_F(LegacyConsoleBridgeTest, IdSucceedsAndReturnsAvdId) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, getStatus(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const google::protobuf::Empty& request,
                         android::emulation::control::EmulatorStatus* response) {
                auto* config = response->mutable_platformconfig();
                (*config)["avd.id"] = "avd_id_123";
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("avd id", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "avd_id_123");
}

TEST_F(LegacyConsoleBridgeTest, NameReturnsUnknownIfAvdNameMissingInPlatformConfig) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, getStatus(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const google::protobuf::Empty& request,
                         android::emulation::control::EmulatorStatus* response) {
                auto* config = response->mutable_platformconfig();
                (*config)["other.property"] = "some_value";
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("avd name", *ctx);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInternal);
}

TEST_F(LegacyConsoleBridgeTest, PathSucceedsAndReturnsAvdPath) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, getStatus(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const google::protobuf::Empty& request,
                         android::emulation::control::EmulatorStatus* response) {
                auto* config = response->mutable_platformconfig();
                (*config)["avd.content_path"] = "/path/to/avd";
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("avd path", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "/path/to/avd");
}

TEST_F(LegacyConsoleBridgeTest, PathReturnsErrorIfAvdPathMissingInPlatformConfig) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, getStatus(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const google::protobuf::Empty& request,
                         android::emulation::control::EmulatorStatus* response) {
                auto* config = response->mutable_platformconfig();
                (*config)["other.property"] = "some_value";
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("avd path", *ctx);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInternal);
}

TEST_F(LegacyConsoleBridgeTest, AvdGrpcSucceedsWhenEmulatorIsActive) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    LegacyConsoleBridge::DiscoveredEmulator mock_res;
    mock_res.discovery_file = "/path/to/dummy.ini";
    mock_res.properties["port.serial"] = "5554";
    mock_res.properties["grpc.port"] = "8554";
    ctx->mock_discovery = mock_res;

    auto result = (*bridge_)("avd grpc", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "8554");
}

TEST_F(LegacyConsoleBridgeTest, AvdGrpcFailsWhenNoActiveGrpcService) {
    auto ctx = CreateContext();
    ctx->authenticated = true;
    ctx->mock_discovery = absl::NotFoundError("No matching emulator found");

    auto result = (*bridge_)("avd grpc", *ctx);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(LegacyConsoleBridgeTest, AvdGrpcFailsWhenMultipleEmulatorsMatch) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto file1 = tmpdir_.Path() / "pid_1.ini";
    auto file2 = tmpdir_.Path() / "pid_2.ini";

    std::ofstream ofs1(file1);
    ofs1 << "port.serial=5554\n";
    ofs1 << "grpc.port=8554\n";
    ofs1.close();

    std::ofstream ofs2(file2);
    ofs2 << "port.serial=5554\n";
    ofs2 << "grpc.port=8555\n";
    ofs2.close();

    ctx->mock_discovered_emulators = std::vector<std::filesystem::path>{file1, file2};

    auto result = (*bridge_)("avd grpc", *ctx);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kFailedPrecondition);
    EXPECT_TRUE(result.status().message().find("Multiple matching emulators found") !=
                std::string::npos);
}

TEST_F(LegacyConsoleBridgeTest, KillSucceedsAndCallsSetVmState) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, setVmState(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::VmRunState& request,
                         google::protobuf::Empty* response) {
                EXPECT_EQ(request.state(), android::emulation::control::VmRunState::SHUTDOWN);
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("kill", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(LegacyConsoleBridgeTest, ResumeSucceedsAndCallsSetVmState) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, setVmState(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::VmRunState& request,
                         google::protobuf::Empty* response) {
                EXPECT_EQ(request.state(), android::emulation::control::VmRunState::RUNNING);
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("avd resume", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(LegacyConsoleBridgeTest, CreatesTokenFileIfMissingOnConstruction) {
    std::filesystem::remove(token_path_);
    EXPECT_FALSE(std::filesystem::exists(token_path_));

    auto new_bridge = std::make_unique<LegacyConsoleBridge>(5554, token_path_);

    EXPECT_TRUE(std::filesystem::exists(token_path_));
    EXPECT_EQ(TelnetAuth::GetStatus(token_path_), AuthStatus::kRequired);

    auto read_result = TelnetAuth::ReadToken(token_path_);
    ASSERT_TRUE(read_result.ok()) << read_result.status();
    EXPECT_FALSE(read_result->AsStringView().empty());
}

TEST_F(LegacyConsoleBridgeTest, CreateContextRespectsDisabledAuth) {
    WriteToken("");
    EXPECT_EQ(TelnetAuth::GetStatus(token_path_), AuthStatus::kDisabled);

    auto ctx = bridge_->CreateContext();
    EXPECT_TRUE(ctx->authenticated);
}

TEST_F(LegacyConsoleBridgeTest, WelcomeMessageDoesNotRequireAuthWhenDisabled) {
    WriteToken("");
    auto ctx = bridge_->CreateContext();
    auto welcome = bridge_->WelcomeMessage(*ctx);

    EXPECT_EQ(welcome, "Android Console: type 'help' for a list of commands\r\n");
}

TEST_F(LegacyConsoleBridgeTest, RotateAdvancesClockwise) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, getPhysicalModel(_, _, _))
            .WillOnce(
                    [](grpc::ClientContext* context,
                       const android::emulation::control::PhysicalModelValue& request,
                       android::emulation::control::PhysicalModelValue* response) -> grpc::Status {
                        EXPECT_EQ(request.target(),
                                  android::emulation::control::PhysicalModelValue::ROTATION);
                        response->set_target(
                                android::emulation::control::PhysicalModelValue::ROTATION);
                        response->mutable_value()->add_data(0.0f);
                        response->mutable_value()->add_data(0.0f);
                        response->mutable_value()->add_data(0.0f);  // Current is portrait (0 deg)
                        return grpc::Status::OK;
                    });
    EXPECT_CALL(*mock_stub, setPhysicalModel(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::PhysicalModelValue& request,
                         google::protobuf::Empty* response) -> grpc::Status {
                EXPECT_EQ(request.target(),
                          android::emulation::control::PhysicalModelValue::ROTATION);
                EXPECT_EQ(request.value().data_size(), 3);
                EXPECT_FLOAT_EQ(request.value().data(2), -90.0f);  // Clockwise is -90 deg
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("rotate", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(LegacyConsoleBridgeTest, FoldSetsClosedPosture) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, setPhysicalModel(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::PhysicalModelValue& request,
                         google::protobuf::Empty* response) -> grpc::Status {
                EXPECT_EQ(request.target(),
                          android::emulation::control::PhysicalModelValue::POSTURE);
                EXPECT_EQ(request.value().data_size(), 1);
                EXPECT_FLOAT_EQ(request.value().data(0), 1.0f);  // Closed posture
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("fold", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(LegacyConsoleBridgeTest, UnfoldSetsOpenedPosture) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, setPhysicalModel(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::PhysicalModelValue& request,
                         google::protobuf::Empty* response) -> grpc::Status {
                EXPECT_EQ(request.target(),
                          android::emulation::control::PhysicalModelValue::POSTURE);
                EXPECT_EQ(request.value().data_size(), 1);
                EXPECT_FLOAT_EQ(request.value().data(0), 3.0f);  // Opened posture
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("unfold", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(LegacyConsoleBridgeTest, PostureSetsSpecificPosture) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, setPhysicalModel(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::PhysicalModelValue& request,
                         google::protobuf::Empty* response) -> grpc::Status {
                EXPECT_EQ(request.target(),
                          android::emulation::control::PhysicalModelValue::POSTURE);
                EXPECT_EQ(request.value().data_size(), 1);
                EXPECT_FLOAT_EQ(request.value().data(0), 2.0f);  // Half-opened posture
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("posture 2", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(LegacyConsoleBridgeTest, PostureFailsOnMissingArgs) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto result = (*bridge_)("posture", *ctx);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(result.status().message().find("Usage: \"posture <posture_id>\"") !=
                std::string::npos);
}

TEST_F(LegacyConsoleBridgeTest, PostureFailsOnInvalidPosture) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto result = (*bridge_)("posture 99", *ctx);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(result.status().message().find("Usage: \"posture <posture_id>\"") !=
                std::string::npos);
}

TEST_F(LegacyConsoleBridgeTest, AvdSnapshotsPathQueriesPath) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, getStatus(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const google::protobuf::Empty& request,
                         android::emulation::control::EmulatorStatus* response) -> grpc::Status {
                auto* config = response->mutable_platformconfig();
                (*config)["avd.content_path"] = "/path/to/avd";
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("avd snapshotspath", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, (std::filesystem::path("/path/to/avd") / "snapshots").string());
}

TEST_F(LegacyConsoleBridgeTest, AvdSnapshotPathQueriesSpecificPath) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, getStatus(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const google::protobuf::Empty& request,
                         android::emulation::control::EmulatorStatus* response) -> grpc::Status {
                auto* config = response->mutable_platformconfig();
                (*config)["avd.content_path"] = "/path/to/avd";
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("avd snapshotpath snap1", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, (std::filesystem::path("/path/to/avd") / "snapshots" / "snap1").string());
}

TEST_F(LegacyConsoleBridgeTest, AvdSnapshotListReturnsFormattedList) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockSnapshotServiceStub>();
    EXPECT_CALL(*mock_stub, ListSnapshots(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::SnapshotFilter& request,
                         android::emulation::control::SnapshotList* response) -> grpc::Status {
                EXPECT_EQ(request.statusfilter(), android::emulation::control::SnapshotFilter::All);
                auto* snap1 = response->add_snapshots();
                snap1->set_snapshot_id("1");
                snap1->mutable_details()->set_logical_name("default_boot");
                auto* snap2 = response->add_snapshots();
                snap2->set_snapshot_id("checkpoint1");
                return grpc::Status::OK;
            });
    ctx->mock_snapshot_stub = std::move(mock_stub);

    auto result = (*bridge_)("avd snapshot list", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result,
              "List of snapshots present on all disks:\r\ndefault_boot\r\ncheckpoint1\r\n");
}

TEST_F(LegacyConsoleBridgeTest, AvdSnapshotListReturnsNoSnapshotAvailableWhenEmpty) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockSnapshotServiceStub>();
    EXPECT_CALL(*mock_stub, ListSnapshots(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::SnapshotFilter& request,
                         android::emulation::control::SnapshotList* response) -> grpc::Status {
                return grpc::Status::OK;
            });
    ctx->mock_snapshot_stub = std::move(mock_stub);

    auto result = (*bridge_)("avd snapshot list", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "There is no snapshot available.");
}

TEST_F(LegacyConsoleBridgeTest, AvdSnapshotSaveCallsSnapshotService) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockSnapshotServiceStub>();
    EXPECT_CALL(*mock_stub, SaveSnapshot(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::SnapshotPackage& request,
                         android::emulation::control::SnapshotPackage* response) -> grpc::Status {
                EXPECT_EQ(request.snapshot_id(), "my_snap");
                response->set_success(true);
                return grpc::Status::OK;
            });
    ctx->mock_snapshot_stub = std::move(mock_stub);

    auto result = (*bridge_)("avd snapshot save my_snap", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(LegacyConsoleBridgeTest, AvdSnapshotLoadCallsSnapshotService) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockSnapshotServiceStub>();
    EXPECT_CALL(*mock_stub, LoadSnapshot(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::SnapshotPackage& request,
                         android::emulation::control::SnapshotPackage* response) -> grpc::Status {
                EXPECT_EQ(request.snapshot_id(), "my_snap");
                response->set_success(true);
                return grpc::Status::OK;
            });
    ctx->mock_snapshot_stub = std::move(mock_stub);

    auto result = (*bridge_)("avd snapshot load my_snap", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(LegacyConsoleBridgeTest, AvdSnapshotDeleteCallsSnapshotService) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockSnapshotServiceStub>();
    EXPECT_CALL(*mock_stub, DeleteSnapshot(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::SnapshotPackage& request,
                         android::emulation::control::SnapshotPackage* response) -> grpc::Status {
                EXPECT_EQ(request.snapshot_id(), "my_snap");
                response->set_success(true);
                return grpc::Status::OK;
            });
    ctx->mock_snapshot_stub = std::move(mock_stub);

    auto result = (*bridge_)("avd snapshot del my_snap", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(LegacyConsoleBridgeTest, SensorStatusReturnsFormattedList) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, getSensor(_, _, _))
            .Times(testing::AtLeast(1))
            .WillRepeatedly([](grpc::ClientContext* context,
                               const android::emulation::control::SensorValue& request,
                               android::emulation::control::SensorValue* response) -> grpc::Status {
                response->set_target(request.target());
                response->set_status(android::emulation::control::SensorValue::OK);
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("sensor status", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_TRUE(result->find("acceleration: enabled.\r\n") != std::string::npos);
    EXPECT_TRUE(result->find("gyroscope: enabled.\r\n") != std::string::npos);
    EXPECT_TRUE(result->find("light: enabled.\r\n") != std::string::npos);
}

TEST_F(LegacyConsoleBridgeTest, SensorGetReturnsFormattedValues) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, getSensor(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::SensorValue& request,
                         android::emulation::control::SensorValue* response) -> grpc::Status {
                EXPECT_EQ(request.target(), android::emulation::control::SensorValue::ACCELERATION);
                response->set_target(request.target());
                response->mutable_value()->add_data(0.0f);
                response->mutable_value()->add_data(9.81f);
                response->mutable_value()->add_data(0.0f);
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("sensor get acceleration", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "acceleration = 0:9.81:0");
}

TEST_F(LegacyConsoleBridgeTest, SensorGetSingleValue) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, getSensor(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::SensorValue& request,
                         android::emulation::control::SensorValue* response) -> grpc::Status {
                EXPECT_EQ(request.target(), android::emulation::control::SensorValue::LIGHT);
                response->set_target(request.target());
                response->mutable_value()->add_data(100.0f);
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("sensor get light", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "light = 100");
}

TEST_F(LegacyConsoleBridgeTest, SensorGetFailsOnMissingArgs) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto result = (*bridge_)("sensor get", *ctx);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(result.status().message().find("Usage: \"get <sensorname>\"") != std::string::npos);
}

TEST_F(LegacyConsoleBridgeTest, SensorGetFailsOnUnknownSensor) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto result = (*bridge_)("sensor get nonexistent_sensor", *ctx);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
    EXPECT_TRUE(result.status().message().find("unknown sensor name: nonexistent_sensor") !=
                std::string::npos);
}

TEST_F(LegacyConsoleBridgeTest, SensorSetSendsValues) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, setSensor(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::SensorValue& request,
                         google::protobuf::Empty* response) -> grpc::Status {
                EXPECT_EQ(request.target(), android::emulation::control::SensorValue::ACCELERATION);
                EXPECT_EQ(request.value().data_size(), 3);
                EXPECT_FLOAT_EQ(request.value().data(0), 0.0f);
                EXPECT_FLOAT_EQ(request.value().data(1), 9.81f);
                EXPECT_FLOAT_EQ(request.value().data(2), 0.0f);
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("sensor set acceleration 0 9.81 0", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(LegacyConsoleBridgeTest, SensorSetColonSeparatedValues) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, setSensor(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::SensorValue& request,
                         google::protobuf::Empty* response) -> grpc::Status {
                EXPECT_EQ(request.target(), android::emulation::control::SensorValue::ACCELERATION);
                EXPECT_EQ(request.value().data_size(), 3);
                EXPECT_FLOAT_EQ(request.value().data(0), 1.0f);
                EXPECT_FLOAT_EQ(request.value().data(1), 2.0f);
                EXPECT_FLOAT_EQ(request.value().data(2), 3.0f);
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("sensor set acceleration 1:2:3", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(LegacyConsoleBridgeTest, SensorSetFailsOnMissingArgs) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto result = (*bridge_)("sensor set", *ctx);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(result.status().message().find("Usage: \"set <sensorname>") != std::string::npos);
}

TEST_F(LegacyConsoleBridgeTest, SensorSetFailsOnMissingValues) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto result = (*bridge_)("sensor set acceleration", *ctx);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(result.status().message().find("Usage: \"set <sensorname>") != std::string::npos);
}

TEST_F(LegacyConsoleBridgeTest, SensorSetFailsOnUnknownSensor) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto result = (*bridge_)("sensor set nonexistent_sensor 1 2 3", *ctx);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
    EXPECT_TRUE(result.status().message().find("unknown sensor name: nonexistent_sensor") !=
                std::string::npos);
}

TEST_F(LegacyConsoleBridgeTest, AllSensorsGetAndSetRoundTrip) {
    const std::vector<std::pair<std::string, std::vector<float>>> kTestSensors = {
        {"acceleration", {1.0f, 2.0f, 3.0f}},
        {"gyroscope", {0.1f, 0.2f, 0.3f}},
        {"magnetic-field", {10.0f, 20.0f, 30.0f}},
        {"orientation", {45.0f, 90.0f, 180.0f}},
        {"temperature", {25.5f}},
        {"proximity", {5.0f}},
        {"light", {300.0f}},
        {"pressure", {1013.25f}},
        {"humidity", {55.0f}},
        {"magnetic-field-uncalibrated", {1.0f, 2.0f, 3.0f}},
        {"gyroscope-uncalibrated", {0.4f, 0.5f, 0.6f}},
        {"hinge-angle0", {90.0f}},
        {"hinge-angle1", {120.0f}},
        {"hinge-angle2", {150.0f}},
        {"heart-rate", {72.0f}},
        {"rgbc-light", {100.0f, 150.0f, 200.0f, 250.0f}},
        {"wrist-tilt", {1.0f}},
        {"acceleration-uncalibrated", {4.0f, 5.0f, 6.0f}},
    };

    for (const auto& [name, values] : kTestSensors) {
        // Test SET
        {
            auto ctx = CreateContext();
            ctx->authenticated = true;
            auto mock_stub =
                    std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
            EXPECT_CALL(*mock_stub, setSensor(_, _, _))
                    .WillOnce([&values](grpc::ClientContext* context,
                                        const android::emulation::control::SensorValue& request,
                                        google::protobuf::Empty* response) -> grpc::Status {
                        EXPECT_EQ(request.value().data_size(), values.size());
                        for (size_t i = 0; i < values.size(); ++i) {
                            EXPECT_FLOAT_EQ(request.value().data(i), values[i]);
                        }
                        return grpc::Status::OK;
                    });
            ctx->mock_stub = std::move(mock_stub);

            std::string set_cmd = absl::StrCat("sensor set ", name, " ");
            for (size_t i = 0; i < values.size(); ++i) {
                absl::StrAppend(&set_cmd, values[i], (i == values.size() - 1) ? "" : ":");
            }
            auto set_res = (*bridge_)(set_cmd, *ctx);
            ASSERT_TRUE(set_res.ok()) << set_res.status().message();
        }

        // Test GET
        {
            auto ctx = CreateContext();
            ctx->authenticated = true;
            auto mock_stub =
                    std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
            EXPECT_CALL(*mock_stub, getSensor(_, _, _))
                    .WillOnce([&values](grpc::ClientContext* context,
                                        const android::emulation::control::SensorValue& request,
                                        android::emulation::control::SensorValue* response)
                                      -> grpc::Status {
                        response->set_target(request.target());
                        for (float v : values) {
                            response->mutable_value()->add_data(v);
                        }
                        return grpc::Status::OK;
                    });
            ctx->mock_stub = std::move(mock_stub);

            auto get_res = (*bridge_)(absl::StrCat("sensor get ", name), *ctx);
            ASSERT_TRUE(get_res.ok()) << get_res.status().message();
        }
    }
}

}  // namespace
}  // namespace goldfish::telnet
