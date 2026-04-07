// Copyright 2026 The Android Open Source Project
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

#include "android/emulation/control/snapshot_service_impl.h"

namespace android::emulation::control {

grpc::Status SnapshotServiceImpl::ListSnapshots(grpc::ServerContext* context,
                                                const SnapshotFilter* request,
                                                SnapshotList* reply) {
    return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "Not implemented yet");
}

grpc::ServerWriteReactor<SnapshotPackage>* SnapshotServiceImpl::PullSnapshot(
        grpc::CallbackServerContext* context, const SnapshotPackage* request) {
    return nullptr;
}

grpc::ServerReadReactor<SnapshotPackage>* SnapshotServiceImpl::PushSnapshot(
        grpc::CallbackServerContext* context, SnapshotPackage* response) {
    return nullptr;
}

grpc::Status SnapshotServiceImpl::LoadSnapshot(grpc::ServerContext* context,
                                               const SnapshotPackage* request,
                                               SnapshotPackage* reply) {
    return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "Not implemented yet");
}

grpc::Status SnapshotServiceImpl::SaveSnapshot(grpc::ServerContext* context,
                                               const SnapshotPackage* request,
                                               SnapshotPackage* reply) {
    return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "Not implemented yet");
}

grpc::Status SnapshotServiceImpl::DeleteSnapshot(grpc::ServerContext* context,
                                                 const SnapshotPackage* request,
                                                 SnapshotPackage* reply) {
    return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "Not implemented yet");
}

grpc::Status SnapshotServiceImpl::UpdateSnapshot(grpc::ServerContext* context,
                                                 const SnapshotUpdateDescription* request,
                                                 SnapshotDetails* reply) {
    return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "Not implemented yet");
}

grpc::Status SnapshotServiceImpl::GetScreenshot(grpc::ServerContext* context,
                                                const SnapshotId* request,
                                                SnapshotScreenshotFile* reply) {
    return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "Not implemented yet");
}

}  // namespace android::emulation::control
