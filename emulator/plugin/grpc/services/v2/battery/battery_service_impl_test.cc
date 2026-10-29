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

#include "battery_service_impl.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "battery_service.grpc.pb.h"
#include "test/grpc_v2_service_test.h"

namespace goldfish::grpc::v2 {
namespace {

using ::android::emulation::v2::BatteryService;
using ::android::emulation::v2::BatteryState;
using ::android::emulation::v2::ChargerSource;
using ::android::emulation::v2::GetBatteryStateRequest;
using ::android::emulation::v2::StreamBatteryStateRequest;
using ::android::emulation::v2::UpdateBatteryStateRequest;
using ::goldfish::avd_universe::battery::Battery;
using ::goldfish::avd_universe::battery::ObservableBattery;
using ::grpc::ClientContext;
using ::grpc::ServerContext;
using ::grpc::StatusCode;
using ::testing::HasSubstr;

class BatteryServiceImplTest : public ::testing::Test {
  protected:
    void SetUp() override {
        observable_battery_.SetValue(Battery{
            .has_battery = true,
            .is_present = true,
            .charger = Battery::Charger::kAc,
            .charge_level = 100,
            .health = Battery::Health::kGood,
            .status = Battery::Status::kCharging,
        });
        service_ = std::make_unique<BatteryServiceImpl>(observable_battery_);
    }

    ObservableBattery observable_battery_;
    std::unique_ptr<BatteryServiceImpl> service_;
};

TEST_F(BatteryServiceImplTest, GetBatteryStateReturnsInitialSnapshot) {
    ServerContext ctx;
    GetBatteryStateRequest request;
    BatteryState reply;

    auto status = service_->GetBatteryState(&ctx, &request, &reply);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(reply.level_percent(), 100);
    EXPECT_EQ(reply.charger(), ChargerSource::CHARGER_SOURCE_AC);
    EXPECT_EQ(reply.health(), BatteryState::GOOD);
    EXPECT_EQ(reply.state(), BatteryState::CHARGING);
}

TEST_F(BatteryServiceImplTest, GetBatteryStateReflectsObservableChanges) {
    observable_battery_.SetValue(Battery{
        .has_battery = true,
        .is_present = true,
        .charger = Battery::Charger::kNone,
        .charge_level = 42,
        .health = Battery::Health::kOverheated,
        .status = Battery::Status::kDischarging,
    });

    ServerContext ctx;
    GetBatteryStateRequest request;
    BatteryState reply;

    auto status = service_->GetBatteryState(&ctx, &request, &reply);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(reply.level_percent(), 42);
    EXPECT_EQ(reply.charger(), ChargerSource::CHARGER_SOURCE_NONE);
    EXPECT_EQ(reply.health(), BatteryState::OVERHEAT);
    EXPECT_EQ(reply.state(), BatteryState::DISCHARGING);
}

TEST_F(BatteryServiceImplTest, UpdateBatteryStatePartialLevelOnly) {
    ServerContext ctx;
    UpdateBatteryStateRequest request;
    request.set_level_percent(55);
    BatteryState reply;

    auto status = service_->UpdateBatteryState(&ctx, &request, &reply);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(reply.level_percent(), 55);
    // Unset fields in request remain unchanged from initial snapshot
    EXPECT_EQ(reply.charger(), ChargerSource::CHARGER_SOURCE_AC);
    EXPECT_EQ(reply.health(), BatteryState::GOOD);
    EXPECT_EQ(reply.state(), BatteryState::CHARGING);

    EXPECT_EQ(observable_battery_.GetValue().charge_level, 55);
}

TEST_F(BatteryServiceImplTest, UpdateBatteryStatePartialCharger) {
    ServerContext ctx;
    UpdateBatteryStateRequest request;
    request.set_charger(ChargerSource::CHARGER_SOURCE_NONE);
    BatteryState reply;

    auto status = service_->UpdateBatteryState(&ctx, &request, &reply);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(reply.charger(), ChargerSource::CHARGER_SOURCE_NONE);
    EXPECT_EQ(reply.level_percent(), 100);

    EXPECT_EQ(observable_battery_.GetValue().charger, Battery::Charger::kNone);
}

TEST_F(BatteryServiceImplTest, UpdateBatteryStatePartialState) {
    ServerContext ctx;
    UpdateBatteryStateRequest request;
    request.set_state(BatteryState::NOT_CHARGING);
    BatteryState reply;

    auto status = service_->UpdateBatteryState(&ctx, &request, &reply);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(reply.state(), BatteryState::NOT_CHARGING);

    EXPECT_EQ(observable_battery_.GetValue().status, Battery::Status::kNotCharging);
}

TEST_F(BatteryServiceImplTest, UpdateBatteryStatePartialHealth) {
    ServerContext ctx;
    UpdateBatteryStateRequest request;
    request.set_health(BatteryState::DEAD);
    BatteryState reply;

    auto status = service_->UpdateBatteryState(&ctx, &request, &reply);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(reply.health(), BatteryState::DEAD);

    EXPECT_EQ(observable_battery_.GetValue().health, Battery::Health::kDead);
}

TEST_F(BatteryServiceImplTest, UpdateBatteryStateFullMutation) {
    ServerContext ctx;
    UpdateBatteryStateRequest request;
    request.set_level_percent(0);
    request.set_charger(ChargerSource::CHARGER_SOURCE_NONE);
    request.set_health(BatteryState::OVER_VOLTAGE);
    request.set_state(BatteryState::DISCHARGING);
    BatteryState reply;

    auto status = service_->UpdateBatteryState(&ctx, &request, &reply);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(reply.level_percent(), 0);
    EXPECT_EQ(reply.charger(), ChargerSource::CHARGER_SOURCE_NONE);
    EXPECT_EQ(reply.health(), BatteryState::OVER_VOLTAGE);
    EXPECT_EQ(reply.state(), BatteryState::DISCHARGING);

    auto val = observable_battery_.GetValue();
    EXPECT_EQ(val.charge_level, 0);
    EXPECT_EQ(val.charger, Battery::Charger::kNone);
    EXPECT_EQ(val.health, Battery::Health::kOvervoltage);
    EXPECT_EQ(val.status, Battery::Status::kDischarging);
}

TEST_F(BatteryServiceImplTest, UpdateBatteryStateRejectsUnsupportedChargersAndHealth) {
    for (ChargerSource unsupported :
         {ChargerSource::CHARGER_SOURCE_USB, ChargerSource::CHARGER_SOURCE_WIRELESS,
          ChargerSource::CHARGER_SOURCE_DOCK}) {
        ServerContext ctx;
        UpdateBatteryStateRequest request;
        request.set_level_percent(25);
        request.set_charger(unsupported);
        BatteryState reply;

        auto status = service_->UpdateBatteryState(&ctx, &request, &reply);
        EXPECT_EQ(status.error_code(), StatusCode::FAILED_PRECONDITION);
        EXPECT_THAT(status.error_message(), HasSubstr("supported_chargers"));
        // Failed validation must not partially mutate state.
        EXPECT_EQ(observable_battery_.GetValue().charge_level, 100);
        EXPECT_EQ(observable_battery_.GetValue().charger, Battery::Charger::kAc);
    }

    {
        ServerContext ctx;
        UpdateBatteryStateRequest request;
        request.set_level_percent(25);
        request.set_health(BatteryState::COLD);
        BatteryState reply;

        auto status = service_->UpdateBatteryState(&ctx, &request, &reply);
        EXPECT_EQ(status.error_code(), StatusCode::FAILED_PRECONDITION);
        EXPECT_THAT(status.error_message(), HasSubstr("COLD"));
        EXPECT_EQ(observable_battery_.GetValue().charge_level, 100);
        EXPECT_EQ(observable_battery_.GetValue().health, Battery::Health::kGood);
    }
}

TEST_F(BatteryServiceImplTest, UpdateBatteryStateRejectsNegativeAndOverHundredLevel) {
    for (int32_t invalid_level : {-1, 101}) {
        ServerContext ctx;
        UpdateBatteryStateRequest request;
        request.set_level_percent(invalid_level);
        BatteryState reply;

        auto status = service_->UpdateBatteryState(&ctx, &request, &reply);
        EXPECT_EQ(status.error_code(), StatusCode::INVALID_ARGUMENT);
        EXPECT_THAT(status.error_message(), HasSubstr("level_percent must be in range [0, 100]"));
    }
}

TEST_F(BatteryServiceImplTest, UpdateBatteryStateRejectsUnspecifiedAndInvalidEnums) {
    for (BatteryState::State bad_state :
         {BatteryState::STATE_UNSPECIFIED, static_cast<BatteryState::State>(999)}) {
        ServerContext ctx;
        UpdateBatteryStateRequest request;
        request.set_state(bad_state);
        BatteryState reply;
        auto status = service_->UpdateBatteryState(&ctx, &request, &reply);
        EXPECT_EQ(status.error_code(), StatusCode::INVALID_ARGUMENT);
    }
    for (ChargerSource bad_charger :
         {ChargerSource::CHARGER_SOURCE_UNSPECIFIED, static_cast<ChargerSource>(999)}) {
        ServerContext ctx;
        UpdateBatteryStateRequest request;
        request.set_charger(bad_charger);
        BatteryState reply;
        auto status = service_->UpdateBatteryState(&ctx, &request, &reply);
        EXPECT_EQ(status.error_code(), StatusCode::INVALID_ARGUMENT);
    }
    for (BatteryState::Health bad_health :
         {BatteryState::HEALTH_UNSPECIFIED, static_cast<BatteryState::Health>(999)}) {
        ServerContext ctx;
        UpdateBatteryStateRequest request;
        request.set_health(bad_health);
        BatteryState reply;
        auto status = service_->UpdateBatteryState(&ctx, &request, &reply);
        EXPECT_EQ(status.error_code(), StatusCode::INVALID_ARGUMENT);
    }
}

TEST_F(BatteryServiceImplTest, UpdateBatteryStateEnforcesReadOnlyOption) {
    BatteryServiceImpl read_only_service(observable_battery_,
                                         BatteryServiceImpl::Options{.read_only = true});
    GetBatteryStateRequest get_req;
    BatteryState get_reply;
    ServerContext get_ctx;
    EXPECT_TRUE(read_only_service.GetBatteryState(&get_ctx, &get_req, &get_reply).ok());

    ServerContext update_ctx;
    UpdateBatteryStateRequest update_req;
    update_req.set_level_percent(50);
    BatteryState update_reply;

    auto status = read_only_service.UpdateBatteryState(&update_ctx, &update_req, &update_reply);
    EXPECT_EQ(status.error_code(), StatusCode::PERMISSION_DENIED);
    EXPECT_THAT(status.error_message(), HasSubstr("ACCESS_MODE_READ_ONLY"));
}

TEST_F(BatteryServiceImplTest, RejectsAllRpcsWhenDeviceHasNoBatterySubsystem) {
    BatteryServiceImpl batteryless_service(observable_battery_,
                                           BatteryServiceImpl::Options{.has_battery = false});
    ::grpc::ServerBuilder builder;
    builder.RegisterService(&batteryless_service);
    auto server = builder.BuildAndStart();
    ASSERT_NE(server, nullptr);
    auto stub = BatteryService::NewStub(server->InProcessChannel(::grpc::ChannelArguments()));

    {
        ClientContext ctx;
        GetBatteryStateRequest req;
        BatteryState reply;
        auto status = stub->GetBatteryState(&ctx, req, &reply);
        EXPECT_EQ(status.error_code(), StatusCode::FAILED_PRECONDITION);
        EXPECT_THAT(status.error_message(), HasSubstr("no battery subsystem"));
    }
    {
        ClientContext ctx;
        UpdateBatteryStateRequest req;
        req.set_level_percent(50);
        BatteryState reply;
        auto status = stub->UpdateBatteryState(&ctx, req, &reply);
        EXPECT_EQ(status.error_code(), StatusCode::FAILED_PRECONDITION);
        EXPECT_THAT(status.error_message(), HasSubstr("no battery subsystem"));
    }
    {
        ClientContext ctx;
        ctx.set_deadline(absl::ToChronoTime(absl::Now() + absl::Seconds(5)));
        StreamBatteryStateRequest req;
        auto reader = stub->StreamBatteryState(&ctx, req);
        BatteryState state;
        EXPECT_FALSE(reader->Read(&state));
        auto status = reader->Finish();
        EXPECT_EQ(status.error_code(), StatusCode::FAILED_PRECONDITION);
        EXPECT_THAT(status.error_message(), HasSubstr("no battery subsystem"));
    }

    server->Shutdown(absl::ToChronoTime(absl::Now() + absl::Seconds(1)));
    server->Wait();
}

TEST_F(BatteryServiceImplTest, StreamBatteryStateInitialSnapshotAndReactiveUpdates) {
    auto* writer = service_->CreateBatteryStateWriter<test::FakeWriteReactor<BatteryState>>();

    // 1. Initial snapshot delivered immediately upon subscription
    ASSERT_EQ(writer->written_messages.size(), 1);
    EXPECT_EQ(writer->written_messages[0].level_percent(), 100);
    EXPECT_EQ(writer->written_messages[0].charger(), ChargerSource::CHARGER_SOURCE_AC);
    EXPECT_EQ(writer->written_messages[0].health(), BatteryState::GOOD);
    EXPECT_EQ(writer->written_messages[0].state(), BatteryState::CHARGING);
    writer->OnWriteDone(true);

    // 2. Reactive update via observable SetValue
    observable_battery_.SetValue(Battery{
        .has_battery = true,
        .is_present = true,
        .charger = Battery::Charger::kNone,
        .charge_level = 73,
        .health = Battery::Health::kGood,
        .status = Battery::Status::kDischarging,
    });

    ASSERT_EQ(writer->written_messages.size(), 2);
    EXPECT_EQ(writer->written_messages[1].level_percent(), 73);
    EXPECT_EQ(writer->written_messages[1].charger(), ChargerSource::CHARGER_SOURCE_NONE);
    EXPECT_EQ(writer->written_messages[1].state(), BatteryState::DISCHARGING);
    writer->OnWriteDone(true);

    // 3. Reactive update via UpdateBatteryState RPC
    ServerContext ctx;
    UpdateBatteryStateRequest req;
    req.set_level_percent(70);
    req.set_health(BatteryState::OVERHEAT);
    BatteryState reply;
    auto status = service_->UpdateBatteryState(&ctx, &req, &reply);
    EXPECT_TRUE(status.ok());

    ASSERT_EQ(writer->written_messages.size(), 3);
    EXPECT_EQ(writer->written_messages[2].level_percent(), 70);
    EXPECT_EQ(writer->written_messages[2].health(), BatteryState::OVERHEAT);
    EXPECT_EQ(writer->written_messages[2].charger(), ChargerSource::CHARGER_SOURCE_NONE);

    writer->OnDone();
}

TEST_F(BatteryServiceImplTest, StreamBatteryStateCoalescesRapidUpdatesForSlowClients) {
    auto* writer = service_->CreateBatteryStateWriter<test::FakeWriteReactor<BatteryState>>();
    ASSERT_EQ(writer->written_messages.size(), 1);
    EXPECT_EQ(writer->written_messages[0].level_percent(), 100);

    // Do NOT call OnWriteDone yet: simulate an in-flight write to a slow client while
    // three rapid updates arrive. Only the latest state (60%) should remain queued.
    for (int32_t level : {80, 70, 60}) {
        ServerContext ctx;
        UpdateBatteryStateRequest req;
        req.set_level_percent(level);
        BatteryState reply;
        ASSERT_TRUE(service_->UpdateBatteryState(&ctx, &req, &reply).ok());
    }

    EXPECT_EQ(writer->written_messages.size(), 1);
    writer->OnWriteDone(true);

    ASSERT_EQ(writer->written_messages.size(), 2);
    EXPECT_EQ(writer->written_messages[1].level_percent(), 60);
    writer->OnWriteDone(true);
    writer->OnDone();
}

TEST_F(BatteryServiceImplTest, StreamBatteryStateConcurrentUpdatesAndCancelAreRaceFree) {
    auto* writer = service_->CreateBatteryStateWriter<test::FakeWriteReactor<BatteryState>>();
    std::atomic<bool> start{false};

    constexpr int kProducerThreads = 4;
    constexpr int kUpdatesPerThread = 25;
    std::vector<std::thread> threads;
    threads.reserve(kProducerThreads + 1);

    for (int t = 0; t < kProducerThreads; ++t) {
        threads.emplace_back([this, t, &start]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int i = 0; i < kUpdatesPerThread; ++i) {
                ServerContext ctx;
                UpdateBatteryStateRequest req;
                req.set_level_percent((t * kUpdatesPerThread + i) % 101);
                BatteryState reply;
                service_->UpdateBatteryState(&ctx, &req, &reply);
            }
        });
    }

    threads.emplace_back([writer, &start]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        writer->OnCancel();
    });

    start.store(true, std::memory_order_release);
    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_TRUE(writer->finish_called);
    writer->OnDone();
    EXPECT_EQ(observable_battery_.CallbackCount(), 1);
    service_.reset();
    EXPECT_EQ(observable_battery_.CallbackCount(), 0);
}

TEST_F(BatteryServiceImplTest, ConcurrentPartialUpdatesPreserveDisjointFields) {
    for (int iter = 0; iter < 50; ++iter) {
        observable_battery_.SetValue(Battery{
            .has_battery = true,
            .is_present = true,
            .charger = Battery::Charger::kAc,
            .charge_level = 100,
            .health = Battery::Health::kGood,
            .status = Battery::Status::kCharging,
        });

        std::atomic<bool> start{false};
        std::thread level_thread([this, &start]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            ServerContext ctx;
            UpdateBatteryStateRequest req;
            req.set_level_percent(17);
            BatteryState reply;
            EXPECT_TRUE(service_->UpdateBatteryState(&ctx, &req, &reply).ok());
        });

        std::thread health_thread([this, &start]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            ServerContext ctx;
            UpdateBatteryStateRequest req;
            req.set_health(BatteryState::OVERHEAT);
            req.set_charger(ChargerSource::CHARGER_SOURCE_NONE);
            BatteryState reply;
            EXPECT_TRUE(service_->UpdateBatteryState(&ctx, &req, &reply).ok());
        });

        start.store(true, std::memory_order_release);
        level_thread.join();
        health_thread.join();

        const auto final_state = observable_battery_.GetValue();
        EXPECT_EQ(final_state.charge_level, 17);
        EXPECT_EQ(final_state.health, Battery::Health::kOverheated);
        EXPECT_EQ(final_state.charger, Battery::Charger::kNone);
    }
}

}  // namespace
}  // namespace goldfish::grpc::v2
