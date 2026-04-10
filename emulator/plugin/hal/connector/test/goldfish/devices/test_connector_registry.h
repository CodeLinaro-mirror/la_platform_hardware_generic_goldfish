// Copyright (C) 2024 The Android Open Source Project
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
#include <memory>
#include <string>

#include "absl/log/check.h"
#include "absl/log/log.h"

#include "goldfish/devices/test/hal_plug_testing_friend.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/connector.h"
#include "goldfish/devices/connector_registry_impl.h"
#include "goldfish/devices/internal/hal_plug.h"
#include "goldfish/devices/ping_topic.h"

namespace goldfish::devices {

using cable::PlugPtr;
using cable::SocketPtr;

struct TestHalSocket : public HalSocket {
    void Send(std::string data) override {
        LOG(ERROR) << "Send " << data;
        on_send(data);
        storage.append(data);
    }
    void Close() override { closed = true; };

    bool closed{false};
    std::string storage;
    std::function<void(const std::string&)> on_send = [](const std::string&) {};
};

/**
 * @brief A ConnectorRegistry implementation for testing purposes.
 *
 * This class provides a controlled environment for testing devices that interact
 * with a ConnectorRegistry. It uses a TestLooper and TestSocket to simulate
 * the asynchronous communication and data transfer aspects of the real system.
 * This allows for precise control over the timing and content of messages
 * exchanged between the device and the test environment.
 *
 * **Note:** This registry is designed to register and construct a single device
 * for testing purposes.  Registering multiple devices will lead to unexpected
 * behavior.  The previously registered factory will be overwritten.
 *
 * @tparam T The type of device to be constructed and tested.  This is
 *           typically an interface type (e.g., ISensorDevice).
 *
 * Typical usage involves these steps:
 *
 * 1. **Create a TestConnectorRegistry:**
 *    ```cpp
 *    TestConnectorRegistry registry;
 *    ```
 *
 * 2. **Register the device factory:** This is usually done through a static
 *    `registerDevice` method on the device interface.
 *    ```cpp
 *    MyFancyDevice::registerDevice(&registry, ...);
 *    ```
 *
 * 3. **Construct the device:**
 *    ```cpp
 *    auto* device = registry.constructDevice<MyFancyDevice>();
 *    ```
 *
 * 4. **Access the TestSocket and TestLooper:**
 *    ```cpp
 *    auto* testSocket = registry.getSocket();
 *    auto* looper = registry.getLooper();
 *    ```
 *
 * 5. **Simulate guest-to-host communication:** Send data to the device as if
 *    it were received from the guest.
 *    ```cpp
 *    std::string_view msg("Data from guest");
 *    device->onReceive(msg.data(), msg.size());
 *    ```
 *
 * 6. **Verify host-to-guest communication:** Check the data sent by the device
 *    through the TestSocket.
 *    ```cpp
 *    testSocket->storage.clear(); // Clear any previous data
 *    // Perform actions that trigger device responses
 *    EXPECT_THAT(testSocket->storage, Eq("Expected data"));
 *    ```
 *
 * This class simplifies testing by providing direct access to the communication
 * channel (TestSocket) and control over the event loop (TestLooper).  It avoids
 * the complexities of setting up a real vsock connection and allows for
 * deterministic testing of device behavior.
 */
class TestConnectorRegistry : public ConnectorRegistry {
  public:
    TestConnectorRegistry() = default;

    ~TestConnectorRegistry() { CHECK(!hal_plug_); }

    void Close() {
        if (hal_plug_) {
            hal_plug_->OnClose();
            hal_plug_.reset();
        }
    }

    bool RegisterQemuDevice(std::string_view /*name*/, DeviceFactory /*factory*/) override {
        return true;
    }

    bool RegisterDevice(std::string_view /*name*/, DeviceFactory /*factory*/) override {
        return true;
    }

    void RegisterHalDevice(std::string /*name*/, async::EventLoop* /*client_loop*/,
                           async::EventLoop* /*qemu_loop*/, HalDeviceFactory factory) override {
        hal_factory_ = std::move(factory);
    }

    void RegisterHalQemuDevice(std::string /*name*/, async::EventLoop* /*client_loop*/,
                               async::EventLoop* /*qemu_loop*/, HalDeviceFactory factory) override {
        hal_factory_ = std::move(factory);
    }

    template <typename T>
    T* ConstructHalDevice(std::string_view args = {}) {
        hal_socket_ = std::make_shared<TestHalSocket>();
        hal_plug_ = hal_factory_(args);
        HalPlugTesting::EstablishConnection(hal_plug_.get(), hal_socket_);
        // registerInternal(std::string(T::kServiceName), hal_plug_);  b/448934377
        return reinterpret_cast<T*>(hal_plug_.get());
    }

    TestHalSocket* HalSocket() { return hal_socket_.get(); }

  private:
    HalDeviceFactory hal_factory_;
    std::shared_ptr<TestHalSocket> hal_socket_;
    std::shared_ptr<HalPlug> hal_plug_;
};

}  // namespace goldfish::devices
