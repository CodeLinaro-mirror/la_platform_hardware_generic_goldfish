/* Copyright (C) 2024 The Android Open Source Project
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#pragma once
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <variant>

#include "absl/strings/str_format.h"

#include "goldfish/archive/reader.h"
#include "goldfish/archive/writer.h"

namespace goldfish::devices::cable {

struct IDataSniffer {
    virtual ~IDataSniffer() = default;
    virtual void ToSocket(const void* data, size_t data_size) = 0;
    virtual void ToPlug(const void* data, size_t data_size) = 0;
};

struct IPlug;  // see the definition below
using PlugPtr = std::shared_ptr<IPlug>;

/* `ISocket` represents an entity to control a connection
 * lifetime and to send events (`sendAsync`, `switchPlug`
 * and `unplug`) to a socket manager from `IPlug`.
 *
 * `IPlug` is expected to hold `SocketPtr` inside itsef
 * which causes a circular dependency (`ISocket` points to
 * `IPlug` and `IPlug` points to `ISocket`). This circle
 * is broken by `IPlug::onUnplug`, `ISocket::unplug` or
 * `~SocketPtr`.
 */
struct ISocket {
    virtual ~ISocket() = default;

    using OnFlowControlEvent = std::function<void(bool enable_reading)>;

    virtual void SetOnFlowControlEvent(OnFlowControlEvent);

    /* `sendAsync` appends data to the outgoing queue and
     * asks the socket manager to send data (if connected,
     * see `IPlug::onConnect` below). If the connection is
     * not fully connected yet, the data will sit and wait
     * the `IPlug::onConnect` notification.
     */
    virtual void SendAsync(const void* data, size_t size) = 0;

    /* `switchPlug` is used to switch plugs connected to a
     * socket, e.g. if you need to switch the wire protocol.
     */
    virtual PlugPtr SwitchPlug(PlugPtr new_plug) = 0;

    struct Unplugger {
        void operator()(ISocket* s) const { s->UnplugImpl(); }
    };

    using Ptr = std::unique_ptr<ISocket, ISocket::Unplugger>;

    /* Should be called from `IPlug` owning this `ISocket`.
     * It causes `ISocket` to be destroyed and since it is
     * destroyed, there is no place to keep `PlugPtr`, so
     * it is returned to avoid `~IPlug` called unexpectedly.
     */
    static PlugPtr Unplug(Ptr socket) {
        PlugPtr plug = socket->UnplugImpl();
        socket.release();  // `~ISocket` was called in `unplugImpl` NOLINT
        return plug;
    }

    virtual void SetDataSniffer(std::unique_ptr<IDataSniffer> sniffer) {}

  protected:
    /* `unplugImpl` destroys the `ISocket` instance in the
     * internal socket manager data structures (`~ISocket`
     * will be called).
     */
    virtual PlugPtr UnplugImpl() = 0;
    virtual void AbslStringifyImpl(absl::FormatSink& s) const { absl::Format(&s, "[ISocket]"); }
    friend void AbslStringify(absl::FormatSink& s, const ISocket& socket);
};

inline void AbslStringify(absl::FormatSink& s, const ISocket& socket) {
    socket.AbslStringifyImpl(s);
}
std::ostream& operator<<(std::ostream& os, const ISocket& socket);

using SocketPtr = ISocket::Ptr;

/*
 * `IPlug` represents an entity to receive events from a socket
 * manager (e.g. virtio-vsock).
 */
struct IPlug {
    using TypeId = std::string;

    virtual ~IPlug() = default;

    /* `onConnect` is called when the connection is ready to use,
     * see `ISocket::sendAsync` above. It is called only once, so
     * if you replace plugs (with `ISocket::switchPlug`),
     * `onConnect` will not be called for the new plug.
     */
    virtual void OnConnect() {}

    /* `onReceive` is called when there is data to process.
     * There is no way to process data partially (you will
     * have to store the unprocessed somewhere on your end).
     * If `onReceive` returns `false`, `onUnplug` will be
     * called after.
     */
    virtual bool OnReceive(const void* data, size_t size) = 0;

    /* `onUnplug` is called when the remote party hangs up.
     * Please note this method is not called if `IPlug`
     * itself hangs up.
     */
    virtual SocketPtr OnUnplug() = 0;

    /* `supportsLoadingFromSnapshot`, `getSnapshotTypeId` and
     * `saveStateToSnapshot` are used to handle snapshot saving.
     * If your `IPlug` does not support loading from a snapshot
     * you don't have to do anything.
     * Otherwise you need to return `true` from
     * `supportsLoadingFromSnapshot`, to assign an unique type id
     * (returned by `getSnapshotTypeId`) and to implement
     * `saveStateToSnapshot`. To load your `IPlug` you will have
     * to implement `PlugLoader` (see below) and to register it
     * using `RegisterPlugLoader` (see below).
     */
    virtual bool SupportsLoadingFromSnapshot() const { return false; }
    virtual TypeId GetSnapshotTypeId() const { return {}; }
    virtual bool SaveStateToSnapshot(archive::IWriter&) const { return false; };

  protected:
    virtual void AbslStringifyImpl(absl::FormatSink& s) const { absl::Format(&s, "[IPlug]"); }
    friend void AbslStringify(absl::FormatSink& s, const IPlug& plug);
};

inline void AbslStringify(absl::FormatSink& s, const IPlug& plug) {
    plug.AbslStringifyImpl(s);
}

std::ostream& operator<<(std::ostream& os, const IPlug& plug);
using PlugOrSocket = std::variant<PlugPtr, SocketPtr>;

/* `PlugLoader` represents a function which loads `IPlug`
 * from `QEMUFile` (a snapshot). If it cannot load a plug,
 * it must return the `SocketPtr` back.
 */
using PlugLoader = std::function<PlugOrSocket(SocketPtr, archive::IReader&)>;

bool RegisterPlugLoader(IPlug::TypeId, PlugLoader);

}  // namespace goldfish::devices::cable
