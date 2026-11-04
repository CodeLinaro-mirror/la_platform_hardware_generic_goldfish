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

#include <memory>
#include <string>
#include <string_view>

#include "absl/base/no_destructor.h"
#include "absl/log/check.h"
#include "absl/strings/str_format.h"

#include "goldfish/archive/reader.h"
#include "goldfish/archive/writer.h"

namespace goldfish::devices {

class HalPlugFactory;
class HalPlugTesting;
class HalPlugToIPlugAdapter;
/**
 * @class HalSocket
 * @brief A simplified, thread-safe socket handle for HALs.
 *
 * This interface is provided to a HalPlug to allow it to send data to the
 * guest and manage the connection lifetime from its own event loop.
 */
class HalSocket {
  public:
    virtual ~HalSocket() = default;

    /**
     * @brief Asynchronously sends a message to the guest.
     *
     * The implementation is responsible for marshalling this call to the
     * correct (QEMU) thread.
     * @note There are no guarantees that the data arrives in the guest.
     * @param data The message to send.
     */
    virtual void Send(std::string_view data) = 0;

    /**
     * @brief Asynchronously closes the connection.
     *
     * The implementation is responsible for marshalling this call to the
     * correct (QEMU) thread.
     */
    virtual void Close() = 0;

  protected:
    /**
     * @brief Provides a string representation for logging and debugging.
     *
     * This is the implementation hook for `absl::StrFormat`. Concrete HalSocket
     * implementations should override this method to provide meaningful
     * diagnostic information (e.g., type, state etc).
     *
     * @param s The `absl::FormatSink` to write the formatted string to.
     */
    virtual void AbslStringifyImpl(absl::FormatSink& s) const {
        absl::Format(&s, "<DefaultHalSocket>");
    }

  private:
    friend void AbslStringify(absl::FormatSink& s, const HalSocket& socket);
};

/**
 * @brief Enables `absl::StrFormat` support for `AsyncSocket`.
 *
 * This free function is the customization point that allows `AsyncSocket`
 * objects (and their derivatives) to be formatted with `absl::StrFormat`
 * using the `%v` format specifier. It delegates the actual formatting to
 * the virtual `AbslStringifyImpl` method.
 *
 * @param s The `absl::FormatSink` to write to.
 * @param socket The `AsyncSocket` to format.
 */
inline void AbslStringify(absl::FormatSink& s, const HalSocket& socket) {
    socket.AbslStringifyImpl(s);
}

/**
 * @brief Enables `std::ostream` support for `AsyncSocket`.
 *
 * This overload allows `HalSocket` objects to be streamed directly to any
 * `std::ostream` (e.g., `std::cout`, `std::stringstream`, or Abseil's `VLOG`).
 * It works by using `absl::StreamFormat` to delegate the formatting to the
 * `AbslStringify` customization point, ensuring a consistent string
 * representation.
 *
 * @param os The output stream to write to.
 * @param socket The `HalSocket` to format.
 * @return A reference to the output stream.
 */
std::ostream& operator<<(std::ostream& os, const HalSocket& socket);

namespace internal {
// A non-functional socket implementation used as a safe null object.
class NullHalSocket : public HalSocket {
    void Send(std::string_view /*data*/) override {}
    void Close() override {}
};
}  // namespace internal

/**
 * @class HalPlug
 * @brief A simplified, thread-safe plug interface for HAL devices.
 *
 * This class provides a robust, thread-safe abstraction for HALs that need to
 * communicate over a vsock connection. It guarantees that all its methods
 * (`onConnect`, `onReceive`, `OnClose`) are invoked on the client-provided
 * event loop, freeing the developer from worrying about QEMU's threading model
 * or potential deadlocks.
 *
 * ### The `socket()` Invariant
 *
 * The most important contract this class provides is the lifecycle of the
 * `HalSocket`.
 *
 * **The pointer returned by `socket()` is only valid for use between the start
 * of the `onConnect()` callback and the start of the `OnClose()` callback.**
 *
 * ### Event Sequence
 *
 * Due to the nature of the underlying connection protocol, it is possible for
 * an `onReceive` event to be delivered *before* the `onConnect` event. This
 * first `onReceive` call typically contains connection parameters or arguments
 * from the guest.
 *
 * The guaranteed lifecycle is as follows:
 * 1. **(Optional) `onReceive()`**: One or more calls with initial configuration.
 *    The `socket()` is **not valid** at this point.
 * 2. **`onConnect()`**: Signals that the connection is fully established and
 *    two-way communication is possible. The `socket()` is **now valid**.
 * 3. **`onReceive()`**: Any number of subsequent data packets.
 * 4. **`OnClose()`**: Signals that the connection has been terminated. The
 *    `socket()` is **no longer valid** after this call begins.
 *
 * The framework enforces this:
 * - Before `onConnect()` is called, `socket()` will return a safe, non-functional
 *   "null" socket.
 * - After `OnClose()` has been called, `socket()` will also return a "null"
 *   socket.
 *
 * This design prevents crashes from use-after-free or null-pointer-dereference
 * errors and makes the connection lifecycle easy to reason about.
 */
class HalPlug {
  public:
    HalPlug() {
        // Start with a safe, non-functional socket. This prevents crashes if
        // the user incorrectly calls socket() before onConnect().
        static const absl::NoDestructor<std::shared_ptr<HalSocket>> null_socket(
                std::make_shared<internal::NullHalSocket>());
        socket_ = *null_socket;
    }

    virtual ~HalPlug() = default;

    /**
     * @brief Callback invoked when the connection is fully established.
     *
     * This method is called on the HAL's dedicated event loop and signals that
     * two-way communication with the guest is now possible. It is the point
     * at which the `socket()` becomes valid for sending data.
     *
     * @note An `onReceive` callback with initial connection parameters may
     * have been called *before* this method. Any such data should be buffered
     * and processed here.
     */
    virtual void OnConnect() = 0;

    /**
     * @brief Callback invoked when data is received from the guest.
     *
     * This method is called on the HAL's dedicated event loop for each
     * incoming data packet.
     *
     * @param data A view of the received data buffer.
     * @warning The `data` parameter is a `std::string_view` and is only valid
     * for the duration of this function call. If the data needs to be
     * stored or used later, it must be copied.
     */
    virtual void OnReceive(std::string_view data) = 0;

    /**
     * @brief Callback invoked when the connection has been terminated by the guest.
     *
     * This method is called on the HAL's dedicated event loop when the remote
     * guest actively closes the connection. After this call begins, the
     * `socket()` will no longer be valid. Implementations should perform any
     * necessary resource cleanup within this method.
     *
     * @note This callback is only triggered by **guest-initiated** disconnects.
     * It will **not** be called as a result of the host calling
     * `socket()->close()`.
     * @note Due to nature of concurrency it is possible that you wrote some bytes to a NullSocket
     * before you received the onClose callback.
     */
    virtual void OnClose() = 0;

    /**
     * @name Snapshot support (optional)
     *
     * A vsock stream whose plug does not support snapshots is **reset (RST)
     * when a snapshot is loaded**: the guest sees the connection drop and has
     * to reconnect. Guest HALs that never reconnect (e.g. the sensors HAL)
     * silently stop working after `loadvm`. A `HalPlug` can opt in to
     * surviving snapshots by overriding the four methods below.
     *
     * ### Threading contract
     *
     * - `SaveToSnapshot()` is called on the **QEMU thread** (BQL held, VM
     *   stopped, host event loops still running). The framework guarantees it
     *   never overlaps `OnConnect()`, `OnReceive()`, `OnClose()`,
     *   `LoadFromSnapshot()` or `OnRestore()`, so state that is only written
     *   from those callbacks needs no extra locking. State that is mutated
     *   from elsewhere on the client loop (timers, observable callbacks) must
     *   be guarded by the plug itself, or those paths must be read-only with
     *   respect to the snapshotted state.
     * - Because the QEMU thread blocks until the running callback returns, an
     *   opted-in plug's callbacks must **never block on the QEMU thread**
     *   (no `PostAndWait` to `qemu_loop`, no `ScopedVmLock`), or snapshot
     *   saving deadlocks. `Socket()->Send()` is asynchronous and safe.
     * - `LoadFromSnapshot()` and `OnRestore()` run on the **client loop**, in
     *   that order, on a freshly constructed plug (created by the registered
     *   `HalDeviceFactory` with the original connection `args`) before any
     *   `OnReceive()` for the restored connection. `OnConnect()` is **not**
     *   called for a restored connection; `OnRestore()` takes its place and
     *   `Socket()` is valid from the start of `LoadFromSnapshot()`. During a
     *   restore the factory runs on the QEMU thread with the vsock state lock
     *   held, one more reason it must stay lightweight and never send.
     * - Bytes the guest sent before the snapshot that had not yet been
     *   delivered are delivered through `OnReceive()` after `OnRestore()`.
     * @{
     */

    /**
     * @brief Whether this plug can be saved to and restored from a snapshot.
     *
     * Must be a constant for the lifetime of the plug. Plugs returning `false`
     * (the default) are reset on snapshot load as described above.
     */
    virtual bool SupportsSnapshot() const { return false; }

    /**
     * @brief Serializes the state needed to resume the connection.
     *
     * Called on the QEMU thread, see the threading contract above. Write a
     * version tag first so `LoadFromSnapshot()` can reject older layouts.
     * @return `false` to fail the snapshot save.
     */
    virtual bool SaveToSnapshot(archive::IWriter& /*writer*/) const { return false; }

    /**
     * @brief Restores the state written by `SaveToSnapshot()`.
     *
     * Called on the client loop on a freshly constructed plug. On failure the
     * framework closes the connection (the guest sees a disconnect).
     * @return `false` if the state is invalid.
     */
    virtual bool LoadFromSnapshot(archive::IReader& /*reader*/) { return false; }

    /**
     * @brief Called on the client loop after a successful `LoadFromSnapshot()`.
     *
     * This replaces `OnConnect()` for a restored connection: re-create timers
     * and resume any periodic work here. The default forwards to
     * `OnConnect()`.
     */
    virtual void OnRestore() { OnConnect(); }
    /** @} */

  protected:
    /**
     * @brief Provides access to the underlying `HalSocket`.
     *
     * @return A pointer to the `HalSocket` instance for this connection.
     * @warning This method adheres to the class invariant: the returned
     * pointer is only functional between the `onConnect()` and `OnClose()`
     * calls. At all other times, it will return a safe, non-functional
     * "null" socket.
     */
    std::shared_ptr<HalSocket> Socket() const {
        DCHECK(socket_) << "socket_ is nullptr";
        return socket_;
    }

    /**
     * @brief Provides a string representation for logging and debugging.
     *
     * This is the implementation hook for `absl::StrFormat`. Concrete HalPlug
     * implementations should override this method to provide meaningful
     * diagnostic information (e.g., type, state etc).
     *
     * @param s The `absl::FormatSink` to write the formatted string to.
     */
    virtual void AbslStringifyImpl(absl::FormatSink& s) const {
        absl::Format(&s, "[HalPlug socket=%v]", *Socket());
    }

  private:
    friend class HalPlugFactory;
    friend class HalPlugToIPlugAdapter;
    friend class HalPlugTesting;
    friend void AbslStringify(absl::FormatSink& s, const HalPlug& plug);

    void EstablishConnection(std::shared_ptr<HalSocket> socket) {
        DCHECK(socket) << "socket is nullptr";
        socket_ = std::move(socket);
    }
    std::shared_ptr<HalSocket> socket_;
};

/**
 * @brief Enables `absl::StrFormat` support for `AsyncSocket`.
 *
 * This free function is the customization point that allows `AsyncSocket`
 * objects (and their derivatives) to be formatted with `absl::StrFormat`
 * using the `%v` format specifier. It delegates the actual formatting to
 * the virtual `AbslStringifyImpl` method.
 *
 * @param s The `absl::FormatSink` to write to.
 * @param socket The `AsyncSocket` to format.
 */
inline void AbslStringify(absl::FormatSink& s, const HalPlug& plug) {
    plug.AbslStringifyImpl(s);
}

/**
 * @brief Enables `std::ostream` support for `AsyncSocket`.
 *
 * This overload allows `HalPlug` objects to be streamed directly to any
 * `std::ostream` (e.g., `std::cout`, `std::stringstream`, or Abseil's `VLOG`).
 * It works by using `absl::StreamFormat` to delegate the formatting to the
 * `AbslStringify` customization point, ensuring a consistent string
 * representation.
 *
 * @param os The output stream to write to.
 * @param socket The `HalPlug` to format.
 * @return A reference to the output stream.
 */
std::ostream& operator<<(std::ostream& os, const HalPlug& plug);

}  // namespace goldfish::devices
