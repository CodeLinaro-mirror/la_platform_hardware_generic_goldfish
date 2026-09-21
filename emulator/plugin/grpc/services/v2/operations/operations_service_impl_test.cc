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

#include <gtest/gtest.h>

#include <thread>

#include "absl/status/status.h"
#include "absl/synchronization/notification.h"
#include "google/longrunning/operations.pb.h"

#include "hypervisor_service.pb.h"

namespace goldfish::grpc::v2 {
namespace {

using ::android::emulation::v2::Snapshot;
using ::android::emulation::v2::SnapshotOperationMetadata;
using ::google::longrunning::CancelOperationRequest;
using ::google::longrunning::DeleteOperationRequest;
using ::google::longrunning::GetOperationRequest;
using ::google::longrunning::ListOperationsRequest;
using ::google::longrunning::ListOperationsResponse;
using ::google::longrunning::Operation;
using ::google::longrunning::WaitOperationRequest;
using ::google::protobuf::Empty;
using ::grpc::ServerContext;
using ::grpc::Status;
using ::grpc::StatusCode;

TEST(OperationsServiceImplTest, LifecycleAndMetadataUnpacking) {
    OperationsServiceImpl service;

    SnapshotOperationMetadata initial_meta;
    initial_meta.set_snapshot_id("snap_1");
    initial_meta.set_phase(SnapshotOperationMetadata::PREPARING);
    initial_meta.set_progress_percent(10);

    const Operation started = service.StartOperation("snapshot/create", initial_meta);
    EXPECT_FALSE(started.name().empty());
    EXPECT_FALSE(started.done());

    SnapshotOperationMetadata unpacked_meta;
    ASSERT_TRUE(started.metadata().UnpackTo(&unpacked_meta));
    EXPECT_EQ(unpacked_meta.snapshot_id(), "snap_1");
    EXPECT_EQ(unpacked_meta.progress_percent(), 10);

    Snapshot result_snap;
    result_snap.set_snapshot_id("snap_1");
    result_snap.set_display_name("First Snapshot");

    SnapshotOperationMetadata final_meta;
    final_meta.set_snapshot_id("snap_1");
    final_meta.set_phase(SnapshotOperationMetadata::TRANSFERRING);
    final_meta.set_progress_percent(100);

    const Operation completed = service.CompleteOperation(started.name(), result_snap, &final_meta);
    EXPECT_TRUE(completed.done());

    GetOperationRequest get_req;
    get_req.set_name(started.name());
    Operation queried;
    ServerContext ctx;
    ASSERT_TRUE(service.GetOperation(&ctx, &get_req, &queried).ok());
    EXPECT_TRUE(queried.done());

    Snapshot unpacked_snap;
    ASSERT_TRUE(queried.response().UnpackTo(&unpacked_snap));
    EXPECT_EQ(unpacked_snap.snapshot_id(), "snap_1");
    EXPECT_EQ(unpacked_snap.display_name(), "First Snapshot");
}

TEST(OperationsServiceImplTest, WaitOperationWakesImmediatelyOnCompletion) {
    OperationsServiceImpl service;

    SnapshotOperationMetadata meta;
    meta.set_snapshot_id("async_snap");
    const Operation started = service.StartOperation("snapshot/create", meta);

    absl::Notification worker_ready;
    Operation waited;
    Status wait_status;

    std::thread waiter([&] {
        WaitOperationRequest req;
        req.set_name(started.name());
        req.mutable_timeout()->set_seconds(10);
        ServerContext ctx;
        worker_ready.Notify();
        wait_status = service.WaitOperation(&ctx, &req, &waited);
    });

    worker_ready.WaitForNotification();

    Snapshot result;
    result.set_snapshot_id("async_snap");
    service.CompleteOperation(started.name(), result);

    waiter.join();
    ASSERT_TRUE(wait_status.ok());
    EXPECT_TRUE(waited.done());
    Snapshot unpacked;
    ASSERT_TRUE(waited.response().UnpackTo(&unpacked));
    EXPECT_EQ(unpacked.snapshot_id(), "async_snap");
}

TEST(OperationsServiceImplTest, ListCancelAndDeleteOperations) {
    OperationsServiceImpl service;

    SnapshotOperationMetadata meta;
    meta.set_snapshot_id("snap_a");
    const Operation op_a = service.StartOperation("snapshot/create", meta);
    const Operation op_b = service.StartOperation("snapshot/restore", meta);

    {
        ListOperationsRequest req;
        req.set_name("operations/snapshot/create");
        ListOperationsResponse resp;
        ServerContext ctx;
        ASSERT_TRUE(service.ListOperations(&ctx, &req, &resp).ok());
        ASSERT_EQ(resp.operations_size(), 1);
        EXPECT_EQ(resp.operations(0).name(), op_a.name());
    }

    {
        CancelOperationRequest cancel_req;
        cancel_req.set_name(op_b.name());
        Empty empty;
        ServerContext ctx;
        ASSERT_TRUE(service.CancelOperation(&ctx, &cancel_req, &empty).ok());

        GetOperationRequest get_req;
        get_req.set_name(op_b.name());
        Operation cancelled;
        ServerContext get_ctx;
        ASSERT_TRUE(service.GetOperation(&get_ctx, &get_req, &cancelled).ok());
        EXPECT_TRUE(cancelled.done());
        EXPECT_TRUE(cancelled.has_error());
    }

    {
        DeleteOperationRequest del_req;
        del_req.set_name(op_a.name());
        Empty empty;
        ServerContext ctx;
        ASSERT_TRUE(service.DeleteOperation(&ctx, &del_req, &empty).ok());

        GetOperationRequest get_req;
        get_req.set_name(op_a.name());
        Operation deleted;
        ServerContext get_ctx;
        EXPECT_EQ(service.GetOperation(&get_ctx, &get_req, &deleted).error_code(),
                  StatusCode::NOT_FOUND);
    }
}

TEST(OperationsServiceImplTest, EvictsOldestCompletedOperationsWhileRetainingInFlight) {
    OperationsServiceImpl service(/*max_completed_operations=*/2);

    SnapshotOperationMetadata meta;
    meta.set_snapshot_id("snap");
    Snapshot result;
    result.set_snapshot_id("snap");

    // Start an in-flight operation that never finishes; it must never be evicted.
    const Operation in_flight = service.StartOperation("snapshot/create", meta);

    // Start and complete 3 operations; with capacity=2, done_1 must be evicted while done_2,
    // done_3, and in_flight remain present.
    const Operation done_1 = service.StartOperation("snapshot/create", meta);
    const Operation done_2 = service.StartOperation("snapshot/create", meta);
    const Operation done_3 = service.StartOperation("snapshot/create", meta);

    service.CompleteOperation(done_1.name(), result);
    service.CompleteOperation(done_2.name(), result);
    service.CompleteOperation(done_3.name(), result);

    auto get_status = [&](std::string_view name) {
        GetOperationRequest req;
        req.set_name(std::string(name));
        Operation op;
        ServerContext ctx;
        return service.GetOperation(&ctx, &req, &op).error_code();
    };

    EXPECT_EQ(get_status(done_1.name()), StatusCode::NOT_FOUND);
    EXPECT_EQ(get_status(done_2.name()), StatusCode::OK);
    EXPECT_EQ(get_status(done_3.name()), StatusCode::OK);
    EXPECT_EQ(get_status(in_flight.name()), StatusCode::OK);
}

TEST(OperationsServiceImplTest, DoesNotResurrectOrClobberCancelledOrDeletedOperation) {
    OperationsServiceImpl service;
    SnapshotOperationMetadata meta;
    meta.set_snapshot_id("snap");
    Snapshot result;
    result.set_snapshot_id("snap");

    // 1. Cancelled operation must not be clobbered by CompleteOperation or FailOperation
    const Operation op_cancel = service.StartOperation("snapshot/create", meta);
    CancelOperationRequest cancel_req;
    cancel_req.set_name(op_cancel.name());
    Empty empty;
    ServerContext cancel_ctx;
    ASSERT_TRUE(service.CancelOperation(&cancel_ctx, &cancel_req, &empty).ok());

    const Operation completed_on_cancelled = service.CompleteOperation(op_cancel.name(), result);
    EXPECT_TRUE(completed_on_cancelled.done());
    EXPECT_TRUE(completed_on_cancelled.has_error());
    EXPECT_EQ(completed_on_cancelled.error().code(),
              static_cast<int>(absl::StatusCode::kCancelled));
    EXPECT_FALSE(completed_on_cancelled.has_response());

    const Operation failed_on_cancelled =
            service.FailOperation(op_cancel.name(), absl::InternalError("Late failure"));
    EXPECT_TRUE(failed_on_cancelled.done());
    EXPECT_TRUE(failed_on_cancelled.has_error());
    EXPECT_EQ(failed_on_cancelled.error().code(), static_cast<int>(absl::StatusCode::kCancelled));

    // 2. Deleted operation must not be resurrected by CompleteOperation or FailOperation
    const Operation op_delete = service.StartOperation("snapshot/create", meta);
    DeleteOperationRequest del_req;
    del_req.set_name(op_delete.name());
    ServerContext del_ctx;
    ASSERT_TRUE(service.DeleteOperation(&del_ctx, &del_req, &empty).ok());

    const Operation completed_on_deleted = service.CompleteOperation(op_delete.name(), result);
    EXPECT_TRUE(completed_on_deleted.done());

    GetOperationRequest get_req;
    get_req.set_name(op_delete.name());
    Operation fetched;
    ServerContext get_ctx1;
    EXPECT_EQ(service.GetOperation(&get_ctx1, &get_req, &fetched).error_code(),
              StatusCode::NOT_FOUND);

    const Operation failed_on_deleted =
            service.FailOperation(op_delete.name(), absl::InternalError("Late failure"));
    EXPECT_TRUE(failed_on_deleted.done());

    ServerContext get_ctx2;
    EXPECT_EQ(service.GetOperation(&get_ctx2, &get_req, &fetched).error_code(),
              StatusCode::NOT_FOUND);
}

}  // namespace
}  // namespace goldfish::grpc::v2
