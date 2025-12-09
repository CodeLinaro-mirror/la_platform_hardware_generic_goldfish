// Copyright 2024 The Android Open Source Project
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

#include "goldfish/devices/connector_registry_impl.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <mutex>

#include "absl/base/no_destructor.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"

#include "goldfish/async/event_loop.h"
#include "goldfish/devices/hal_plug_factory.h"
#include "goldfish/vsock/listen.h"

namespace goldfish {
namespace devices {
using cable::PlugPtr;
using cable::SocketPtr;

ConnectorRegistry::ConnectorRegistry() : ConnectorRegistry(std::make_shared<PingTopic>()) {}

ConnectorRegistry::ConnectorRegistry(std::shared_ptr<PingTopic> pingTopic)
        : mPingTopic(std::move(pingTopic)) {}

bool ConnectorRegistry::listen(int port) {
    return listen([port](HostPortListener listener) { return vsock::listen(port, listener); });
}

bool ConnectorRegistry::listen(ListenFn startListening) {
    // Only start listening once, the registry is now closed.
    std::lock_guard<std::mutex> lock(mEntriesMutex);
    mAcceptingRegistries = false;

    for (auto& [key, factory_fn] : mEntries) {
        mDevices.push_back({std::move(key), std::move(factory_fn)});
    }

    mEntries.clear();

    return startListening([this](auto socket) {
        return std::make_shared<Connector>(std::move(socket), mPingTopic, mDevices.data(),
                                           mDevices.size());
    });
}

bool ConnectorRegistry::registerQemuDevice(const std::string_view name, DeviceFactory factory) {
    using namespace std::string_view_literals;
    return registerDeviceImpl("q"sv, name, std::move(factory));
}

bool ConnectorRegistry::registerDevice(const std::string_view name, DeviceFactory factory) {
    using namespace std::string_view_literals;
    return registerDeviceImpl("-"sv, name, std::move(factory));
}

bool ConnectorRegistry::registerDeviceImpl(const std::string_view prefix,
                                           const std::string_view name, DeviceFactory factory) {
    std::lock_guard<std::mutex> lock(mEntriesMutex);
    if (!mAcceptingRegistries) {
        LOG(WARNING) << "The registry is closed, device: " << name << " is not registered.";
        return false;
    }

    return mEntries.insert({absl::StrCat(prefix, name), std::move(factory)}).second;
}

void ConnectorRegistry::registerHalDevice(std::string name, async::EventLoop* clientLoop,
                                          async::EventLoop* qemuLoop, HalDeviceFactory factory) {
    registerHalDeviceImpl(std::move(name), clientLoop, qemuLoop, std::move(factory),
                          [this](std::string name, DeviceFactory factory) {
                              return registerDevice(name, std::move(factory));
                          });
}

void ConnectorRegistry::registerHalQemuDevice(std::string name, async::EventLoop* clientLoop,
                                              async::EventLoop* qemuLoop,
                                              HalDeviceFactory factory) {
    registerHalDeviceImpl(std::move(name), clientLoop, qemuLoop, std::move(factory),
                          [this](std::string name, DeviceFactory factory) {
                              return registerQemuDevice(name, std::move(factory));
                          });
}

void ConnectorRegistry::registerHalDeviceImpl(std::string name, async::EventLoop* clientLoop,
                                              async::EventLoop* qemuLoop, HalDeviceFactory factory,
                                              DeviceRegistration registerFn) {
    auto wrapperFactory = [name, qemuLoop, clientLoop, userFactory = std::move(factory)](
                                  SocketPtr qemuSocket, std::shared_ptr<PingTopic> pingTopic,
                                  std::string_view args) -> PlugPtr {
        // Create the user's HAL plug on the QEMU thread. This has to be a synchronous call
        // as we must give our vsockstream a concrete PlugPtr. Let's hope developers are not doing
        // *crazy* things in the factory.
        std::shared_ptr<HalPlug> realHalPlug = userFactory();

        // Wrap the HAL plug in a marshalling layer. This will ensure that all calls to the
        // HAL plug are marshalled to the client thread and vice versa.
        return HalPlugFactory::wrapHalPlug(
                std::move(qemuSocket), [realHalPlug = realHalPlug] { return realHalPlug; },
                clientLoop, qemuLoop);
    };

    registerFn(std::move(name), std::move(wrapperFactory));
}

ConnectorRegistry& ConnectorRegistry::defaultRegistry() {
    static ConnectorRegistry registry;
    return registry;
}

}  // namespace devices
}  // namespace goldfish
