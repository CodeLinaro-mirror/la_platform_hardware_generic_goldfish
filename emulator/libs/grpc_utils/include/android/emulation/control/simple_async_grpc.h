// Copyright (C) 2022 The Android Open Source Project
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
#include <grpcpp/support/client_callback.h>

#include <functional>
#include <mutex>
#include <queue>
#include <type_traits>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

/**
 * @class WithReactorLock
 * @brief A common synchronization base class virtually inherited by both reader and writer mixins.
 *
 * @details `reactor_lock_` is protected to ensure strict encapsulation and prevent external lock
 * tampering. It provides a unified synchronization primitive for all critical reactor transitions
 * (`StartRead`, `StartWrite`, and `Finish`).
 */
class WithReactorLock {
  protected:
    absl::Mutex reactor_lock_;
};

/**
 * @struct Select
 * @brief Selects a type from a pack of types based on the provided index.
 *
 * @tparam Index The index of the type to select.
 * @tparam Args The parameter pack of types.
 */
template <std::size_t Index, typename... Args>
struct Select {
    /**
     * @brief Calculates the index of the last available type in the pack.
     */
    static constexpr std::size_t LastIndex = sizeof...(Args) - 1;

    /**
     * @brief Selects the type at the specified index from the pack of types.
     * If the index is out of range, the last available type is selected.
     */
    using type = typename std::tuple_element<(Index < sizeof...(Args) ? Index : LastIndex),
                                             std::tuple<Args...>>::type;
};

/**
 * @struct Select<Index, Template<Args...>>
 * @brief Specialization for template types with a pack of template arguments.
 */
template <std::size_t Index, template <typename...> class Template, typename... Args>
struct Select<Index, Template<Args...>> {
    using type = typename Select<Index, Args...>::type;
};

/**
 * @brief Convenient type alias for the selected type.
 */
template <std::size_t Index, typename T>
using Select_t = typename Select<Index, T>::type;

/**
 * @class WithSimpleReader
 * @brief A simple reader reactor mixin for reading a stream of data asynchronously.
 *
 * @details For a server reader, the channel will be closed with `grpc::Status::OK`
 * if a message cannot be read (i.e., `OnReadDone` is not ok).
 *
 * `T` can be a Server or Client Reactor. Note that for a client reactor, you must
 * explicitly call `StartRead` once you create the reader object.
 *
 * @tparam T The base gRPC reactor class (e.g., `grpc::ServerReadReactor`,
 * `grpc::ClientBidiReactor`).
 */
template <typename T>
class WithSimpleReader : public T, public virtual WithReactorLock {
  public:
    using is_server = std::is_base_of<grpc::internal::ServerReactor, T>;

    // We select index 0, or index 1 in case of Bi-directional reactors.
    // I.e. T<X,Y,...> --> Y
    // T<X> --> X
    using R = Select_t<1, T>;

    WithSimpleReader() {
        if constexpr (is_server::value) {
            // Clients should not start immediately
            StartRead();
        }
    }

    void OnReadDone(bool ok) override {
        if (ok) {
            if (Read(&incoming_)) {
                absl::MutexLock lock(&this->reactor_lock_);
                T::StartRead(&incoming_);
            }
        } else {
            if constexpr (is_server::value) {
                // Call finish if we are a server
                absl::MutexLock lock(&this->reactor_lock_);
                T::Finish(grpc::Status::OK);
            }
        }
    }

    void StartRead() {
        absl::MutexLock lock(&this->reactor_lock_);
        T::StartRead(&incoming_);
    }

    /**
     * @brief Callback invoked when a new object is successfully read from the stream.
     *
     * @param read Pointer to the newly read object of type `R`.
     * @return `true` to continue reading the stream, `false` to stop.
     */
    virtual bool Read(const R* read) = 0;

  private:
    R incoming_;
};

/**
 * @class SimpleServerLambdaReader
 * @brief A simple server reader reactor utilizing lambda callbacks for read and completion events.
 *
 * @details This class automatically deletes itself upon completion (`OnDone`).
 * The channel is closed with `grpc::Status::OK` when the stream finishes normally.
 *
 * @tparam R The type of incoming request messages.
 * @tparam Base The underlying gRPC server read reactor base class.
 */
template <typename R, typename Base = grpc::ServerReadReactor<R>>
class SimpleServerLambdaReader : public WithSimpleReader<Base> {
    // A return other than OkStatus will Finish the stream with that status.
    using ReadCallback = std::function<grpc::Status(const R*)>;
    using OnDoneCallback = std::function<void()>;

  public:
    /**
     * @brief Constructs a `SimpleServerLambdaReader` with specified read and done callbacks.
     *
     * @param readFn Callback invoked for each incoming message. Returning a non-OK status finishes
     * the stream with that status.
     * @param doneFn Callback invoked when the stream completes before the reactor self-deletes.
     */
    SimpleServerLambdaReader(
            ReadCallback readFn, OnDoneCallback doneFn = []() {})
            : read_fn_(readFn), done_fn_(doneFn) {}

    virtual bool Read(const R* read) override {
        auto status = read_fn_(read);
        if (!status.ok()) {
            absl::MutexLock lock(&this->reactor_lock_);
            Base::Finish(status);
            return false;
        }
        return true;
    }

    virtual void OnDone() override {
        done_fn_();
        delete this;
    }

  private:
    ReadCallback read_fn_;
    OnDoneCallback done_fn_;
};

/**
 * @class SimpleClientLambdaReader
 * @brief A simple client reader reactor utilizing lambda callbacks for asynchronous stream reading.
 *
 * @details This class automatically deletes itself upon completion (`OnDone`).
 *
 * @par Example Usage:
 * @code
 * grpc::ClientContext* context = client_->NewContext().release();
 * static google::protobuf::Empty empty;
 * auto read = new SimpleClientLambdaReader<PhoneEvent>(
 *         context,
 *         [](auto event) {
 *            std::cout << "Received event: " << event.ShortDebugString();
 *            return grpc::Status::OK;
 *         },
 *         [context](auto status) {
 *             std::cout << "Finished: " << status.error_message();
 *             delete context;
 *         });
 * service_->async()->receivePhoneEvents(context, &empty, read);
 * read->StartRead();
 * read->StartCall();
 * @endcode
 *
 * @tparam R The type of incoming response messages.
 * @tparam Base The underlying gRPC client read reactor base class.
 */
template <typename R, typename Base = grpc::ClientReadReactor<R>>
class SimpleClientLambdaReader : public WithSimpleReader<Base> {
    using ReadCallback = std::function<grpc::Status(const R*)>;
    using OnDoneCallback = std::function<void(::grpc::Status)>;

  public:
    /**
     * @brief Constructs a `SimpleClientLambdaReader` with a shared context and callbacks.
     *
     * @param context Shared pointer to the gRPC client context.
     * @param readFn Callback invoked for each incoming message.
     * @param doneFn Callback invoked upon stream completion with the final gRPC status.
     */
    SimpleClientLambdaReader(
            std::shared_ptr<grpc::ClientContext> context, ReadCallback readFn,
            OnDoneCallback doneFn = [](auto s) {})
            : read_fn_(readFn), context_(std::move(context)), done_fn_(doneFn) {}

    virtual bool Read(const R* read) override {
        auto status = read_fn_(read);
        if (!status.ok()) {
            absl::MutexLock lock(&this->reactor_lock_);
            Base::Finish(status);
            return false;
        }
        return true;
    }

    virtual void OnDone(const grpc::Status& status) override {
        done_fn_(status);
        delete this;
    }

    virtual void TryCancel() { context_->TryCancel(); }

  private:
    ReadCallback read_fn_;
    OnDoneCallback done_fn_;
    std::shared_ptr<grpc::ClientContext> context_;
};

/**
 * @class WithSimpleQueueWriter
 * @brief A simple asynchronous writer mixin where outgoing objects are queued and written
 * sequentially.
 *
 * @details Important considerations when using this mixin:
 * - You will not be explicitly notified when an individual object has completed writing to the
 * wire.
 * - The internal queue will grow unbounded if the enqueue rate exceeds the underlying gRPC network
 * transmission rate.
 *
 * @tparam T The base gRPC reactor class (e.g., `grpc::ServerWriteReactor`,
 * `grpc::ClientBidiReactor`).
 */
template <typename T>
class WithSimpleQueueWriter : public T, public virtual WithReactorLock {
  public:
    // We always select index 0 of the T<X,...>
    using W = Select_t<0, T>;

    void OnWriteDone(bool ok) override {
        {
            absl::MutexLock lock(&this->reactor_lock_);
            write_queue_.pop();
            writing_ = false;
        }
        NextWrite();
    }

    /**
     * @brief Enqueues the specified object for asynchronous writing on the gRPC thread pool.
     *
     * @param msg The message object of type `W` to write.
     */
    void Write(const W& msg) {
        {
            absl::MutexLock lock(&this->reactor_lock_);
            write_queue_.push(msg);
        }
        NextWrite();
    }

  private:
    void NextWrite() {
        absl::MutexLock lock(&this->reactor_lock_);
        if (!write_queue_.empty() && !writing_) {
            writing_ = true;
            T::StartWrite(&write_queue_.front());
        }
    }

    std::queue<W> write_queue_;
    bool writing_{false};
};

/**
 * @class SimpleClientWriter
 * @brief A simple client writer reactor managing an asynchronous outgoing stream.
 *
 * @tparam W The type of outgoing request messages.
 */
template <typename W>
class SimpleClientWriter : public WithSimpleQueueWriter<grpc::ClientWriteReactor<W>> {
  public:
    /**
     * @brief Constructs a `SimpleClientWriter` with a shared client context.
     *
     * @param context Shared pointer to the gRPC client context.
     */
    SimpleClientWriter(std::shared_ptr<::grpc::ClientContext> context)
            : context_(std::move(context)) {}

    /**
     * @brief Retrieves the raw pointer to the underlying gRPC client context.
     *
     * @return Pointer to `grpc::ClientContext`.
     */
    ::grpc::ClientContext* context() { return context_.get(); }

  private:
    std::shared_ptr<::grpc::ClientContext> context_;
};

/**
 * @brief A bidirectional server stream composed of both simple reader and queue writer mixins.
 *
 * @tparam R The type of incoming request messages.
 * @tparam W The type of outgoing response messages.
 */
template <typename R, typename W>
using SimpleServerBidiStream =
        WithSimpleQueueWriter<WithSimpleReader<grpc::ServerBidiReactor<R, W>>>;

/**
 * @brief A simple server reader reactor alias.
 *
 * @tparam R The type of incoming request messages.
 */
template <typename R>
using SimpleServerReader = WithSimpleReader<grpc::ServerReadReactor<R>>;

/**
 * @brief A simple server writer reactor alias.
 *
 * @tparam W The type of outgoing response messages.
 */
template <typename W>
using SimpleServerWriter = WithSimpleQueueWriter<grpc::ServerWriteReactor<W>>;

/**
 * @brief A bidirectional client stream composed of both simple reader and queue writer mixins.
 *
 * @tparam R The type of incoming response messages.
 * @tparam W The type of outgoing request messages.
 */
template <typename R, typename W>
using SimpleClientBidiStream =
        WithSimpleQueueWriter<WithSimpleReader<grpc::ClientBidiReactor<W, R>>>;
