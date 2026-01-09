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
#include <utility>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"

#include "goldfish/async/event_loop.h"
#include "goldfish/devices/hal_plug_factory.h"
#include "goldfish/vsock/listen.h"

namespace goldfish::devices {
using cable::PlugPtr;
using cable::SocketPtr;

ConnectorRegistry::ConnectorRegistry() : ConnectorRegistry(std::make_shared<PingTopic>()) {}

ConnectorRegistry::ConnectorRegistry(std::shared_ptr<PingTopic> ping_topic)
        : ping_topic_(std::move(ping_topic)) {}

bool ConnectorRegistry::Listen(int port) {
    return Listen(
            [port](HostPortListener listener) { return vsock::Listen(port, std::move(listener)); });
}

bool ConnectorRegistry::Listen(const ListenFn& start_listening) {
    // Only start listening once, the registry is now closed.
    const std::lock_guard<std::mutex> lock(entries_mutex_);
    accepting_registries_ = false;

    for (auto& [key, factory_fn] : entries_) {
        devices_.push_back({.qname = key, .factory = std::move(factory_fn)});
    }

    entries_.clear();

    return start_listening([this](auto socket) {
        return std::make_shared<Connector>(std::move(socket), ping_topic_, devices_.data(),
                                           devices_.size());
    });
}

bool ConnectorRegistry::RegisterQemuDevice(const std::string_view name, DeviceFactory factory) {
    using namespace std::string_view_literals;
    return RegisterDeviceImpl("q"sv, name, std::move(factory));
}

bool ConnectorRegistry::RegisterDevice(const std::string_view name, DeviceFactory factory) {
    using namespace std::string_view_literals;
    return RegisterDeviceImpl("-"sv, name, std::move(factory));
}

bool ConnectorRegistry::RegisterDeviceImpl(const std::string_view prefix,
                                           const std::string_view name, DeviceFactory factory) {
    const std::lock_guard<std::mutex> lock(entries_mutex_);
    if (!accepting_registries_) {
        LOG(WARNING) << "The registry is closed, device: " << name << " is not registered.";
        return false;
    }

    return entries_.insert({absl::StrCat(prefix, name), std::move(factory)}).second;
}

void ConnectorRegistry::RegisterHalDevice(std::string name, async::EventLoop* client_loop,
                                          async::EventLoop* qemu_loop, HalDeviceFactory factory) {
    RegisterHalDeviceImpl(std::move(name), client_loop, qemu_loop, std::move(factory),
                          [this](const std::string& name, DeviceFactory factory) {
                              return RegisterDevice(name, std::move(factory));
                          });
}

void ConnectorRegistry::RegisterHalQemuDevice(std::string name, async::EventLoop* client_loop,
                                              async::EventLoop* qemu_loop,
                                              HalDeviceFactory factory) {
    RegisterHalDeviceImpl(std::move(name), client_loop, qemu_loop, std::move(factory),
                          [this](const std::string& name, DeviceFactory factory) {
                              return RegisterQemuDevice(name, std::move(factory));
                          });
}
void ConnectorRegistry::RegisterHalDeviceImpl(std::string name, async::EventLoop* client_loop,
                                              async::EventLoop* qemu_loop, HalDeviceFactory factory,
                                              const DeviceRegistration& register_fn) {
    auto wrapper_factory = [name, qemu_loop, client_loop, user_factory = std::move(factory)](
                                   SocketPtr qemu_socket,
                                   const std::shared_ptr<PingTopic>& /*ping_topic*/,
                                   std::string_view args) -> PlugPtr {
        // Create the user's HAL plug on the QEMU thread. This has to be a synchronous call
        // as we must give our vsockstream a concrete PlugPtr. Let's hope developers are not doing
        // *crazy* things in the factory.
        const std::shared_ptr<HalPlug> real_hal_plug = user_factory(args);

        // Wrap the HAL plug in a marshalling layer. This will ensure that all calls to the
        // HAL plug are marshalled to the client thread and vice versa.
        return HalPlugFactory::WrapHalPlug(
                std::move(qemu_socket), [real_hal_plug = real_hal_plug] { return real_hal_plug; },
                client_loop, qemu_loop);
    };

    register_fn(std::move(name), std::move(wrapper_factory));
}

}  // namespace goldfish::devices
