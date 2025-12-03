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

#include "goldfish/devices/Connector.h"
#include "goldfish/devices/PingTopic.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/connector_registry.h"
#include "goldfish/hal/plug/HalPlug.h"
#include "hal_plug_testing_friend.h"

namespace goldfish {
namespace devices {

using cable::PlugPtr;
using cable::SocketPtr;

struct TestHalSocket : public HalSocket {
    void send(std::string data) override {
        LOG(ERROR) << "Send " << data;
        storage.append(data);
    }
    void close() override { closed = true; };

    bool closed{false};
    std::string storage;
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
    TestConnectorRegistry() {}
    ~TestConnectorRegistry() = default;

    bool registerQemuDevice(std::string_view name, Connector::DeviceFactory factory) override {
        mFactory = std::move(factory);
        return true;
    }

    bool registerDevice(std::string_view name, Connector::DeviceFactory factory) override {
        mFactory = std::move(factory);
        return true;
    }

    void registerHalDevice(std::string name, async::EventLoop* clientLoop,
                           async::EventLoop* qemuLoop, HalDeviceFactory factory) override {
        mHalFactory = factory;
    }

    void registerHalQemuDevice(std::string name, async::EventLoop* clientLoop,
                               async::EventLoop* qemuLoop, HalDeviceFactory factory) override {
        mHalFactory = factory;
    }

    template <typename T>
    T* constructHalDevice() {
        mHalSocket = std::make_shared<TestHalSocket>();
        mHalPlug = mHalFactory();
        HalPlugTesting::establishConnection(mHalPlug.get(), mHalSocket);
        // registerInternal(std::string(T::serviceName), mHalPlug);  b/448934377
        return reinterpret_cast<T*>(mHalPlug.get());
    }

    TestHalSocket* halSocket() { return mHalSocket.get(); }
    PlugPtr getPlug() { return mPlug; }

  private:
    Connector::DeviceFactory mFactory;
    HalDeviceFactory mHalFactory;

    PlugPtr mPlug;

    std::shared_ptr<TestHalSocket> mHalSocket;
    std::shared_ptr<HalPlug> mHalPlug;
};

}  // namespace devices
}  // namespace goldfish
