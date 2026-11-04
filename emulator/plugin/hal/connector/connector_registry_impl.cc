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
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/base/no_destructor.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/async/event_loop.h"
#include "goldfish/devices/hal_plug_factory.h"
#include "goldfish/devices/hal_plug_snapshot.h"
#include "goldfish/vsock/listen.h"

namespace goldfish::devices {
using cable::PlugPtr;
using cable::SocketPtr;

namespace {
// snapshot id -> the registry listening under it (see
// ConnectorRegistry::PublishSnapshotTarget). Guarded by SnapshotMutex().
absl::Mutex& SnapshotMutex() {
    static absl::NoDestructor<absl::Mutex> mutex;
    return *mutex;
}
using SnapshotRegistries = absl::flat_hash_map<std::string, ConnectorRegistry*>;
SnapshotRegistries& GetSnapshotRegistries() ABSL_EXCLUSIVE_LOCKS_REQUIRED(SnapshotMutex()) {
    static absl::NoDestructor<SnapshotRegistries> registries;
    return *registries;
}
}  // namespace

ConnectorRegistry::ConnectorRegistry() : ConnectorRegistry(PingTopic::Create()) {}

ConnectorRegistry::ConnectorRegistry(std::shared_ptr<PingTopic> ping_topic)
        : ping_topic_(std::move(ping_topic)) {}

bool ConnectorRegistry::Listen(int port) {
    return ListenImpl(absl::StrCat("vsock:", port), [port](HostPortListener listener) {
        return vsock::Listen(port, std::move(listener));
    });
}

bool ConnectorRegistry::Listen(const ListenFn& start_listening) {
    return ListenImpl(/*snapshot_id=*/"", start_listening);
}

bool ConnectorRegistry::ListenImpl(std::string snapshot_id, const ListenFn& start_listening) {
    // Only start listening once, the registry is now closed.
    const std::lock_guard<std::mutex> lock(entries_mutex_);
    accepting_registries_ = false;

    for (auto& [key, factory_fn] : entries_) {
        devices_.push_back({.qname = key, .factory = std::move(factory_fn)});
    }

    entries_.clear();

    snapshot_id_ = std::move(snapshot_id);
    EnsureConnectorLoaderRegistered();
    PublishSnapshotTarget();

    return start_listening([this](auto socket) {
        return std::make_shared<Connector>(std::move(socket), ping_topic_, devices_.data(),
                                           devices_.size(), snapshot_id_);
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
    RegisterHalDeviceImpl(std::move(name), "-", client_loop, qemu_loop, std::move(factory),
                          [this](const std::string& name, DeviceFactory factory) {
                              return RegisterDevice(name, std::move(factory));
                          });
}

void ConnectorRegistry::RegisterHalQemuDevice(std::string name, async::EventLoop* client_loop,
                                              async::EventLoop* qemu_loop,
                                              HalDeviceFactory factory) {
    RegisterHalDeviceImpl(std::move(name), "q", client_loop, qemu_loop, std::move(factory),
                          [this](const std::string& name, DeviceFactory factory) {
                              return RegisterQemuDevice(name, std::move(factory));
                          });
}
void ConnectorRegistry::RegisterHalDeviceImpl(std::string name, std::string_view prefix,
                                              async::EventLoop* client_loop,
                                              async::EventLoop* qemu_loop, HalDeviceFactory factory,
                                              const DeviceRegistration& register_fn) {
    // The key under which the device is found again when a snapshot is loaded
    // (see HalPlugSnapshotRegistry); it matches DeviceEntry::qname.
    const std::string qname = absl::StrCat(prefix, name);

    auto wrapper_factory = [qname, qemu_loop, client_loop, user_factory = factory](
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
                client_loop, qemu_loop,
                HalPlugSnapshotIdentity{.device = qname, .args = std::string(args)});
    };

    if (register_fn(std::move(name), std::move(wrapper_factory))) {
        HalPlugSnapshotRegistry::Get().Register(qname, {.client_loop = client_loop,
                                                        .qemu_loop = qemu_loop,
                                                        .factory = factory,
                                                        .owner = this});
        const std::lock_guard<std::mutex> lock(entries_mutex_);
        snapshot_devices_.push_back(qname);
    }
}

ConnectorRegistry::~ConnectorRegistry() {
    WithdrawSnapshotTarget();
    const std::lock_guard<std::mutex> lock(entries_mutex_);
    for (const std::string& qname : snapshot_devices_) {
        HalPlugSnapshotRegistry::Get().Unregister(qname, this);
    }
}

void ConnectorRegistry::EnsureConnectorLoaderRegistered() {
    // RegisterPlugLoader() only returns true the first time; a second call for
    // the same id means the loader is already there, which is what we want.
    static const bool kRegistered = cable::RegisterPlugLoader(
            std::string(Connector::kSnapshotTypeId), &ConnectorRegistry::LoadConnectorFromSnapshot);
    (void)kRegistered;
}

void ConnectorRegistry::PublishSnapshotTarget() {
    const absl::MutexLock lock(&SnapshotMutex());
    auto& registries = GetSnapshotRegistries();
    const auto [it, inserted] = registries.insert_or_assign(snapshot_id_, this);
    LOG_IF(WARNING, !inserted) << "ConnectorRegistry: replacing the registry listening under '"
                               << snapshot_id_ << "' for Connector snapshots";
    snapshot_target_published_ = true;
}

void ConnectorRegistry::WithdrawSnapshotTarget() {
    const absl::MutexLock lock(&SnapshotMutex());
    if (!snapshot_target_published_) {
        return;
    }
    auto& registries = GetSnapshotRegistries();
    const auto it = registries.find(snapshot_id_);
    if (it != registries.end() && it->second == this) {
        registries.erase(it);
    }
    snapshot_target_published_ = false;
}

std::optional<Connector::Target> ConnectorRegistry::FindSnapshotTarget(
        const std::string_view snapshot_id) {
    const absl::MutexLock lock(&SnapshotMutex());
    const auto& registries = GetSnapshotRegistries();
    const auto it = registries.find(snapshot_id);
    if (it == registries.end()) {
        return std::nullopt;
    }
    const ConnectorRegistry& registry = *it->second;
    return Connector::Target{.ping_topic = registry.ping_topic_,
                             .devices_entries = registry.devices_.data(),
                             .devices_entries_size = registry.devices_.size()};
}

cable::PlugPtr ConnectorRegistry::CreateConnector(SocketPtr socket,
                                                  const std::string_view snapshot_id) {
    EnsureConnectorLoaderRegistered();
    const std::optional<Connector::Target> target = FindSnapshotTarget(snapshot_id);
    if (!target.has_value()) {
        return nullptr;
    }
    return std::make_shared<Connector>(std::move(socket), target->ping_topic,
                                       target->devices_entries, target->devices_entries_size,
                                       std::string(snapshot_id));
}

cable::PlugOrSocket ConnectorRegistry::LoadConnectorFromSnapshot(SocketPtr socket,
                                                                 archive::IReader& reader) {
    return Connector::LoadFromSnapshot(std::move(socket), reader,
                                       &ConnectorRegistry::FindSnapshotTarget);
}

}  // namespace goldfish::devices
