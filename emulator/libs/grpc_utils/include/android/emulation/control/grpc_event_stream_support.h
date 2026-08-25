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

#include "absl/synchronization/mutex.h"
#include "google/protobuf/util/message_differencer.h"

#include "android/emulation/control/simple_async_grpc.h"
#include "goldfish/eventing/event_sources.h"

#define DEBUG_EVT 0

#if DEBUG_EVT >= 1
#define DD_EVT(fmt, ...) printf("EventSupport: %s:%d| " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
#define DD_EVT(...) (void)0
#endif

namespace android {
namespace emulation {
namespace control {

using android::base::eventing::CallbackEventSource;
using android::base::eventing::EventListener;
using android::base::eventing::EventParam;

/**
 * @class BaseEventStreamWriter
 * @brief A base server writer reactor that subscribes to an event source and streams incoming
 * events to a gRPC client.
 *
 * @details This class bridges internal emulator event sources (`CallbackEventSource`) with gRPC
 * asynchronous writer streams (`SimpleServerWriter`).
 *
 * ### Subclassing Contract & Thread-Safety Invariant:
 * Derived subclasses **must explicitly call `Subscribe()`** at the end of their constructor (once
 * all subclass fields are initialized and the dynamic type is complete) to begin receiving events.
 *
 * Automatic subscription in `BaseEventStreamWriter`'s constructor is intentionally avoided:
 * registering callbacks while base construction is in flight leaks `this` to concurrent event
 * dispatch threads, resulting in `vptr` data races (ctor/dtor vs virtual call) and concurrent
 * access to uninitialized subclass members.
 *
 * ### Example Usage:
 * @code
 * class MyStreamWriter : public BaseEventStreamWriter<MyProtoReply, MyInternalEvent> {
 *   public:
 *     MyStreamWriter(ChangeSupport* source, CustomConfig config)
 *         : BaseEventStreamWriter(source), config_(std::move(config)) {
 *         // Subclass is fully constructed; safe to activate subscription:
 *         Subscribe();
 *     }
 *
 *     void EventArrived(const MyInternalEvent& event) override {
 *         MyProtoReply reply = FormatReply(event, config_);
 *         Write(reply);
 *     }
 *
 *   private:
 *     CustomConfig config_;
 * };
 * @endcode
 *
 * @tparam T The type of gRPC messages to be written to the stream.
 * @tparam Event The underlying event type produced by the event source.
 */
template <class T, class Event>
class BaseEventStreamWriter : public SimpleServerWriter<T>, EventListener<Event> {
  public:
    using ChangeSupport = CallbackEventSource<Event>;

    /**
     * @brief Constructs a `BaseEventStreamWriter` for the specified event source.
     *
     * @param listener Pointer to the `CallbackEventSource` instance managing event subscriptions.
     */
    explicit BaseEventStreamWriter(ChangeSupport* listener) : listener_(listener) {}

    virtual ~BaseEventStreamWriter() { Unsubscribe(); }

    /**
     * @brief Overrides `SimpleServerWriter::OnDone()` to delete the writer instance when the client
     * finishes reading the stream.
     */
    void OnDone() override {
        Unsubscribe();
        delete this;
    }

    /**
     * @brief Overrides `SimpleServerWriter::OnCancel()` to handle client stream cancellations.
     *
     * @details Unregisters the event callback and synchronizes stream termination with the unified
     * `reactor_lock_`.
     */
    void OnCancel() override {
        DD_EVT("Cancelled %p", this);
        Unsubscribe();
        absl::MutexLock lock(&this->reactor_lock_);
        grpc::ServerWriteReactor<T>::Finish(grpc::Status::CANCELLED);
    }

  protected:
    /**
     * @brief Subscribes to the event source.
     *
     * @note **Subclass Contract**: This method MUST be invoked at the end of the derived class
     * constructor once all derived fields are initialized and the vtable is finalized.
     * It is thread-safe and idempotent.
     */
    void Subscribe() {
        absl::MutexLock lock(&this->reactor_lock_);
        if (callback_id_ == ChangeSupport::kInvalidCallbackId && listener_) {
            callback_id_ = listener_->AddCallback(
                    [this](const Event event) { this->EventArrived(event); });
        }
    }

    /**
     * @brief Unsubscribes from the event source.
     *
     * @note Thread-safe and idempotent. Automatically invoked on cancellation, completion, and
     * destruction.
     */
    void Unsubscribe() {
        absl::MutexLock lock(&this->reactor_lock_);
        if (callback_id_ != ChangeSupport::kInvalidCallbackId && listener_) {
            listener_->RemoveCallback(callback_id_);
            callback_id_ = ChangeSupport::kInvalidCallbackId;
        }
    }

  private:
    typename ChangeSupport::CallbackId callback_id_{ChangeSupport::kInvalidCallbackId};
    ChangeSupport* listener_;
};

/**
 * @class GenericEventStreamWriter
 * @brief A generic gRPC server event stream writer where the gRPC message type and event type are
 * identical.
 *
 * @tparam T The type of events and gRPC messages to be written to the stream.
 */
template <class T>
class GenericEventStreamWriter : public BaseEventStreamWriter<T, T> {
    using ChangeSupport = CallbackEventSource<T>;

  public:
    /**
     * @brief Constructs a `GenericEventStreamWriter` subscribed to the specified event source.
     *
     * @param listener Pointer to the `CallbackEventSource` instance.
     */
    explicit GenericEventStreamWriter(ChangeSupport* listener)
            : BaseEventStreamWriter<T, T>(listener) {
        this->Subscribe();
    }

    virtual ~GenericEventStreamWriter() = default;

    /**
     * @brief Invoked when an event arrives from the underlying event source. Enqueues the event for
     * gRPC writing.
     *
     * @param event The event of type `T` that has arrived.
     */
    void EventArrived(typename EventParam<T>::type event) override {
        DD_EVT("Handling %p, %s", this, event.ShortDebugString().c_str());
        SimpleServerWriter<T>::Write(event);
    };
};

/**
 * @class UniqueEventStreamWriter
 * @brief A gRPC server event stream writer that filters out duplicate consecutive events.
 *
 * @details This class inherits from `GenericEventStreamWriter` and is designed for event sources
 * that produce spurious duplicate notifications. For example, if the raw event source emits `A, A,
 * B, B, B, C, C`, this writer ensures only `A, B, C` are delivered to the gRPC client.
 *
 * @tparam T The type of events and gRPC messages to be written to the stream.
 */
template <class T>
class UniqueEventStreamWriter : public GenericEventStreamWriter<T> {
    using ChangeSupport = CallbackEventSource<T>;

  public:
    /**
     * @brief Constructs a `UniqueEventStreamWriter` subscribed to the specified event source.
     *
     * @param listener Pointer to the `CallbackEventSource` instance.
     */
    UniqueEventStreamWriter(ChangeSupport* listener) : GenericEventStreamWriter<T>(listener) {}
    virtual ~UniqueEventStreamWriter() = default;

    /**
     * @brief Invoked when an event arrives. Compares the new event against the last forwarded event
     * using `MessageDifferencer`.
     *
     * @param event The event of type `T` that has arrived.
     */
    void EventArrived(typename EventParam<T>::type event) override {
        const std::lock_guard<std::mutex> lock(event_lock_);
        if (!google::protobuf::util::MessageDifferencer::Equals(event, last_event_)) {
            last_event_ = event;
            GenericEventStreamWriter<T>::Write(event);
        }
    };

    T last_event_;
    std::mutex event_lock_;
};

}  // namespace control
}  // namespace emulation
}  // namespace android
