// Copyright (C) 2017 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "goldfish/devices/sensor/sensor_device.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "absl/strings/str_format.h"
#include "absl/time/time.h"

#include "android/goldfish/fake_hardware_config.h"
#include "goldfish/archive/collections/vector.h"
#include "goldfish/archive/deque_archive.h"
#include "goldfish/archive/reader.h"
#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/devices/qemud/qemud.h"
#include "goldfish/devices/test_connector_registry.h"
#include "goldfish/system/testing/fake_clocks.h"

namespace goldfish::devices::sensor {

using async::testing::TestEventLoop;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::Not;

namespace {
int CountOccurrences(const std::string& text, const std::string& target) {
    int count = 0;
    std::string::size_type pos = 0;

    while ((pos = text.find(target, pos)) != std::string::npos) {
        ++count;
        pos += target.length();
    }

    return count;
}
}  // namespace

class SensorDeviceTest : public ::testing::Test {
  protected:
    void SetUp() override {
        hw_ = android::goldfish::FakeHardwareConfig::GetHwConfig();
        physical_model_ = std::make_unique<PhysicalModel>(hw_, clock_);
        client_loop_ = TestEventLoop::Create();
        qemu_loop_ = TestEventLoop::Create();

        ISensorDevice::RegisterDevice(physical_model_.get(), &registry_,
                                      /*avd_type=*/android::goldfish::DeviceType::kPhone,
                                      /*avd_api=*/30, hw_, client_loop_.get(), qemu_loop_.get(),
                                      clock_);
        device_ = registry_.ConstructHalDevice<ISensorDevice>();
        test_socket_ = registry_.HalSocket();
        Clear();
        device_->OnConnect();
    }

    void TearDown() override {
        registry_.Close();
        client_loop_->RunAll();
    }

  public:
    void Receive(std::string_view msg) {
        (void)client_loop_->Post([&, this] { device_->OnReceive(qemud::EncodeQemudPacket(msg)); });
        client_loop_->RunAll();
    }
    void Clear() { test_socket_->storage.clear(); }

  protected:
    android::goldfish::HardwareConfig hw_;
    std::unique_ptr<PhysicalModel> physical_model_;
    std::unique_ptr<TestEventLoop> client_loop_;
    std::unique_ptr<TestEventLoop> qemu_loop_;
    TestConnectorRegistry registry_;
    ::goldfish::system::testing::FakeVirtualClock clock_;
    ISensorDevice* device_;
    TestHalSocket* test_socket_;
};

TEST_F(SensorDeviceTest, canCreateDevice) {
    EXPECT_NE(device_, nullptr);
}

TEST_F(SensorDeviceTest, canListSensors) {
    Receive("list-sensors");
    EXPECT_THAT(test_socket_->storage, Eq("0006133119"));
}

TEST_F(SensorDeviceTest, canSetSensors) {
    Receive("set:acceleration:0");
    Receive("set:gyroscope:0");
    Clear();

    // The active set should have changed.
    Receive("list-sensors");
    EXPECT_THAT(test_socket_->storage, Eq("0006133116"));
}

TEST_F(SensorDeviceTest, setDelayCausesATick) {
    clock_.Set(absl::Nanoseconds(1234567890));
    Receive("set-delay:10");
    // The looper keeps ticking so just check for the first few digits.
    EXPECT_THAT(test_socket_->storage, HasSubstr("0015guest-sync:1234"));
}

TEST_F(SensorDeviceTest, setTimeOffset) {
    clock_.Set(absl::Nanoseconds(1234567890));
    Receive("time:100");
    Receive("set-delay:1");
    EXPECT_THAT(test_socket_->storage, HasSubstr("000Eguest-sync:10"));
}

TEST_F(SensorDeviceTest, timeKeepsOnRolling) {
    clock_.Set(absl::Nanoseconds(1234567890));
    Receive("set-delay:1");
    Clear();
    EXPECT_THAT(test_socket_->storage, Eq(""));
    for (int i = 0; i < 11; i++) client_loop_->AdvanceClock(absl::Milliseconds(10));

    // We should see a sync several times.
    EXPECT_THAT(CountOccurrences(test_socket_->storage, "guest-sync:"), Gt(10));
}

// --- Snapshots -------------------------------------------------------------

class SensorDeviceSnapshotTest : public SensorDeviceTest {
  protected:
    // A second, freshly constructed device, as the snapshot loader creates it.
    ISensorDevice* ConstructRestoredDevice() {
        ISensorDevice::RegisterDevice(physical_model_.get(), &restored_registry_,
                                      /*avd_type=*/android::goldfish::DeviceType::kPhone,
                                      /*avd_api=*/30, hw_, client_loop_.get(), qemu_loop_.get(),
                                      clock_);
        restored_ = restored_registry_.ConstructHalDevice<ISensorDevice>();
        restored_socket_ = restored_registry_.HalSocket();
        return restored_;
    }

    // LoadFromSnapshot() + OnRestore() run on the client loop, as the adapter does it.
    bool Restore(archive::IReader& reader) {
        bool loaded = false;
        (void)client_loop_->Post([&, this] {
            loaded = restored_->LoadFromSnapshot(reader);
            if (loaded) restored_->OnRestore();
        });
        client_loop_->RunAll();
        return loaded;
    }

    void ReceiveRestored(std::string_view raw) {
        (void)client_loop_->Post([&, this] { restored_->OnReceive(raw); });
        client_loop_->RunAll();
    }

    void TearDown() override {
        if (restored_) {
            restored_registry_.Close();
        }
        SensorDeviceTest::TearDown();
    }

    TestConnectorRegistry restored_registry_;
    ISensorDevice* restored_ = nullptr;
    TestHalSocket* restored_socket_ = nullptr;
};

TEST_F(SensorDeviceSnapshotTest, supportsSnapshots) {
    EXPECT_TRUE(device_->SupportsSnapshot());
}

TEST_F(SensorDeviceSnapshotTest, roundTripRestoresNegotiatedStateAndResumesTicks) {
    clock_.Set(absl::Nanoseconds(1234567890));
    Receive("set:gyroscope:0");
    Receive("set-delay:50");
    Receive("time:100");  // guest time offset: guest-sync == vm time + (100 - 1234567890)
    // A qemud packet the guest had only half sent: "000Bset:light:0" (11 bytes).
    (void)client_loop_->Post([this] { device_->OnReceive("000Bset:li"); });
    client_loop_->RunAll();

    archive::DequeArchive archive;
    ASSERT_TRUE(device_->SaveToSnapshot(archive));

    ConstructRestoredDevice();
    restored_socket_->storage.clear();
    ASSERT_TRUE(Restore(archive));
    EXPECT_TRUE(archive.Empty()) << "LoadFromSnapshot must consume exactly what was saved";

    // OnRestore() ticks right away with the restored mask and time offset...
    EXPECT_THAT(restored_socket_->storage, HasSubstr("acceleration:"));
    EXPECT_THAT(restored_socket_->storage, Not(HasSubstr("gyroscope:")));
    EXPECT_THAT(restored_socket_->storage, HasSubstr("guest-sync:100"));
    // ... and re-arms the timer with the restored 50ms delay.
    restored_socket_->storage.clear();
    client_loop_->AdvanceClock(absl::Milliseconds(49));
    EXPECT_THAT(restored_socket_->storage, Eq(""));
    client_loop_->AdvanceClock(absl::Milliseconds(1));
    EXPECT_THAT(CountOccurrences(restored_socket_->storage, "guest-sync:"), Eq(1));

    // The rest of the half-sent packet completes the command on the restored device.
    restored_socket_->storage.clear();
    ReceiveRestored("ght:0");
    ReceiveRestored(qemud::EncodeQemudPacket("list-sensors"));
    // 6133119 without gyroscope (bit 1) and light (bit 6).
    EXPECT_THAT(restored_socket_->storage, HasSubstr("0006133053"));
}

TEST_F(SensorDeviceSnapshotTest, restoreWithNoEnabledSensorsDoesNotTick) {
    // Disable every sensor the fake AVD has.
    Receive("list-sensors");
    EXPECT_THAT(test_socket_->storage, Eq("0006133119"));
    for (const char* sensor :
         {"acceleration", "gyroscope", "magnetic-field", "orientation", "temperature", "proximity",
          "light", "pressure", "humidity", "magnetic-field-uncalibrated", "gyroscope-uncalibrated",
          "hinge-angle0", "hinge-angle1", "hinge-angle2", "heart-rate", "rgbc-light", "wrist-tilt",
          "acceleration-uncalibrated"}) {
        Receive(absl::StrFormat("set:%s:0", sensor));
    }
    Clear();
    Receive("list-sensors");
    ASSERT_THAT(test_socket_->storage, Eq("00010"));

    archive::DequeArchive archive;
    ASSERT_TRUE(device_->SaveToSnapshot(archive));
    ConstructRestoredDevice();
    restored_socket_->storage.clear();
    ASSERT_TRUE(Restore(archive));

    EXPECT_THAT(restored_socket_->storage, Eq(""));
    client_loop_->AdvanceClock(absl::Seconds(2));
    EXPECT_THAT(restored_socket_->storage, Eq("")) << "no sensor enabled, timer must stay idle";

    // But the guest can still talk to it, and enabling a sensor starts ticking.
    ReceiveRestored(qemud::EncodeQemudPacket("set:acceleration:1"));
    EXPECT_THAT(restored_socket_->storage, HasSubstr("acceleration:"));
}

TEST_F(SensorDeviceSnapshotTest, restoreDropsSensorsTheAvdDoesNotHave) {
    // Wrist tilt (bit 16) is not part of the fake AVD; a snapshot claiming it
    // is enabled must not make the restored device report it.
    archive::DequeArchive archive;
    archive << uint32_t{1} << uint32_t{(1U << 16) | 1U} << int64_t{800'000'000} << int64_t{0};
    archive << std::vector<uint8_t>{};  // empty qemud parser buffer
    ConstructRestoredDevice();
    ASSERT_TRUE(Restore(archive));
    restored_socket_->storage.clear();
    ReceiveRestored(qemud::EncodeQemudPacket("list-sensors"));
    EXPECT_THAT(restored_socket_->storage, Eq("00011"));
}

TEST_F(SensorDeviceSnapshotTest, rejectsCorruptSnapshot) {
    ConstructRestoredDevice();

    archive::DequeArchive truncated;
    truncated << uint32_t{1} << uint32_t{3};
    EXPECT_FALSE(Restore(truncated));

    archive::DequeArchive wrong_version;
    wrong_version << uint32_t{42} << uint32_t{3} << int64_t{0} << int64_t{0};
    wrong_version << std::vector<uint8_t>{};
    EXPECT_FALSE(Restore(wrong_version));

    // The device is unusable after a failed load (the adapter closes it);
    // make sure it at least did not start a timer.
    client_loop_->AdvanceClock(absl::Seconds(2));
    EXPECT_THAT(restored_socket_->storage, Eq(""));
}

}  // namespace goldfish::devices::sensor
