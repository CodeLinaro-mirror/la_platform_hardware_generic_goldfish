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

struct QEMUFile;

namespace devices {
namespace cable {

struct IPlug;   // see the definition below
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
    virtual ~ISocket() {}

    /* `sendAsync` appends data to the outgoing queue and
     * asks the socket manager to send data (if connected,
     * see `IPlug::onConnect` below). If the connection is
     * not fully connected yet, the data will sit and wait
     * the `IPlug::onConnect` notification.
     */
    virtual void sendAsync(const void *data, size_t size) = 0;

    /* `switchPlug` is used to switch plugs connected to a
     * socket, e.g. if you need to swicth the wire protocol.
     */
    virtual PlugPtr switchPlug(PlugPtr newPlug) = 0;

    struct Unplugger {
        void operator()(ISocket *s) const {
            s->unplugImpl();
        }
    };

    using Ptr = std::unique_ptr<ISocket, ISocket::Unplugger>;

    /* Should be called from `IPlug` owning this `ISocket`.
     * It causes `ISocket` to be destroyed and since it is
     * destroyed, there is no place to keep `PlugPtr`, so
     * it is returned to avoid `~IPlug` called unexpectedly.
     */
    static PlugPtr unplug(Ptr socket) {
        PlugPtr plug = socket->unplugImpl();
        socket.release();  // `~ISocket` was called in `unplugImpl`
        return plug;
    }

protected:
    /* `unplugImpl` destroys the `ISocket` instance in the
     * internal socket manager data structures (`~ISocket`
     * will be called).
     */
    virtual PlugPtr unplugImpl() = 0;
};

using SocketPtr = ISocket::Ptr;

/*
 * `IPlug` represents an entity to receive events from a socket
 * manager (e.g. virtio-vsock).
 */
struct IPlug {
    using TypeId = std::string;

    virtual ~IPlug() {}

    /* `onConnect` is called when the connection is ready to use,
     * see `ISocket::sendAsync` above.
     */
    virtual void onConnect() = 0;

    /* `onReceive` is called when there is data to process.
     * There is no way to process data partially (you will
     * have to store the unprocessed somewhere on your end).
     * If `onReceive` returns `false`, `onUnplug` will be
     * called after.
     */
    virtual bool onReceive(const void *data, size_t size) = 0;

    /* `onUnplug` is called when the remote party hangs up.
     * Please note this method is not called if `IPlug`
     * itself hangs up.
     */
    virtual SocketPtr onUnplug() = 0;

    /* `supportsLoadingFromSnapshot`, `getSnapshotTypeId` and
     * `saveStateToSnapshot` are used to handle snapshot saving.
     * If your `IPlug` does not support loading from a snapshot
     * you don't have to do anything.
     * Otherwise you need to return `true` from
     * `supportsLoadingFromSnapshot`, to assign an unique type id
     * (returned by `getSnapshotTypeId`) and to implement
     * `saveStateToSnapshot`. To load your `IPlug` you will have
     * to implement `PlugLoader` (see below) and to register it
     * using `registerPlugLoader` (see below).
     */
    virtual bool supportsLoadingFromSnapshot() const { return false; }
    virtual TypeId getSnapshotTypeId() const { return {}; }
    virtual bool saveStateToSnapshot(QEMUFile *) const { return false; };
};

using PlugOrSocket = std::variant<PlugPtr, SocketPtr>;

/* `PlugLoader` represents a function which loads `IPlug`
 * from `QEMUFile` (a snapshot). If it cannot load a plug,
 * it must return the `SocketPtr` back.
 */
using PlugLoader = std::function<PlugOrSocket(SocketPtr, QEMUFile *)>;

bool registerPlugLoader(IPlug::TypeId, PlugLoader);

}  // namespace cable
}  // namespace devices
