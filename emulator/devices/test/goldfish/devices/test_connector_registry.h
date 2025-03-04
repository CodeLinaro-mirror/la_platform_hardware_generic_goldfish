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

#include "aemu/base/testing/TestLooper.h"
#include "goldfish/devices/Connector.h"
#include "goldfish/devices/PingTopic.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/connector_registry.h"
#include "goldfish/devices/test_socket.h"

namespace goldfish {
namespace devices {

using cable::PlugPtr;
using cable::SocketPtr;

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

    bool registerQemuDevice(std::string name, Connector::DeviceFactory factory) override {
        mFactory = std::move(factory);
        return true;
    }

    bool registerDevice(std::string name, Connector::DeviceFactory factory) override {
        mFactory = std::move(factory);
        return true;
    }

    template <typename T>
    T* constructDevice() {
        auto socket = goldfish::devices::fakeConnection(&mLooper);
        mSocket = static_cast<TestSocket*>(socket.get());
        mPlug = mFactory(std::move(socket), std::make_shared<PingTopic>(), "");
        registerInternal(std::string(T::serviceName), mPlug);
        return reinterpret_cast<T*>(mPlug.get());
    }

    TestSocket* getSocket() { return mSocket; }

    PlugPtr getPlug() { return mPlug; }

    TestLooper* getLooper() { return &mLooper; }

  private:
    TestLooper mLooper;
    Connector::DeviceFactory mFactory;
    TestSocket* mSocket;
    PlugPtr mPlug;
};

}  // namespace devices
}  // namespace goldfish
