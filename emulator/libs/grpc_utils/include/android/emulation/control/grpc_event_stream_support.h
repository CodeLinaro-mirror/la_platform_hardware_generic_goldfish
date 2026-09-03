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

#include <functional>
#include <mutex>
#include <unordered_set>

#include "absl/log/check.h"
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
template <class T, class Event, class Source = CallbackEventSource<Event>,
          size_t max_queue_size = 0, size_t recycle_size = 0,
          class Reactor = ::grpc::ServerWriteReactor<T>>
class BaseEventStreamWriter : public WithSimpleQueueWriter<Reactor, max_queue_size, recycle_size>,
                              EventListener<Event> {
  public:
    using ChangeSupport = Source;

    /**
     * @brief Constructs a `BaseEventStreamWriter` for the specified event source.
     *
     * @param listener Pointer to the event source instance managing event subscriptions.
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
        Reactor::Finish(grpc::Status::CANCELLED);
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
template <class T, size_t max_queue_size = 0, size_t recycle_size = 0>
class GenericEventStreamWriter
        : public BaseEventStreamWriter<T, T, CallbackEventSource<T>, max_queue_size, recycle_size> {
    using ChangeSupport = CallbackEventSource<T>;

  public:
    /**
     * @brief Constructs a `GenericEventStreamWriter` subscribed to the specified event source.
     *
     * @param listener Pointer to the `CallbackEventSource` instance.
     */
    explicit GenericEventStreamWriter(ChangeSupport* listener)
            : BaseEventStreamWriter<T, T, CallbackEventSource<T>, max_queue_size, recycle_size>(
                      listener) {
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
        this->Write(event);
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
template <class T, size_t max_queue_size = 0, size_t recycle_size = 0>
class UniqueEventStreamWriter : public GenericEventStreamWriter<T, max_queue_size, recycle_size> {
    using ChangeSupport = CallbackEventSource<T>;

  public:
    /**
     * @brief Constructs a `UniqueEventStreamWriter` subscribed to the specified event source.
     *
     * @param listener Pointer to the `CallbackEventSource` instance.
     */
    UniqueEventStreamWriter(ChangeSupport* listener)
            : GenericEventStreamWriter<T, max_queue_size, recycle_size>(listener) {}
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
            this->Write(event);
        }
    };

    T last_event_;
    std::mutex event_lock_;
};

/**
 * @class StateStreamWriter
 * @brief A gRPC server event stream writer that sends an immediate initial snapshot
 * upon connection and reactively streams state updates as events arrive.
 *
 * @details When recycling is enabled (`recycle_size > 0`), message objects passed to
 * `populate_fn` are acquired from the internal pool via `AcquireMessage()` and can (and will)
 * contain stale data from previous transmissions. It is up to `populate_fn` to properly initialize
 * the recycled object (e.g. overwriting fields or clearing stale state as needed). This behavior is
 * intentional and by design to allow reusing existing `std::string` buffers, repeated fields, and
 * allocated storage across stream updates without repetitive heap allocations.
 *
 * ### Thread Safety:
 * - `populate_fn` is invoked synchronously on the calling thread during `StateStreamWriter`
 *   construction to deliver the initial state snapshot, and subsequently invoked asynchronously
 *   on whichever thread(s) dispatch notifications from the `CallbackEventSource` via
 * `EventArrived`.
 * - `populate_fn` MUST be thread-safe with respect to any external state being read. Callers should
 *   acquire appropriate locks (e.g. state/framebuffer mutexes) inside `populate_fn` if external
 *   state can be modified concurrently.
 * - Internal queue operations and recycling via `AcquireMessage()` and `Write()` are fully
 * thread-safe and protected by `reactor_lock_`.
 *
 * ### Example `populate_fn` Usage:
 * @code
 * auto populate_fn = [&](ImageFrame* frame) {
 *     // Acquire external lock if state can be modified concurrently:
 *     absl::MutexLock lock(&display_mutex_);
 *
 *     // Scalars must be explicitly overwritten since recycled instances contain stale data:
 *     frame->set_img_width(current_width_);
 *     frame->set_img_height(current_height_);
 *     frame->set_format(ImageFormat::RGBA8888);
 *
 *     // Reuses internal std::string capacity without allocating new heap memory:
 *     frame->mutable_image_bytes()->assign(raw_pixels, byte_size);
 * };
 * @endcode
 *
 * @tparam T The gRPC Protobuf message type to write to the stream.
 * @tparam Event The underlying event type produced by the CallbackEventSource.
 * @tparam max_queue_size The maximum number of items to keep in the write queue (0 = unbounded).
 * @tparam recycle_size The maximum number of items to keep in the recycle pool (0 = disabled).
 */
template <class T, class Event, class Source = CallbackEventSource<Event>,
          size_t max_queue_size = 0, size_t recycle_size = 0,
          class Reactor = ::grpc::ServerWriteReactor<T>>
class StateStreamWriter
        : public BaseEventStreamWriter<T, Event, Source, max_queue_size, recycle_size, Reactor> {
  public:
    using ChangeSupport = Source;

    /**
     * @brief Callback function type to populate a Protobuf message `T` with the current state.
     *
     * @details
     * ### Stale Data & Buffer Reuse:
     * When recycling is enabled (`recycle_size > 0`), the `T*` passed to this function can
     * (and will) contain stale data from previous transmissions. It is up to `populate_fn` to
     * properly initialize the recycled object (e.g. overwriting fields or clearing unneeded data).
     * For example, assigning scalar fields like `set_img_width()` / `set_img_height()` and
     * using `mutable_image_bytes()->assign(...)` allows reusing existing `std::string` buffer
     * capacity without incurring repetitive heap allocations.
     *
     * ### Thread Safety:
     * This callback is executed synchronously during `StateStreamWriter` construction (initial
     * snapshot) and asynchronously from event source dispatcher threads during `EventArrived()`.
     * Any shared or mutable external state accessed inside `populate_fn` must be synchronized by
     * the caller.
     */
    using PopulateStateFn = std::function<void(T*)>;
    using FilterPredicate = std::function<bool(const typename EventParam<Event>::type&)>;

    /**
     * @brief Constructs a state stream writer, writes the initial state snapshot,
     * and subscribes to the event source.
     *
     * @param source The event source to subscribe to.
     * @param populate_fn Function to populate the Protobuf message with current state.
     *                    Note: When recycling is enabled (`recycle_size > 0`), the object passed
     *                    to `populate_fn` can (and will) contain stale data and must be properly
     *                    initialized by `populate_fn` (e.g. setting `img_width`, `img_height`, and
     *                    reusing `std::string` buffers). Must be thread-safe as it is invoked
     * across constructor and event dispatch threads.
     * @param filter_fn Optional predicate to filter incoming events.
     */
    StateStreamWriter(ChangeSupport* source, PopulateStateFn populate_fn,
                      FilterPredicate filter_fn = nullptr)
            : BaseEventStreamWriter<T, Event, Source, max_queue_size, recycle_size, Reactor>(source)
            , populate_fn_(std::move(populate_fn))
            , filter_fn_(std::move(filter_fn)) {
        WriteState();
        this->Subscribe();
    }

    virtual ~StateStreamWriter() { this->Unsubscribe(); }

    void EventArrived(typename EventParam<Event>::type event) override {
        if (!filter_fn_ || filter_fn_(event)) {
            WriteState();
        }
    }

  private:
    void WriteState() {
        DCHECK(populate_fn_);
        T current_state = this->AcquireMessage();
        populate_fn_(&current_state);
        this->Write(std::move(current_state));
    }

    PopulateStateFn populate_fn_;
    FilterPredicate filter_fn_;
};

}  // namespace control
}  // namespace emulation
}  // namespace android
