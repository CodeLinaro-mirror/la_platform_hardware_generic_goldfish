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
#include "goldfish/devices/connector_registry.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <mutex>

#include "absl/base/no_destructor.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"

#include "goldfish/async/event_loop.h"
#include "goldfish/hal/plug/HalPlugFactory.h"
#include "goldfish/hal/plug/HalPlugToIPlugAdapter.h"
#include "goldfish/hal/plug/MarshallingHalSocket.h"
#include "goldfish/vsock/listen.h"

namespace goldfish {
namespace devices {
using cable::PlugPtr;
using cable::SocketPtr;

ConnectorRegistry::ConnectorRegistry() : ConnectorRegistry(std::make_shared<PingTopic>()) {}

ConnectorRegistry::ConnectorRegistry(std::shared_ptr<PingTopic> pingTopic)
        : mPingTopic(std::move(pingTopic)), mAcceptingRegistries(true) {}

bool ConnectorRegistry::listen(int port) {
    return listen([port](HostPortListener listener) { return vsock::listen(port, listener); });
}

bool ConnectorRegistry::listen(ListenFn startListening) {
    // Only start listening once, the registry is now closed.
    std::lock_guard<std::mutex> lock(mEntriesMutex);
    mAcceptingRegistries = false;

    for (const auto& [key, factory_fn] : mEntries) {
        Connector::DeviceFactory registerfn = [factory_fn, key, this](auto socket, auto ping,
                                                                      auto args) {
            auto connector = factory_fn(std::move(socket), std::move(ping), args);
            auto registryName = key.substr(1);

            // Only register if our factory_fn didn't register it already
            if (!mActivePlugs.count(registryName)) {
                registerInternal<cable::IPlug>(registryName, connector);
            }
            return connector;
        };

        mDevices.push_back({key.c_str(), std::move(registerfn)});
    }

    return startListening([this](auto socket) {
        return std::make_shared<Connector>(std::move(socket), mPingTopic, mDevices.data(),
                                           mDevices.size());
    });
}

bool ConnectorRegistry::registerQemuDevice(const std::string_view name,
                                           Connector::DeviceFactory factory) {
    using namespace std::string_view_literals;
    return registerDeviceImpl("q"sv, name, std::move(factory));
}

bool ConnectorRegistry::registerDevice(const std::string_view name,
                                       Connector::DeviceFactory factory) {
    using namespace std::string_view_literals;
    return registerDeviceImpl("-"sv, name, std::move(factory));
}

bool ConnectorRegistry::registerDeviceImpl(const std::string_view prefix,
                                           const std::string_view name,
                                           Connector::DeviceFactory factory) {
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
                          [this](std::string name, Connector::DeviceFactory factory) {
                              return registerDevice(name, std::move(factory));
                          });
}

void ConnectorRegistry::registerHalQemuDevice(std::string name, async::EventLoop* clientLoop,
                                              async::EventLoop* qemuLoop,
                                              HalDeviceFactory factory) {
    registerHalDeviceImpl(std::move(name), clientLoop, qemuLoop, std::move(factory),
                          [this](std::string name, Connector::DeviceFactory factory) {
                              return registerQemuDevice(name, std::move(factory));
                          });
}

void ConnectorRegistry::registerHalDeviceImpl(std::string name, async::EventLoop* clientLoop,
                                              async::EventLoop* qemuLoop, HalDeviceFactory factory,
                                              DeviceRegistration registerFn) {
    auto wrapperFactory = [this, name, qemuLoop, clientLoop, userFactory = std::move(factory)](
                                  SocketPtr qemuSocket, std::shared_ptr<PingTopic> pingTopic,
                                  std::string_view args) -> PlugPtr {
        // Create the user's HAL plug on the QEMU thread. This has to be a synchronous call
        // as we must give our vsockstream a concrete PlugPtr. Let's hope developers are not doing
        // *crazy* things in the factory.
        std::shared_ptr<HalPlug> realHalPlug = userFactory();

        // Wrap the HAL plug in a marshalling layer. This will ensure that all calls to the
        // HAL plug are marshalled to the client thread and vice versa.
        auto adapter = HalPlugFactory::wrapHalPlug(
                std::move(qemuSocket), [realHalPlug = realHalPlug] { return realHalPlug; },
                clientLoop, qemuLoop);
        // Register the plug for activeDevice() lookups and return the
        // adapter to the vsock layer.
        registerInternal<HalPlug>(name, realHalPlug);
        return adapter;
    };

    registerFn(std::move(name), std::move(wrapperFactory));
}

ConnectorRegistry& ConnectorRegistry::defaultRegistry() {
    static ConnectorRegistry registry;
    return registry;
}

}  // namespace devices
}  // namespace goldfish
