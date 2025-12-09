// Copyright (C) 2018 The Android Open Source Project
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
#include "android/emulation/control/ClipboardService.h"

#include "absl/log/log.h"

namespace android {
namespace emulation {
namespace control {

using goldfish::avd_universe::clipboard::ClipboardData;

namespace {
ClipboardEvent toClipboardEvent(const ClipboardData& clip) {
    using namespace std::literals::string_literals;

    ClipboardEvent result;
    result.source = "android"s;
    result.data.set_text(clip.contents);
    return result;
}

/**
 * @brief A gRPC stream writer for clipboard data events.
 *
 * This class implements a server-side gRPC stream writer that sends
 * `ClipData` messages to clients as clipboard events occur.  It filters out
 * duplicate events, ensuring that clients only receive updates when the
 * clipboard data actually changes. It also manages per-client state to
 * prevent redundant updates.
 */
class ClipDataEventStreamWriter : public BaseEventStreamWriter<ClipData, ClipboardEvent> {
  public:
    ClipDataEventStreamWriter(CallbackEventSource<ClipboardEvent>* listener, std::string peerId)
            : BaseEventStreamWriter<ClipData, ClipboardEvent>(listener)
            , mPeerId(std::move(peerId)) {}

    /**
     * @brief Handles the arrival of a new clipboard event.
     *
     * This method checks if the new event represents a change in clipboard
     * data compared to the last event sent to the client.  If the data has
     * changed, it sends the new `ClipData` to the client.  Otherwise, the
     * event is ignored to avoid sending redundant updates.
     *
     * @param event The clipboard event that has arrived.
     */
    void eventArrived(const ClipboardEvent& event) override {
        const std::lock_guard<std::mutex> lock(mEventLock);
        if (google::protobuf::util::MessageDifferencer::Equals(event.data, mLastEvent)) {
            VLOG(1) << "ignoring clipboard event for: " << mPeerId
                    << " since it is already aware of: " << event.data.ShortDebugString();
            return;
        }

        mLastEvent = event.data;

        if (mPeerId != event.source) {
            VLOG(1) << "Event from: " << event.source << " for " << mPeerId
                    << ", data: " << event.data.ShortDebugString();
            SimpleServerWriter<ClipData>::Write(event.data);
        } else {
            VLOG(1) << "Dropping update from: " << event.source << " for " << mPeerId;
        }
    }

  private:
    const std::string mPeerId;
    ClipData mLastEvent;
    std::mutex mEventLock;
};
}  // namespace

ClipboardServiceImpl::ClipboardServiceImpl(ClipboardChannel& channel) : mClipboardChannel(channel) {
    mGuestUpdatesCallbackId = mClipboardChannel.guestToHost.addCallback(
            [this](const ClipboardData& clip) { mGuestUpdates.fireEvent(toClipboardEvent(clip)); });
}

ClipboardServiceImpl::~ClipboardServiceImpl() {
    mClipboardChannel.guestToHost.removeCallback(mGuestUpdatesCallbackId);
}

/**
 * @brief Streams clipboard data to the client.
 *
 * This method establishes a stream for sending clipboard updates to the client.
 * It retrieves the initial clipboard contents and then registers a listener
 * to receive subsequent changes.  Each change triggers a new `ClipData`
 * message to be sent through the stream.  The method ensures that the
 * clipboard listener is registered only once. Note that the clipboard listener
 * will forward the events for all future stream requests.
 *
 * @param context The server context for the gRPC call.
 * @param request An empty request message (unused).
 * @return A gRPC server write reactor for streaming `ClipData` updates.
 */
::grpc::ServerWriteReactor<ClipData>* ClipboardServiceImpl::streamClipboard(std::string peerId) {
    auto stream = std::make_unique<ClipDataEventStreamWriter>(&mGuestUpdates, std::move(peerId));
    stream->eventArrived(toClipboardEvent(mClipboardChannel.guestToHost.getValue()));
    return stream.release();
}

Status ClipboardServiceImpl::getClipboard(ClipData* reply) {
    reply->set_text(mClipboardChannel.guestToHost.getValue().contents);
    return Status::OK;
}

Status ClipboardServiceImpl::setClipboard(std::string /*source*/, const ClipData& clipData) {
    mClipboardChannel.hostToGuest.setValue({.contents = clipData.text()});
    return Status::OK;
}

std::string ClipboardServiceImpl::getPeerId(const ::grpc::ServerContextBase& context) {
    std::string flat;
    for (const auto& str : context.auth_context()->GetPeerIdentity()) {
        flat += ":" + std::string(str.data(), str.size());
    }

    flat += ":" + context.peer();
    return flat;
}

}  // namespace control
}  // namespace emulation
}  // namespace android
