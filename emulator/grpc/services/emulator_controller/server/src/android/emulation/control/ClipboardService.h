// Copyright (C) 2024 The Android Open Source Project
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
#pragma once
#include <grpcpp/grpcpp.h>

#include <atomic>

#include "aemu/base/events/EventSources.h"
#include "android/clipboard/ClipboardDevice.h"
#include "android/emulation/control/utils/grpc_event_stream_support.h"
#include "emulator_controller.grpc.pb.h"
#include "goldfish/devices/connector_registry.h"

namespace android {
namespace emulation {
namespace control {

using ::goldfish::devices::ConnectorRegistry;
using ::goldfish::devices::clipboard::ClipboardData;
using ::goldfish::devices::clipboard::IClipboardDevice;
using grpc::Status;

/**
 * @brief Represents a clipboard event.
 *
 * This structure encapsulates information about a change in clipboard
 * data, including the source of the change and the new clipboard data.
 */
struct ClipboardEvent {
    std::string source;  ///< The source of the clipboard change (e.g., the peer ID).
    ClipData data;       ///< The new clipboard data.
};

/**
 * @brief Implements the clipboard-related gRPC APIs.
 *
 * This class handles clipboard interactions between the host and guest,
 * supporting both streaming and setting/getting clipboard data.  It manages
 * the registration of listeners and callbacks to handle clipboard updates,
 * ensuring efficient data transfer and synchronization. The service also
 * filters out duplicate clipboard events to prevent unnecessary updates to
 * listeners.
 */
class ClipboardServiceImpl : public CallbackEventSource<ClipboardEvent> {
  public:
    ClipboardServiceImpl(ConnectorRegistry* connectorRegistry) : mRegistry(connectorRegistry) {}
    ~ClipboardServiceImpl();

    /**
     * @brief Streams clipboard data to the client.
     *
     * This method streams clipboard updates to the client as they occur.
     * It handles two scenarios:
     *
     * 1. Clipboard data changes within the guest: Updates are sent to
     *    listeners only if the data is different from the last event.
     * 2. Clipboard data is sent from a peer: Updates are delivered to all
     *    other peers, excluding the sender.
     *
     * @param context The server context for the gRPC call.
     * @param request An empty request message.
     * @return A gRPC server write reactor for streaming `ClipData`.
     */
    ::grpc::ServerWriteReactor<ClipData>* streamClipboard(std::string peerId);

    Status getClipboard(ClipData* reply);

    /**
     * @brief Sets the clipboard contents.
     *
     * This method sets the clipboard data to the provided `ClipData`.  It
     * notifies all registered listeners of the update, except for the peer
     * that initiated the change.
     *
     * @param context The server context for the gRPC call.
     * @param clipData The new clipboard data.
     * @param reply An empty response message.
     * @return A gRPC status indicating the success or failure of the
     *         operation.
     */
    Status setClipboard(std::string source, const ClipData& clipData);

    static std::string getPeerId(const ::grpc::ServerContextBase& context);

  private:
    ConnectorRegistry* mRegistry;
    IClipboardDevice::CallbackId mClipboardListenerId{0};  ///< The ID of the clipboard listener.
    std::atomic_bool mClipboardListenerRegistered{false};  ///< Whether the listener is registered.
};

}  // namespace control
}  // namespace emulation
}  // namespace android
