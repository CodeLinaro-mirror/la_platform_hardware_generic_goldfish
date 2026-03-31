// Copyright 2026 The Android Open Source Project
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

#include "android/emulation/control/ui_controller_forwarder.h"

#include <gmock/gmock.h>
#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include "android/emulation/control/service_forwarder_impl.h"
#include "service_forwarder.grpc.pb.h"
#include "ui_controller_service.grpc.pb.h"

namespace android::emulation::forwarding {

using ::android::emulation::control::ExtendedControlsStatus;
using ::android::emulation::control::PaneEntry;
using ::android::emulation::control::ThemingStyle;
using ::android::emulation::control::UiController;
using ::android::emulation::control::UserConfig;
using ::google::protobuf::Empty;
using ::grpc::Server;
using ::grpc::ServerBuilder;
using ::grpc::ServerContext;
using ::grpc::Status;
using ::grpc::StatusCode;
using ::testing::_;
using ::testing::Return;

// Mock Backend Service
class MockUiControllerService : public UiController::Service {
  public:
    MOCK_METHOD(Status, showExtendedControls,
                (ServerContext * context, const PaneEntry* request, ExtendedControlsStatus* reply),
                (override));
    MOCK_METHOD(Status, closeExtendedControls,
                (ServerContext * context, const Empty* request, ExtendedControlsStatus* reply),
                (override));
    MOCK_METHOD(Status, setUiTheme,
                (ServerContext * context, const ThemingStyle* request, Empty* reply), (override));
    MOCK_METHOD(Status, getUserConfig,
                (ServerContext * context, const Empty* request, UserConfig* reply), (override));
};

class UiControllerForwarderTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // 1. Start Mock Server
        ServerBuilder builder;
        int port;
        builder.AddListeningPort("localhost:0", grpc::InsecureServerCredentials(), &port);
        builder.RegisterService(&mock_service_);
        server_ = builder.BuildAndStart();
        ASSERT_NE(server_, nullptr);

        backend_target_ = "localhost:" + std::to_string(port);

        // 2. Setup Forwarder
        service_forwarder_ = std::make_shared<ServiceForwarderImpl>();
        ui_forwarder_ = std::make_shared<UiControllerForwarder>(service_forwarder_);

        // 3. Register Backend
        ForwardingRule rule;
        rule.set_service_uri("android.emulation.control.UiController");
        rule.mutable_endpoint()->set_target(backend_target_);
        service_forwarder_->registerForwarder(nullptr, &rule, nullptr);
    }

    void TearDown() override {
        if (server_) {
            server_->Shutdown();
            server_->Wait();
        }
    }

    MockUiControllerService mock_service_;
    std::unique_ptr<Server> server_;
    std::string backend_target_;
    std::shared_ptr<ServiceForwarderImpl> service_forwarder_;
    std::shared_ptr<UiControllerForwarder> ui_forwarder_;
};

TEST_F(UiControllerForwarderTest, ShowExtendedControls_ForwardsRequest) {
    PaneEntry request;
    request.set_index(PaneEntry::BATTERY);
    ExtendedControlsStatus response;

    EXPECT_CALL(mock_service_, showExtendedControls(_, _, _))
            .WillOnce([&](ServerContext*, const PaneEntry* req, ExtendedControlsStatus* rep) {
                EXPECT_EQ(req->index(), PaneEntry::BATTERY);
                rep->set_visibilitychanged(true);
                return Status::OK;
            });

    Status status = ui_forwarder_->showExtendedControls(nullptr, &request, &response);
    EXPECT_TRUE(status.ok());
    EXPECT_TRUE(response.visibilitychanged());
}

TEST_F(UiControllerForwarderTest, CloseExtendedControls_ForwardsRequest) {
    Empty request;
    ExtendedControlsStatus response;

    EXPECT_CALL(mock_service_, closeExtendedControls(_, _, _)).WillOnce(Return(Status::OK));

    Status status = ui_forwarder_->closeExtendedControls(nullptr, &request, &response);
    EXPECT_TRUE(status.ok());
}

TEST_F(UiControllerForwarderTest, SetUiTheme_ForwardsRequest) {
    ThemingStyle request;
    request.set_style(ThemingStyle::DARK);
    Empty response;

    EXPECT_CALL(mock_service_, setUiTheme(_, _, _))
            .WillOnce([&](ServerContext*, const ThemingStyle* req, Empty*) {
                EXPECT_EQ(req->style(), ThemingStyle::DARK);
                return Status::OK;
            });

    Status status = ui_forwarder_->setUiTheme(nullptr, &request, &response);
    EXPECT_TRUE(status.ok());
}

TEST_F(UiControllerForwarderTest, GetUserConfig_ForwardsRequest) {
    Empty request;
    UserConfig response;

    EXPECT_CALL(mock_service_, getUserConfig(_, _, _))
            .WillOnce([&](ServerContext*, const Empty*, UserConfig* rep) {
                auto* entry = rep->add_entries();
                entry->set_key("foo");
                entry->set_value("bar");
                return Status::OK;
            });

    Status status = ui_forwarder_->getUserConfig(nullptr, &request, &response);
    EXPECT_TRUE(status.ok());
    ASSERT_EQ(response.entries_size(), 1);
    EXPECT_EQ(response.entries(0).key(), "foo");
    EXPECT_EQ(response.entries(0).value(), "bar");
}

TEST_F(UiControllerForwarderTest, NoEndpointRegistered_ReturnsUnimplemented) {
    // Override with empty registry
    service_forwarder_ = std::make_shared<ServiceForwarderImpl>();
    ui_forwarder_ = std::make_shared<UiControllerForwarder>(service_forwarder_);

    PaneEntry request;
    ExtendedControlsStatus response;

    Status status = ui_forwarder_->showExtendedControls(nullptr, &request, &response);
    EXPECT_EQ(status.error_code(), StatusCode::UNIMPLEMENTED);
}

}  // namespace android::emulation::forwarding
