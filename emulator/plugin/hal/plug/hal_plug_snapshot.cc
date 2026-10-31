/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "goldfish/devices/hal_plug_snapshot.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/base/no_destructor.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/hal_plug_to_i_plug_adapter.h"

namespace goldfish::devices {

HalPlugSnapshotRegistry& HalPlugSnapshotRegistry::Get() {
    static absl::NoDestructor<HalPlugSnapshotRegistry> instance;
    return *instance;
}

void HalPlugSnapshotRegistry::Register(std::string device, Entry entry) {
    EnsurePlugLoaderRegistered();
    const absl::MutexLock lock(&mutex_);
    VLOG(1) << "Registering snapshot factory for HalPlug device '" << device << "'";
    entries_.insert_or_assign(std::move(device), std::move(entry));
}

void HalPlugSnapshotRegistry::Unregister(std::string_view device, const void* owner) {
    const absl::MutexLock lock(&mutex_);
    const auto it = entries_.find(device);
    if (it != entries_.end() && (owner == nullptr || it->second.owner == owner)) {
        entries_.erase(it);
    }
}

std::optional<HalPlugSnapshotRegistry::Entry> HalPlugSnapshotRegistry::Find(
        std::string_view device) const {
    const absl::MutexLock lock(&mutex_);
    const auto it = entries_.find(device);
    if (it == entries_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool HalPlugSnapshotRegistry::EnsurePlugLoaderRegistered() {
    // RegisterPlugLoader() only returns true the first time; a second call for
    // the same id means the loader is already there, which is what we want.
    static const bool kRegistered =
            cable::RegisterPlugLoader(std::string(HalPlugToIPlugAdapter::kSnapshotTypeId),
                                      &HalPlugSnapshotRegistry::LoadPlug);
    (void)kRegistered;
    return true;
}

cable::PlugOrSocket HalPlugSnapshotRegistry::LoadPlug(cable::SocketPtr socket,
                                                      archive::IReader& reader) {
    return HalPlugToIPlugAdapter::RestoreFromSnapshot(
            std::move(socket), reader,
            [](std::string_view device) { return HalPlugSnapshotRegistry::Get().Find(device); });
}

}  // namespace goldfish::devices
