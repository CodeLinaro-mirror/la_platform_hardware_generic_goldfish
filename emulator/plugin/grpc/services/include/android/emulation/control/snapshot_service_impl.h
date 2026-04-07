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

#include "grpcpp/grpcpp.h"

#include "snapshot_service.grpc.pb.h"

namespace android::emulation::control {

class SnapshotServiceImpl final
        : public SnapshotService::WithCallbackMethod_PullSnapshot<
                  SnapshotService::WithCallbackMethod_PushSnapshot<SnapshotService::Service>> {
  public:
    grpc::Status ListSnapshots(grpc::ServerContext* context, const SnapshotFilter* request,
                               SnapshotList* reply) override;
    grpc::ServerWriteReactor<SnapshotPackage>* PullSnapshot(
            grpc::CallbackServerContext* context, const SnapshotPackage* request) override;
    grpc::ServerReadReactor<SnapshotPackage>* PushSnapshot(grpc::CallbackServerContext* context,
                                                           SnapshotPackage* response) override;
    grpc::Status LoadSnapshot(grpc::ServerContext* context, const SnapshotPackage* request,
                              SnapshotPackage* reply) override;
    grpc::Status SaveSnapshot(grpc::ServerContext* context, const SnapshotPackage* request,
                              SnapshotPackage* reply) override;
    grpc::Status DeleteSnapshot(grpc::ServerContext* context, const SnapshotPackage* request,
                                SnapshotPackage* reply) override;
    // TrackProcess - not supported
    grpc::Status UpdateSnapshot(grpc::ServerContext* context,
                                const SnapshotUpdateDescription* request,
                                SnapshotDetails* reply) override;
    grpc::Status GetScreenshot(grpc::ServerContext* context, const SnapshotId* request,
                               SnapshotScreenshotFile* reply) override;
};

}  // namespace android::emulation::control