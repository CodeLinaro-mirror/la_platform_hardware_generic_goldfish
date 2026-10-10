// Copyright (C) 2026 The Android Open Source Project
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

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "google/longrunning/operations.grpc.pb.h"
#include "google/protobuf/message.h"
#include "grpcpp/grpcpp.h"

namespace goldfish::grpc::v2 {

/**
 * Thread-safe coordinator and gRPC service implementing google.longrunning.Operations.
 *
 * Manages lifecycle state, typed progress metadata, completion payloads, and predicate-based
 * waiting for long-running operations initiated by v2 microservices (such as SnapshotService).
 *
 * Invariants:
 *   - Every stored operation in `operations_` has a non-empty unique `name()`.
 *   - In-flight (`done() == false`) operations are never evicted automatically.
 *   - Once an operation transitions to `done() == true`, its terminal result (`response` or
 *     `error`) is immutable until removed via `DeleteOperation` or FIFO eviction when completed
 *     operations exceed `max_completed_operations_`.
 *
 * Thread Safety:
 *   - All public methods are thread-safe and synchronized via `operations_mutex_`.
 */
class OperationsServiceImpl final : public ::google::longrunning::Operations::Service {
  public:
    static constexpr size_t kDefaultMaxCompletedOperations = 256;

    explicit OperationsServiceImpl(
            size_t max_completed_operations = kDefaultMaxCompletedOperations);
    ~OperationsServiceImpl() override = default;

    OperationsServiceImpl(const OperationsServiceImpl&) = delete;
    OperationsServiceImpl& operator=(const OperationsServiceImpl&) = delete;

    /**
     * Registers a new in-flight operation with initial metadata.
     *
     * @param prefix Logical subsystem prefix used to construct `operations/<prefix>/<id>`.
     * @param initial_metadata Protobuf message packed into `Operation.metadata`.
     * @return Initial snapshot of the newly registered `google.longrunning.Operation`.
     */
    ::google::longrunning::Operation StartOperation(
            std::string_view prefix, const ::google::protobuf::Message& initial_metadata)
            ABSL_LOCKS_EXCLUDED(operations_mutex_);

    /**
     * Updates the packed metadata of an existing in-flight operation.
     *
     * @param name Unique operation resource name.
     * @param metadata Updated metadata protobuf message.
     */
    void UpdateOperationMetadata(std::string_view name, const ::google::protobuf::Message& metadata)
            ABSL_LOCKS_EXCLUDED(operations_mutex_);

    /**
     * Marks an operation as completed with a successful response payload and wakes any callers
     * parked in `WaitOperation`.
     *
     * @param name Unique operation resource name.
     * @param response Result protobuf message packed into `Operation.response`.
     * @param final_metadata Optional final metadata protobuf message to pack into
     * `Operation.metadata`.
     * @return Completed snapshot of the `google.longrunning.Operation`.
     */
    ::google::longrunning::Operation CompleteOperation(
            std::string_view name, const ::google::protobuf::Message& response,
            const ::google::protobuf::Message* final_metadata = nullptr)
            ABSL_LOCKS_EXCLUDED(operations_mutex_);

    /**
     * Marks an operation as failed with an error status and wakes any callers parked in
     * `WaitOperation`.
     *
     * @param name Unique operation resource name.
     * @param status Error status translated into `Operation.error`.
     * @param final_metadata Optional final metadata protobuf message to pack into
     * `Operation.metadata`.
     * @return Failed snapshot of the `google.longrunning.Operation`.
     */
    ::google::longrunning::Operation FailOperation(
            std::string_view name, const absl::Status& status,
            const ::google::protobuf::Message* final_metadata = nullptr)
            ABSL_LOCKS_EXCLUDED(operations_mutex_);

    /**
     * Blocks until the operation is done, deleted, `deadline` passes, or `is_cancelled` returns
     * true. `is_cancelled` is polled every `kCancellationPollInterval`, since an external
     * cancellation (e.g. a disconnected gRPC client) does not signal `operations_mutex_`.
     *
     * @return The operation's latest state (possibly still in-flight on deadline), NotFound if
     * it does not exist or was deleted while waiting, or Cancelled if `is_cancelled` fired.
     */
    absl::StatusOr<::google::longrunning::Operation> WaitForOperation(
            std::string_view name, absl::Time deadline, absl::FunctionRef<bool()> is_cancelled)
            ABSL_LOCKS_EXCLUDED(operations_mutex_);

    static constexpr absl::Duration kCancellationPollInterval = absl::Milliseconds(100);

    ::grpc::Status ListOperations(::grpc::ServerContext* context,
                                  const ::google::longrunning::ListOperationsRequest* request,
                                  ::google::longrunning::ListOperationsResponse* response) override
            ABSL_LOCKS_EXCLUDED(operations_mutex_);

    ::grpc::Status GetOperation(::grpc::ServerContext* context,
                                const ::google::longrunning::GetOperationRequest* request,
                                ::google::longrunning::Operation* response) override
            ABSL_LOCKS_EXCLUDED(operations_mutex_);

    ::grpc::Status DeleteOperation(::grpc::ServerContext* context,
                                   const ::google::longrunning::DeleteOperationRequest* request,
                                   ::google::protobuf::Empty* response) override
            ABSL_LOCKS_EXCLUDED(operations_mutex_);

    ::grpc::Status CancelOperation(::grpc::ServerContext* context,
                                   const ::google::longrunning::CancelOperationRequest* request,
                                   ::google::protobuf::Empty* response) override
            ABSL_LOCKS_EXCLUDED(operations_mutex_);

    ::grpc::Status WaitOperation(::grpc::ServerContext* context,
                                 const ::google::longrunning::WaitOperationRequest* request,
                                 ::google::longrunning::Operation* response) override
            ABSL_LOCKS_EXCLUDED(operations_mutex_);

  private:
    void RecordCompletedOperation(std::string_view name)
            ABSL_EXCLUSIVE_LOCKS_REQUIRED(operations_mutex_);

    const size_t max_completed_operations_;
    mutable absl::Mutex operations_mutex_;
    uint64_t next_operation_id_ ABSL_GUARDED_BY(operations_mutex_) = 1;
    absl::flat_hash_map<std::string, ::google::longrunning::Operation> operations_
            ABSL_GUARDED_BY(operations_mutex_);
    std::deque<std::string> completed_operations_ ABSL_GUARDED_BY(operations_mutex_);
};

}  // namespace goldfish::grpc::v2
