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
#include "goldfish/devices/vehicle/vehicle_device.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#include <winsock2.h>
#undef ERROR
#else
#include <arpa/inet.h>
#endif
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/devices/test_connector_registry.h"

namespace goldfish::devices::vehicle {

using goldfish::async::testing::TestEventLoop;
using ::goldfish::avd_universe::vehicle::VehicleChannel;
using ::goldfish::avd_universe::vehicle::VehiclePropValue;

class VehicleDeviceTest : public ::testing::Test {
  protected:
    void SetUp() override {
        mClientLoop = TestEventLoop::Create();
        mQemuLoop = TestEventLoop::Create();

        mChannel = std::make_unique<VehicleChannel>();

        IVehicleDevice::RegisterDevice(mChannel.get(), &registry, mClientLoop.get(),
                                       mQemuLoop.get());

        device = registry.ConstructHalDevice<IVehicleDevice>();
        test_socket = registry.HalSocket();
    }

    void TearDown() override {
        registry.Close();
        mClientLoop->RunAll();
    }

    std::unique_ptr<TestEventLoop> mClientLoop;
    std::unique_ptr<TestEventLoop> mQemuLoop;
    std::unique_ptr<VehicleChannel> mChannel;
    TestConnectorRegistry registry;
    TestHalSocket* test_socket;
    IVehicleDevice* device;
};

TEST_F(VehicleDeviceTest, canCreateDevice) {
    EXPECT_NE(device, nullptr);
}

TEST_F(VehicleDeviceTest, guestToHostSendsData) {
    // Simulate incoming vsock payload from guest
    VehiclePropValue prop;
    prop.set_prop(123);
    prop.set_area_id(0);

    std::string payload;
    prop.SerializeToString(&payload);

    uint32_t size = payload.size();
    uint32_t network_size = htonl(size);

    // Send size
    device->OnReceive(std::string(reinterpret_cast<const char*>(&network_size), sizeof(uint32_t)));
    // Send payload
    device->OnReceive(payload);

    // Verify avd_universe state changes
    VehiclePropValue received_prop = mChannel->guest_to_host.GetValue();
    EXPECT_EQ(received_prop.prop(), 123);
}

TEST_F(VehicleDeviceTest, hostToGuestSendsData) {
    // Simulate host update via avd_universe
    VehiclePropValue prop;
    prop.set_prop(456);
    prop.set_area_id(1);

    mChannel->host_to_guest.SetValue(prop);

    // Verify data was sent to the socket
    std::string expected_payload;
    prop.SerializeToString(&expected_payload);

    uint32_t expected_size = expected_payload.size();
    uint32_t expected_network_size = htonl(expected_size);

    std::string expected_data =
            std::string(reinterpret_cast<char*>(&expected_network_size), sizeof(uint32_t)) +
            expected_payload;

    EXPECT_EQ(test_socket->storage, expected_data);
}

}  // namespace goldfish::devices::vehicle
