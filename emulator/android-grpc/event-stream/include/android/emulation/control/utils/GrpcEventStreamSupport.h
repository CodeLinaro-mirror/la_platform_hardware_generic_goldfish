// Copyright (C) 2020 The Android Open Source Project
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

#include <mutex>
#include <unordered_set>

#include "google/protobuf/util/message_differencer.h"

#include "android/emulation/control/utils/EventSupport.h"
#include "android/grpc/utils/SimpleAsyncGrpc.h"

#define DEBUG_EVT 0

#if DEBUG_EVT >= 1
#define DD_EVT(fmt, ...) printf("EventSupport: %s:%d| " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
#define DD_EVT(...) (void)0
#endif

namespace android {
namespace emulation {
namespace control {

/**
 * BaseEventStreamWriter is a class for writing events of type T to a gRPC
 * stream while also acting as an event listener for event notifications. It
 * uses EventWriterPolicy to define the behavior of writing events.
 *
 * @tparam T The type of events to be written to the gRPC stream.
 */
template <class T, class Event>
class BaseEventStreamWriter : public SimpleServerWriter<T>, public GenericEventHandler<Event> {
  public:
    using ChangeSupport = EventChangeSupport<Event>;

    /**
     * Constructs a new GenericEventWriter with the specified listener.
     * The listener is used to subscribe to and receive events of type T.
     *
     * Events that are received will be forwarded and written to the gRPC
     * stream.
     *
     * @param listener A pointer to the ChangeSupport instance that will handle
     *        event subscriptions and event notifications.
     */
    BaseEventStreamWriter(ChangeSupport* listener) : GenericEventHandler<Event>(listener) {}

    virtual ~BaseEventStreamWriter() = default;

    /**
     * Overrides the GenericEventHandler<T, EventWriterPolicy>::OnDone() method
     * to delete the GenericEventWriter instance when the client is done reading
     * the event stream.
     */
    void OnDone() override { delete this; }

    /**
     * Overrides the GenericEventHandler<T, EventWriterPolicy>::OnCancel()
     * method to inform the parent we want to Cancel this connection. This
     * should result in a callback to OnDone, which will do the final cleanup.
     */
    void OnCancel() override {
        DD_EVT("Cancelled %p", this);
        GenericEventHandler<Event>::unsubscribe();
        grpc::ServerWriteReactor<T>::Finish(grpc::Status::CANCELLED);
    }
};

// template<class T>
// using GenericEventStreamWriter = BaseEventStreamWriter<T, T>

template <class T>
class GenericEventStreamWriter : public BaseEventStreamWriter<T, T> {
    using ChangeSupport = EventChangeSupport<T>;

  public:
    GenericEventStreamWriter(ChangeSupport* listener) : BaseEventStreamWriter<T, T>(listener) {}

    virtual ~GenericEventStreamWriter() = default;

    /**
     * Overrides the EventListener<T>::eventArrived() method to handle an
     * event of type T using the EventPolicy class.
     *
     * @param event The event of type T that has arrived and needs to be
     * handled.
     */
    void eventArrived(const T event) override {
        DD_EVT("Handling %p, %s", this, event.ShortDebugString().c_str());
        SimpleServerWriter<T>::Write(event);
    };
};

/**
 * A template class that provides an implementation of a gRPC server event
 * stream writer for events of type T. It inherits from
 * GenericEventStreamWriter<T> and is designed to be used as an event stream
 * writer in a gRPC server. You would mainly use this one if your underlying
 * event mechanism produces spurious events. I.e. your event stream looks
 * something like this:
 *
 * A, A, B, B, B, C, C, ...
 *
 * But you would like to deliver only A, B, C to your clients.
 *
 * @tparam T The type of events that this event stream writer can write to the
 * client.
 */
template <class T>
class UniqueEventStreamWriter : public GenericEventStreamWriter<T> {
    using ChangeSupport = EventChangeSupport<T>;

  public:
    UniqueEventStreamWriter(ChangeSupport* listener) : GenericEventStreamWriter<T>(listener) {}
    virtual ~UniqueEventStreamWriter() = default;

    /**
     * An EventStreamWriter that will filter out duplicate events.
     *
     * @param event The event of type T that has arrived and needs to be written
     *        to the client.
     */
    void eventArrived(const T event) override {
        const std::lock_guard<std::mutex> lock(mEventLock);
        if (!google::protobuf::util::MessageDifferencer::Equals(event, mLastEvent)) {
            mLastEvent = event;
            GenericEventStreamWriter<T>::Write(event);
        }
    };

    T mLastEvent;
    std::mutex mEventLock;
};

}  // namespace control
}  // namespace emulation
}  // namespace android