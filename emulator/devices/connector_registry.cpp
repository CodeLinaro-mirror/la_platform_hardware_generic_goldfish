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

    for (const auto& [key, value] : mEntries) {
        Connector::DeviceFactory registerfn = [value, key, this](auto socket, auto ping,
                                                                 auto args) {
            auto connector = value(std::move(socket), std::move(ping), args);
            auto registryName = key.substr(1);
            registerInternal(registryName, connector);
            return connector;
        };
        mDevices.push_back({key.c_str(), std::move(registerfn)});
    }
    return startListening([this](auto socket) {
        return std::make_shared<Connector>(std::move(socket), mPingTopic, mDevices.data(),
                                           mDevices.size());
    });
}

void ConnectorRegistry::registerInternal(std::string registryName,
                                         std::weak_ptr<cable::IPlug> plug) {
    {
        std::lock_guard<std::mutex> lock(mActivePlugsMutex);
        VLOG(1) << "Registering device: " << registryName;
        mActivePlugs[registryName] = plug;
    }
    fireEvent(registryName);
}

bool ConnectorRegistry::registerQemuDevice(std::string name, Connector::DeviceFactory factory) {
    std::lock_guard<std::mutex> lock(mEntriesMutex);
    if (!mAcceptingRegistries) {
        LOG(WARNING) << "The registry is closed, qemud: " << name << " is not registered.";
        return false;
    }

    mEntries[absl::StrCat("q", name)] = factory;
    return true;
}

bool ConnectorRegistry::registerDevice(std::string name, Connector::DeviceFactory factory) {
    std::lock_guard<std::mutex> lock(mEntriesMutex);
    if (!mAcceptingRegistries) {
        LOG(WARNING) << "The registry is closed, device: " << name << " is not registered.";
        return false;
    }
    mEntries[absl::StrCat("-", name)] = factory;
    return true;
}

ConnectorRegistry& ConnectorRegistry::defaultRegistry() {
    static ConnectorRegistry registry;
    return registry;
}

}  // namespace devices
}  // namespace goldfish
