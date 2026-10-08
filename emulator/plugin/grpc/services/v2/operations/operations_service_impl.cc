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

#include "operations/operations_service_impl.h"

#include <string>
#include <string_view>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/rpc/status.pb.h"

namespace goldfish::grpc::v2 {

using ::google::longrunning::CancelOperationRequest;
using ::google::longrunning::DeleteOperationRequest;
using ::google::longrunning::GetOperationRequest;
using ::google::longrunning::ListOperationsRequest;
using ::google::longrunning::ListOperationsResponse;
using ::google::longrunning::Operation;
using ::google::longrunning::WaitOperationRequest;
using ::google::protobuf::Empty;
using ::google::protobuf::Message;
using ::grpc::ServerContext;
using ::grpc::Status;
using ::grpc::StatusCode;

OperationsServiceImpl::OperationsServiceImpl(size_t max_completed_operations)
        : max_completed_operations_(max_completed_operations) {}

void OperationsServiceImpl::RecordCompletedOperation(std::string_view name) {
    completed_operations_.emplace_back(name);
    while (completed_operations_.size() > max_completed_operations_) {
        const std::string oldest = std::move(completed_operations_.front());
        completed_operations_.pop_front();
        operations_.erase(oldest);
    }
}

Operation OperationsServiceImpl::StartOperation(std::string_view prefix,
                                                const Message& initial_metadata) {
    absl::MutexLock operations_lock(&operations_mutex_);
    const uint64_t id = next_operation_id_++;
    const std::string name = absl::StrCat("operations/", prefix, "/", id);

    Operation op;
    op.set_name(name);
    op.set_done(false);
    (void)op.mutable_metadata()->PackFrom(initial_metadata);

    operations_[name] = op;
    return op;
}

void OperationsServiceImpl::UpdateOperationMetadata(std::string_view name,
                                                    const Message& metadata) {
    absl::MutexLock operations_lock(&operations_mutex_);
    auto it = operations_.find(name);
    if (it == operations_.end() || it->second.done()) {
        return;
    }
    (void)it->second.mutable_metadata()->PackFrom(metadata);
}

Operation OperationsServiceImpl::CompleteOperation(std::string_view name, const Message& response,
                                                   const Message* final_metadata) {
    absl::MutexLock operations_lock(&operations_mutex_);
    auto it = operations_.find(name);
    if (it == operations_.end() || it->second.done()) {
        if (it != operations_.end()) {
            return it->second;
        }
        Operation op;
        op.set_name(std::string(name));
        op.set_done(true);
        return op;
    }
    Operation& op = it->second;
    op.set_name(std::string(name));
    if (final_metadata != nullptr) {
        (void)op.mutable_metadata()->PackFrom(*final_metadata);
    }
    (void)op.mutable_response()->PackFrom(response);
    op.set_done(true);
    Operation completed_copy = op;
    RecordCompletedOperation(name);
    return completed_copy;
}

Operation OperationsServiceImpl::FailOperation(std::string_view name, const absl::Status& status,
                                               const Message* final_metadata) {
    absl::MutexLock operations_lock(&operations_mutex_);
    auto it = operations_.find(name);
    if (it == operations_.end() || it->second.done()) {
        if (it != operations_.end()) {
            return it->second;
        }
        Operation op;
        op.set_name(std::string(name));
        op.set_done(true);
        return op;
    }
    Operation& op = it->second;
    op.set_name(std::string(name));
    if (final_metadata != nullptr) {
        (void)op.mutable_metadata()->PackFrom(*final_metadata);
    }
    auto* err = op.mutable_error();
    err->set_code(static_cast<int>(status.code()));
    err->set_message(std::string(status.message()));
    op.set_done(true);
    Operation failed_copy = op;
    RecordCompletedOperation(name);
    return failed_copy;
}

Status OperationsServiceImpl::ListOperations(ServerContext* /*context*/,
                                             const ListOperationsRequest* request,
                                             ListOperationsResponse* response) {
    absl::MutexLock operations_lock(&operations_mutex_);
    const std::string_view filter_prefix = request->name();
    for (const auto& [name, op] : operations_) {
        if (filter_prefix.empty() || absl::StartsWith(name, filter_prefix)) {
            *response->add_operations() = op;
        }
    }
    return Status::OK;
}

Status OperationsServiceImpl::GetOperation(ServerContext* /*context*/,
                                           const GetOperationRequest* request,
                                           Operation* response) {
    if (request->name().empty()) {
        return {StatusCode::INVALID_ARGUMENT, "Operation name cannot be empty."};
    }

    absl::MutexLock operations_lock(&operations_mutex_);
    auto it = operations_.find(request->name());
    if (it == operations_.end()) {
        return {StatusCode::NOT_FOUND, absl::StrCat("Operation not found: ", request->name())};
    }
    *response = it->second;
    return Status::OK;
}

Status OperationsServiceImpl::DeleteOperation(ServerContext* /*context*/,
                                              const DeleteOperationRequest* request,
                                              Empty* /*response*/) {
    if (request->name().empty()) {
        return {StatusCode::INVALID_ARGUMENT, "Operation name cannot be empty."};
    }

    absl::MutexLock operations_lock(&operations_mutex_);
    auto it = operations_.find(request->name());
    if (it == operations_.end()) {
        return {StatusCode::NOT_FOUND, absl::StrCat("Operation not found: ", request->name())};
    }
    operations_.erase(it);
    return Status::OK;
}

Status OperationsServiceImpl::CancelOperation(ServerContext* /*context*/,
                                              const CancelOperationRequest* request,
                                              Empty* /*response*/) {
    if (request->name().empty()) {
        return {StatusCode::INVALID_ARGUMENT, "Operation name cannot be empty."};
    }

    absl::MutexLock operations_lock(&operations_mutex_);
    auto it = operations_.find(request->name());
    if (it == operations_.end()) {
        return {StatusCode::NOT_FOUND, absl::StrCat("Operation not found: ", request->name())};
    }
    if (!it->second.done()) {
        auto* err = it->second.mutable_error();
        err->set_code(static_cast<int>(absl::StatusCode::kCancelled));
        err->set_message("Operation cancelled by client.");
        it->second.set_done(true);
        RecordCompletedOperation(request->name());
    }
    return Status::OK;
}

Status OperationsServiceImpl::WaitOperation(ServerContext* /*context*/,
                                            const WaitOperationRequest* request,
                                            Operation* response) {
    if (request->name().empty()) {
        return {StatusCode::INVALID_ARGUMENT, "Operation name cannot be empty."};
    }

    const absl::Duration timeout = request->has_timeout()
                                           ? absl::Seconds(request->timeout().seconds()) +
                                                     absl::Nanoseconds(request->timeout().nanos())
                                           : absl::Seconds(30);
    const absl::Time deadline = absl::Now() + timeout;

    absl::MutexLock operations_lock(&operations_mutex_);
    if (!operations_.contains(request->name())) {
        return {StatusCode::NOT_FOUND, absl::StrCat("Operation not found: ", request->name())};
    }

    const auto is_done_or_deleted = [this, &name = request->name()]()
                                            ABSL_EXCLUSIVE_LOCKS_REQUIRED(operations_mutex_) {
                                                auto it = operations_.find(name);
                                                return it == operations_.end() || it->second.done();
                                            };

    operations_mutex_.AwaitWithDeadline(absl::Condition(&is_done_or_deleted), deadline);

    auto it = operations_.find(request->name());
    if (it == operations_.end()) {
        return {StatusCode::NOT_FOUND, absl::StrCat("Operation not found: ", request->name())};
    }
    *response = it->second;
    return Status::OK;
}

}  // namespace goldfish::grpc::v2
