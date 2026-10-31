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
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/archive/reader.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/hal_plug_to_i_plug_adapter.h"
#include "goldfish/devices/internal/hal_plug.h"

namespace goldfish::devices {

/**
 * @class HalPlugSnapshotRegistry
 * @brief Process-wide table that lets the snapshot loader re-create HalPlugs.
 *
 * `cable::RegisterPlugLoader` keys loaders by a global `IPlug::TypeId`, so a
 * single loader (`HalPlugToIPlugAdapter::kSnapshotTypeId`) serves every
 * HalPlug-based service. The loader reads the device key saved with the
 * stream (e.g. "qsensors") and asks this registry for the `HalDeviceFactory`
 * and event loops that `IConnectorRegistry::RegisterHal[Qemu]Device` was
 * given for that device, then builds the plug exactly like a fresh guest
 * connection would.
 *
 * `ConnectorRegistry` populates this table; HAL authors never touch it.
 * Re-registering a device replaces the previous entry, so a registry that is
 * torn down and re-created (tests, reboot) does not leave stale factories
 * behind.
 *
 * Thread-safe.
 */
class HalPlugSnapshotRegistry {
  public:
    using Entry = HalPlugToIPlugAdapter::RestoreTarget;

    /// The process-wide instance used by `ConnectorRegistry` and the loader.
    static HalPlugSnapshotRegistry& Get();

    /// Prefer `Get()`; separate instances exist for tests only.
    HalPlugSnapshotRegistry() = default;

    /**
     * @brief Registers (or replaces) the factory for `device` and makes sure
     * the adapter's `cable::PlugLoader` is registered.
     * @param device The connector registry key, prefix included ("q<name>"
     * for qemud services, "-<name>" otherwise).
     */
    void Register(std::string device, Entry entry) ABSL_LOCKS_EXCLUDED(mutex_);

    /// Removes `device` (if `owner` is non-null, only when owned by `owner`).
    void Unregister(std::string_view device, const void* owner = nullptr)
            ABSL_LOCKS_EXCLUDED(mutex_);

    std::optional<Entry> Find(std::string_view device) const ABSL_LOCKS_EXCLUDED(mutex_);

    /**
     * @brief Registers the `cable::PlugLoader` for
     * `HalPlugToIPlugAdapter::kSnapshotTypeId` (idempotent).
     *
     * Called by `Register()`; exposed so a process that only restores (never
     * registers) can still install the loader.
     * @return `true` if the loader is registered (by this call or earlier).
     */
    static bool EnsurePlugLoaderRegistered();

    /// The `cable::PlugLoader` that `EnsurePlugLoaderRegistered()` installs.
    static cable::PlugOrSocket LoadPlug(cable::SocketPtr socket, archive::IReader& reader);

  private:
    mutable absl::Mutex mutex_;
    absl::flat_hash_map<std::string, Entry> entries_ ABSL_GUARDED_BY(mutex_);
};

}  // namespace goldfish::devices
