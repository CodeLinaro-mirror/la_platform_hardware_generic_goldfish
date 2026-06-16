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

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/escaping.h"

namespace goldfish::devices::vehicle {

using ::goldfish::avd_universe::vehicle::ObservableVehiclePropValue;
using ::goldfish::avd_universe::vehicle::VehicleChannel;
using ::goldfish::avd_universe::vehicle::VehiclePropValue;

using VehicleDataUpdateSubscription = std::unique_ptr<
        android::base::eventing::ScopedEventCallback<ObservableVehiclePropValue, VehiclePropValue>>;

class VehicleDevice : public IVehicleDevice {
  public:
    explicit VehicleDevice(VehicleChannel* vehicle_channel) : vehicle_channel_(*vehicle_channel) {
        VLOG(1) << "Vehicle device has been created";
    }

    void OnConnect() override { VLOG(1) << "Vehicle device has been connected"; }
    void OnClose() override { VLOG(1) << "Vehicle device has been disconnected"; }

    void OnReceive(const std::string_view data) override {
        VLOG(1) << "VehicleDevice::OnReceive called with " << data.size() << " bytes";
        receive_data_.insert(receive_data_.end(), data.begin(), data.end());

        while (true) {
            if (expected_payload_size_ == 0) {
                if (receive_data_.size() < sizeof(uint32_t)) {
                    return;
                }
                // Read size in network byte order (big-endian) as per SocketComm.cpp
                uint32_t data_size;
                memcpy(&data_size, receive_data_.data(), sizeof(uint32_t));
                expected_payload_size_ = ntohl(data_size);

                // Remove size header from buffer
                receive_data_.erase(receive_data_.begin(),
                                    receive_data_.begin() + sizeof(uint32_t));
            }

            if (receive_data_.size() < expected_payload_size_) {
                return;
            }

            // Extract payload
            std::string payload(receive_data_.data(), expected_payload_size_);
            receive_data_.erase(receive_data_.begin(),
                                receive_data_.begin() + expected_payload_size_);

            // Reset for the next packet before parsing
            expected_payload_size_ = 0;

            VehiclePropValue prop_value;
            if (prop_value.ParseFromString(payload)) {
                VLOG(1) << "Received VHAL property update from guest: " << prop_value.prop();
                {
                    std::lock_guard<std::mutex> lock(vehicle_channel_.guest_state_mutex);
                    vehicle_channel_.guest_state_map[{prop_value.prop(), prop_value.area_id()}] =
                            prop_value;
                }
                vehicle_channel_.guest_to_host.SetValue(std::move(prop_value));
            } else {
                LOG(ERROR)
                        << "VHAL communication error: The emulator host received an invalid "
                           "data payload from the Android guest system. This indicates a protocol "
                           "mismatch or corrupted data packet between the guest Vehicle HAL driver "
                           "and the host emulator. The property update could not be parsed and "
                           "will be ignored.";
                std::string hex_dump = absl::BytesToHexString(payload);
                LOG(ERROR) << "Raw payload (hex): " << hex_dump;
            }
        }
    }

    void SetContents(const VehiclePropValue& prop_value) {
        std::string payload;
        if (!prop_value.SerializeToString(&payload)) {
            LOG(ERROR) << "VHAL serialization error: Failed to serialize the vehicle property (ID: "
                       << prop_value.prop()
                       << ") into a protobuf payload. The property change event could not "
                          "be sent to the Android guest system. This might be due to an "
                          "uninitialized or invalid field value in the property definition.";
            return;
        }

        const uint32_t size = payload.size();
        VLOG(1) << "Sending VHAL property update to guest: " << prop_value.prop();

        // Combine size header (4 bytes, big-endian) and payload into a single packet string
        // to prevent packet interleaving from concurrent threads.
        uint32_t network_size = htonl(size);
        std::string packet(reinterpret_cast<char*>(&network_size), sizeof(uint32_t));
        packet.append(payload);

        Socket()->Send(std::move(packet));
    }

    void SetVehicleChangeSubscription(VehicleDataUpdateSubscription subscription) {
        vehicle_data_update_subscription_ = std::move(subscription);
    }

  private:
    std::vector<char> receive_data_;
    uint32_t expected_payload_size_ = 0;
    VehicleChannel& vehicle_channel_;
    VehicleDataUpdateSubscription vehicle_data_update_subscription_;
};

void IVehicleDevice::RegisterDevice(avd_universe::vehicle::VehicleChannel* channel,
                                    IConnectorRegistry* registry, EventLoop* client_loop,
                                    EventLoop* qemu_loop) {
    registry->RegisterHalDevice(
            std::string(IVehicleDevice::kServiceName), client_loop, qemu_loop,
            [channel](std::string_view /*args*/) -> std::shared_ptr<HalPlug> {
                auto dev = std::make_shared<VehicleDevice>(channel);
                std::weak_ptr<VehicleDevice> weak_dev = dev;

                auto change_subscription = MakeScopedCallback(
                        channel->host_to_guest,
                        [weak_dev = std::move(weak_dev)](const VehiclePropValue& val) {
                            if (const auto dev = weak_dev.lock()) {
                                dev->SetContents(val);
                            }
                        });

                dev->SetVehicleChangeSubscription(std::move(change_subscription));

                return dev;
            });
}

}  // namespace goldfish::devices::vehicle
