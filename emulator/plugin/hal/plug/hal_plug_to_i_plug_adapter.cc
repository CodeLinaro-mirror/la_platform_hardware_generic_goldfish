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
#include "goldfish/devices/hal_plug_to_i_plug_adapter.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/archive/collections/string.h"
#include "goldfish/archive/deque_archive.h"
#include "goldfish/devices/marshalling_hal_socket.h"
#include "goldfish/perfetto/perfetto_categories.h"

namespace goldfish::devices {

namespace {
// Upper bounds for values read back from a snapshot, so a corrupt archive
// cannot make us allocate arbitrary amounts of memory.
constexpr size_t kMaxSnapshotIdentityBytes = 4096;
constexpr size_t kMaxSnapshotBufferBytes = static_cast<size_t>(64) << 20;  // 64 MiB
}  // namespace

HalPlugToIPlugAdapter::~HalPlugToIPlugAdapter() {
    VLOG(1) << "Tearing down " << *this << " with mHalPlug: " << *hal_plug_
            << ", use_count: " << hal_plug_.use_count();
}

HalPlugToIPlugAdapter::HalPlugToIPlugAdapter(async::EventLoop* client_loop,
                                             std::shared_ptr<HalPlug> hal_plug,
                                             std::optional<HalPlugSnapshotIdentity> identity)
        : client_loop_(client_loop)
        , hal_plug_(std::move(hal_plug))
        , identity_(std::move(identity)) {
    DCHECK(hal_plug_) << "hal_plug_ is nullptr";
    receive_buffer_.reserve(kInitialBufferSize);
    deliver_buffer_.reserve(kInitialBufferSize);
    VLOG(1) << "HalPlugToIPlugAdapter created with mHalPlug: " << *hal_plug_
            << ", use_count: " << hal_plug_.use_count();
}

void HalPlugToIPlugAdapter::OnConnect() {
    PostOnConnect();
}

void HalPlugToIPlugAdapter::PostOnConnect() {
    // Let's inform the client of the new connection.
    VLOG(1) << "Scheduling OnConnect for mHalPlug: " << *hal_plug_;
    client_loop_
            ->Post([this, self = shared_from_this()]() {
                const absl::MutexLock lock(&callback_mutex_);
                hal_plug_->OnConnect();
            })
            .IgnoreError();
}

bool HalPlugToIPlugAdapter::OnReceive(const void* data, size_t size) {
    // This is called on the QEMU thread.
    // We coalesce incoming chunks and post a single delivery task to the
    // client loop for the real HalPlug to handle.
    // We return true immediately, preventing the QEMU thread from blocking.
    bool schedule_delivery = false;
    uint64_t trace_flow_id = 0;
    {
        const absl::MutexLock lock(&receive_buffer_mutex_);
        schedule_delivery = !std::exchange(has_pending_receive_, true);
        if (schedule_delivery) {
            if (TRACE_EVENT_CATEGORY_ENABLED("async")) {
                trace_flow_id = TRACE_NONCE();
            }
            receive_buffer_.append(static_cast<const char*>(data), size);
        } else {
            TRACE_EVENT("async", "HalPlugToIPlugAdapter::Coalesce", "bytes", size,
                        "coalesced_bytes", receive_buffer_.size() + size);
            receive_buffer_.append(static_cast<const char*>(data), size);
            VLOG(2) << "Coalesced " << size << " bytes for mHalPlug " << *hal_plug_
                    << " (total: " << receive_buffer_.size() << " bytes)";
            return true;
        }
    }

    TRACE_EVENT("async", "HalPlugToIPlugAdapter::OnReceive",
                perfetto::Flow::ProcessScoped(trace_flow_id), "bytes", size);
    VLOG(2) << "Scheduling onReceive for mHalPlug " << *hal_plug_ << " with: " << size << " bytes.";
    ScheduleDelivery(trace_flow_id);
    return true;
}

void HalPlugToIPlugAdapter::ScheduleDelivery(uint64_t trace_flow_id) {
    client_loop_
            ->Post([this, self = shared_from_this(), trace_flow_id]() {
                // callback_mutex_ spans both the swap and the delivery, so a
                // concurrent SaveStateToSnapshot() sees the bytes either still
                // in receive_buffer_ or already consumed by the HalPlug, never
                // in flight in deliver_buffer_.
                const absl::MutexLock callback_lock(&callback_mutex_);
                {
                    const absl::MutexLock lock(&receive_buffer_mutex_);
                    DCHECK(has_pending_receive_);
                    has_pending_receive_ = false;
                    receive_buffer_.swap(deliver_buffer_);
                }
                if (restore_failed_) {
                    // The plug could not restore and its socket is closing;
                    // bytes the guest sent meanwhile are dropped with it.
                    deliver_buffer_.clear();
                    return;
                }
                TRACE_EVENT("async", "HalPlugToIPlugAdapter::Deliver",
                            perfetto::Flow::ProcessScoped(trace_flow_id), "bytes",
                            deliver_buffer_.size());
                hal_plug_->OnReceive(deliver_buffer_);
                deliver_buffer_.clear();
            })
            .IgnoreError();
}

cable::SocketPtr HalPlugToIPlugAdapter::OnUnplug() {
    // This is called on the QEMU thread when the guest disconnects.
    // We must fulfill the IPlug contract by returning the SocketPtr.
    //
    // First yank the socket, we do not want to start an immediate race
    // with the post call we make below that can also close the socket.
    //
    // Note: the marshalling socket can be a NullSocket if someone else was just
    // ahead of us when closing.

    // We must keep ourselves alive during the unplugging process,
    // as the close() call can trigger our own destruction on another thread.
    auto self = shared_from_this();
    auto marshalling_socket = std::static_pointer_cast<MarshallingHalSocket>(hal_plug_->Socket());
    VLOG(1) << "Closing and releasing " << *marshalling_socket;
    marshalling_socket->Close();
    auto released_socket = marshalling_socket->Release();

    // Now notify the client that we are no longer alive.
    VLOG(1) << "Scheduling onClose for mHalPlug:" << *hal_plug_;
    client_loop_
            ->Post([this, self]() {
                VLOG(1) << "Calling OnClose from client thread on " << *hal_plug_;
                const absl::MutexLock lock(&callback_mutex_);
                hal_plug_->OnClose();
            })
            .IgnoreError();

    return released_socket;
}

bool HalPlugToIPlugAdapter::SupportsLoadingFromSnapshot() const {
    return identity_.has_value() && hal_plug_->SupportsSnapshot();
}

bool HalPlugToIPlugAdapter::SaveStateToSnapshot(archive::IWriter& writer) const {
    if (!SupportsLoadingFromSnapshot()) {
        return false;
    }

    // Serialize against the HalPlug callbacks running on the client loop (see
    // the threading contract in hal_plug.h). We hold no vsock lock-dependent
    // resources here other than being called with the vsock state lock held;
    // nothing under callback_mutex_ calls back into vsock synchronously
    // (HalSocket::Send/Close only post to qemu_loop), so this cannot deadlock.
    const absl::MutexLock callback_lock(&callback_mutex_);

    // Host -> guest bytes still sitting in the marshalling socket: its flush
    // task cannot run while the QEMU thread is saving. Adapters with an
    // identity are only created by WrapHalPlug()/RestoreFromSnapshot(), both
    // of which attach a MarshallingHalSocket before constructing the adapter.
    const std::string pending_send =
            std::static_pointer_cast<MarshallingHalSocket>(hal_plug_->Socket())->PendingSendBytes();

    // Guest -> host bytes that have been coalesced but not delivered yet.
    std::string pending_receive;
    {
        const absl::MutexLock lock(&receive_buffer_mutex_);
        pending_receive = receive_buffer_;
    }

    // The HalPlug state goes into its own blob so a loader can skip or
    // validate it without understanding the plug's layout.
    archive::DequeArchive plug_archive;
    if (!hal_plug_->SaveToSnapshot(plug_archive)) {
        LOG(WARNING) << "HalPlug " << *hal_plug_ << " failed to save its snapshot state.";
        return false;
    }
    const std::string plug_state(plug_archive.storage.begin(), plug_archive.storage.end());

    writer << kSnapshotVersion << std::string_view(identity_->device)
           << std::string_view(identity_->args) << std::string_view(pending_send)
           << std::string_view(pending_receive) << std::string_view(plug_state);

    VLOG(1) << "Saved snapshot state of " << *this << " (device: " << identity_->device
            << ", pending_send: " << pending_send.size()
            << " bytes, pending_receive: " << pending_receive.size()
            << " bytes, plug_state: " << plug_state.size() << " bytes)";
    return true;
}

void HalPlugToIPlugAdapter::RestorePendingReceive(std::string pending) {
    uint64_t trace_flow_id = 0;
    bool schedule = false;
    {
        const absl::MutexLock lock(&receive_buffer_mutex_);
        // Bytes the guest sent since the load may already be queued (and their
        // delivery posted behind this restore task); the snapshot bytes were
        // sent first, so they go first.
        pending.append(receive_buffer_);
        receive_buffer_ = std::move(pending);
        schedule = !has_pending_receive_;
        has_pending_receive_ = true;
        if (schedule && TRACE_EVENT_CATEGORY_ENABLED("async")) {
            trace_flow_id = TRACE_NONCE();
        }
    }
    if (schedule) {
        ScheduleDelivery(trace_flow_id);
    }
}

cable::PlugOrSocket HalPlugToIPlugAdapter::RestoreFromSnapshot(
        cable::SocketPtr socket, archive::IReader& reader, const FindRestoreTarget& find_factory) {
    uint32_t version = 0;
    HalPlugSnapshotIdentity identity;
    std::string pending_send;
    std::string pending_receive;
    std::string plug_state;
    if (!ReadValue(reader, version, identity.device, identity.args, pending_send, pending_receive,
                   plug_state)
                 .ok()) {
        LOG(WARNING) << "Corrupt HalPlug snapshot.";
        return socket;
    }
    if (version != kSnapshotVersion) {
        LOG(WARNING) << "Unsupported HalPlug snapshot version " << version;
        return socket;
    }
    if (identity.device.size() > kMaxSnapshotIdentityBytes ||
        identity.args.size() > kMaxSnapshotIdentityBytes ||
        pending_send.size() > kMaxSnapshotBufferBytes ||
        pending_receive.size() > kMaxSnapshotBufferBytes ||
        plug_state.size() > kMaxSnapshotBufferBytes) {
        LOG(WARNING) << "HalPlug snapshot for '" << identity.device << "' has oversized fields.";
        return socket;
    }

    const std::optional<RestoreTarget> target = find_factory(identity.device);
    if (!target || !target->factory || !target->client_loop || !target->qemu_loop) {
        LOG(WARNING) << "No HalPlug factory registered for '" << identity.device
                     << "'; cannot restore plug from snapshot.";
        return socket;
    }

    // Like a fresh connection, the HalPlug is constructed on the QEMU thread.
    std::shared_ptr<HalPlug> hal_plug = target->factory(identity.args);
    if (!hal_plug) {
        LOG(WARNING) << "Factory for '" << identity.device << "' returned no HalPlug.";
        return socket;
    }
    if (!hal_plug->SupportsSnapshot()) {
        LOG(WARNING) << "HalPlug for '" << identity.device
                     << "' no longer supports snapshots; cannot restore plug from snapshot.";
        return socket;
    }

    // From here on we own the socket; every failure is reported by closing it.
    auto marshalling_socket =
            std::make_shared<MarshallingHalSocket>(std::move(socket), target->qemu_loop);
    hal_plug->EstablishConnection(marshalling_socket);

    auto adapter = std::make_shared<HalPlugToIPlugAdapter>(target->client_loop, hal_plug,
                                                           std::move(identity));

    // 1. Re-queue what the host had not flushed before the snapshot. Send()
    //    only posts to qemu_loop, so this is safe under the vsock state lock.
    //    It goes first so it stays ahead of anything OnRestore() sends.
    if (!pending_send.empty()) {
        marshalling_socket->Send(pending_send);
    }

    // 2. Restore the plug state and resume it on the client loop. This is
    //    the first task the new plug sees on its loop, so it runs before any
    //    OnReceive() for bytes the guest sends after the load. Only once the
    //    plug has restored successfully are the guest bytes it had not yet
    //    consumed before the snapshot handed to it (3.); a plug that failed to
    //    restore is closed and must not see any callback at all.
    target->client_loop
            ->Post([adapter, plug_state = std::move(plug_state),
                    pending_receive = std::move(pending_receive)]() mutable {
                const absl::MutexLock lock(&adapter->callback_mutex_);
                archive::DequeArchive archive;
                archive.storage.assign(plug_state.begin(), plug_state.end());
                HalPlug& plug = *adapter->hal_plug_;
                if (!plug.LoadFromSnapshot(archive)) {
                    LOG(ERROR) << "HalPlug " << plug
                               << " failed to load its snapshot state; closing the connection.";
                    adapter->restore_failed_ = true;
                    plug.Socket()->Close();
                    return;
                }
                VLOG(1) << "Restored " << plug << " from snapshot.";
                plug.OnRestore();
                // 3. Deliver what the guest had sent before the snapshot.
                if (!pending_receive.empty()) {
                    adapter->RestorePendingReceive(std::move(pending_receive));
                }
            })
            .IgnoreError();

    return cable::PlugPtr(std::move(adapter));
}

}  // namespace goldfish::devices
