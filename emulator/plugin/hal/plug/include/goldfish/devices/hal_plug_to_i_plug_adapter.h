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
/**
 * @file HalPlugToIPlugAdapter.h
 * @brief Defines the adapter that bridges the IPlug interface to the HalPlug
 * interface.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/archive/reader.h"
#include "goldfish/archive/writer.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/internal/hal_plug.h"

namespace goldfish::devices {

class MarshallingHalSocket;

/**
 * @brief Identifies which registered device factory created a HalPlug.
 *
 * Saved alongside the HalPlug state so the snapshot loader can re-create the
 * plug through the same factory (see `HalPlugSnapshotRegistry`).
 */
struct HalPlugSnapshotIdentity {
    /// Registry key of the device, e.g. "qsensors" ('q' = qemud, '-' = plain).
    std::string device;
    /// The `args` the guest passed when it connected (`pipe:<name>:<args>`).
    std::string args;
};

/**
 * @class HalPlugToIPlugAdapter
 * @brief An internal implementation of the IPlug interface that acts as an
 * adapter.
 *
 * It wraps a HalPlug and is responsible for marshalling calls from the QEMU
 * event loop to the client event loop where the real HalPlug lives.
 *
 * ### Snapshots
 *
 * The adapter is snapshottable (`SupportsLoadingFromSnapshot()`) when it was
 * given a `HalPlugSnapshotIdentity` **and** the wrapped plug returns
 * `HalPlug::SupportsSnapshot()`. Otherwise the vsock layer resets the stream
 * on load (the guest sees a RST). The snapshot contains the identity, bytes
 * buffered in either direction that have not reached their destination yet,
 * and the opaque `HalPlug::SaveToSnapshot()` blob.
 *
 * `SaveStateToSnapshot()` runs on the QEMU thread while the client loop keeps
 * running. `callback_mutex_` serializes it against every HalPlug callback the
 * adapter dispatches (`OnConnect`, `OnReceive`, `OnClose`, `LoadFromSnapshot`,
 * `OnRestore`), which is what lets a HalPlug snapshot its state without a lock
 * of its own (see the threading contract in hal_plug.h).
 */
class HalPlugToIPlugAdapter : public cable::IPlug,
                              public std::enable_shared_from_this<HalPlugToIPlugAdapter> {
  public:
    /// `IPlug::TypeId` under which the adapter's snapshot loader is registered.
    static constexpr std::string_view kSnapshotTypeId = "HalPlugAdapter";

    /**
     * @brief Constructs a new HalPlugToIPlugAdapter.
     * @param client_loop The event loop on which the HalPlug's methods should be
     * invoked.
     * @param hal_plug The real HalPlug instance to which calls will be
     * forwarded.
     * @param identity How to find the plug's factory again when loading a
     * snapshot. Without it the adapter is never snapshotted.
     */
    HalPlugToIPlugAdapter(async::EventLoop* client_loop, std::shared_ptr<HalPlug> hal_plug,
                          std::optional<HalPlugSnapshotIdentity> identity = std::nullopt);
    ~HalPlugToIPlugAdapter() override;
    /**
     * @brief Called on the QEMU thread when the connection is established.
     *
     * This method is part of the IPlug interface. It marshals the call to the
     * HalPlug's onConnect method on the client event loop.
     */
    void OnConnect() override;

    /**
     * @brief Called on the QEMU thread when data is received from the guest.
     *
     * This method is part of the IPlug interface. It coalesces incoming guest
     * chunks into a pre-reserved buffer and marshals delivery to the HalPlug's
     * OnReceive method on the client event loop.
     * @param data A pointer to the received data.
     * @param size The size of the received data.
     * @return Always returns true, as the actual processing is asynchronous.
     */
    bool OnReceive(const void* data, size_t size) override;

    /**
     * @brief Called on the QEMU thread when the connection is unplugged.
     *
     * This method is part of the IPlug interface. It marshals the call to the
     * HalPlug's onClose method on the client event loop.
     * @return A null SocketPtr, as the connection is terminated.
     */
    cable::SocketPtr OnUnplug() override;

    /// True iff an identity was given and the HalPlug opted in to snapshots.
    bool SupportsLoadingFromSnapshot() const override;
    TypeId GetSnapshotTypeId() const override { return std::string(kSnapshotTypeId); }
    /**
     * @brief Saves identity, undelivered bytes and the HalPlug state.
     *
     * Called on the QEMU thread with the vsock state lock held; blocks until
     * any in-flight HalPlug callback on the client loop has returned.
     */
    bool SaveStateToSnapshot(archive::IWriter& writer) const override;

    /**
     * @brief Re-creates an adapter (and its HalPlug) from a snapshot.
     *
     * The counterpart of `SaveStateToSnapshot()`. Reads the identity, looks
     * up the factory with `find_factory`, creates the HalPlug on the calling
     * (QEMU) thread, attaches it to `socket`, and posts `LoadFromSnapshot()`
     * + `OnRestore()` followed by any undelivered guest bytes to the client
     * loop. Undelivered host bytes are re-sent through the new socket.
     *
     * @return The new adapter, or `socket` back if the snapshot could not be
     * read, no factory is registered for the device, or the plug it created
     * does not support snapshots.
     */
    struct RestoreTarget {
        async::EventLoop* client_loop = nullptr;
        async::EventLoop* qemu_loop = nullptr;
        std::function<std::shared_ptr<HalPlug>(std::string_view args)> factory;
        const void* owner = nullptr;
    };
    using FindRestoreTarget = std::function<std::optional<RestoreTarget>(std::string_view device)>;
    static cable::PlugOrSocket RestoreFromSnapshot(cable::SocketPtr socket,
                                                   archive::IReader& reader,
                                                   const FindRestoreTarget& find_factory);

    /**
     * @brief Posts `HalPlug::OnConnect()` to the client loop.
     *
     * Used by the factory for registry-created plugs, where the vsock layer
     * delivered `IPlug::OnConnect()` to the `Connector` before switching plugs.
     */
    void PostOnConnect();

    const std::shared_ptr<HalPlug>& GetHalPlug() const { return hal_plug_; }
    const std::optional<HalPlugSnapshotIdentity>& GetSnapshotIdentity() const { return identity_; }

  protected:
    void AbslStringifyImpl(absl::FormatSink& s) const override {
        absl::Format(&s, "[IPlugAdapter: client: %p, halPlug: %v]", client_loop_, *hal_plug_);
    }

  private:
    // Default socket buffer size
    static constexpr size_t kInitialBufferSize = 64 * 1024;
    // Snapshot layout version written by SaveStateToSnapshot().
    static constexpr uint32_t kSnapshotVersion = 1;

    // Posts the task that hands receive_buffer_ to the HalPlug. Requires
    // has_pending_receive_ to have just been set by the caller.
    void ScheduleDelivery(uint64_t trace_flow_id);
    // Puts the guest bytes restored from a snapshot *ahead* of anything the
    // guest sent since the load, and schedules their delivery if none is
    // pending. Runs on the client loop from the restore task, after
    // LoadFromSnapshot() + OnRestore() succeeded, so a plug that failed to
    // restore never sees them.
    void RestorePendingReceive(std::string pending) ABSL_EXCLUSIVE_LOCKS_REQUIRED(callback_mutex_)
            ABSL_LOCKS_EXCLUDED(receive_buffer_mutex_);

    async::EventLoop* client_loop_;
    const std::shared_ptr<HalPlug> hal_plug_;
    const std::optional<HalPlugSnapshotIdentity> identity_;

    std::string receive_buffer_ ABSL_GUARDED_BY(receive_buffer_mutex_);
    std::string deliver_buffer_ ABSL_GUARDED_BY(callback_mutex_);

    // Set when LoadFromSnapshot() failed: the connection is being closed and
    // no further HalPlug callback (OnReceive in particular) may run.
    bool restore_failed_ ABSL_GUARDED_BY(callback_mutex_) = false;
    bool has_pending_receive_ ABSL_GUARDED_BY(receive_buffer_mutex_) = false;

    // Held on the client loop around every HalPlug callback and on the QEMU
    // thread for the duration of SaveStateToSnapshot(). Never held while
    // calling into the vsock layer, so it cannot invert with its state lock.
    // Acquired before receive_buffer_mutex_ when both are needed.
    mutable absl::Mutex callback_mutex_;
    mutable absl::Mutex receive_buffer_mutex_ ABSL_ACQUIRED_AFTER(callback_mutex_);
};

}  // namespace goldfish::devices
