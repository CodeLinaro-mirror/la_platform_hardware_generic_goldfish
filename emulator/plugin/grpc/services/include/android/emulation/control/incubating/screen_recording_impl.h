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

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "grpcpp/grpcpp.h"

#include "goldfish/display/QemuMultidisplay/multi_display.h"
#include "goldfish/display/video_recorder.h"
#include "screen_recording_service.grpc.pb.h"

using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::Status;

namespace android {
namespace emulation {
namespace control {
namespace incubating {

class ScreenRecordingServiceImpl final : public ScreenRecording::Service {
  public:
    ScreenRecordingServiceImpl(::goldfish::display::IMultiDisplay* display);

    // RPC: Start a new recording
    Status StartRecording(ServerContext* context, const RecordingInfo* request,
                          RecordingInfo* response) override;

    // RPC: Stop an active recording
    Status StopRecording(ServerContext* context, const RecordingInfo* request,
                         RecordingInfo* response) override;

    // RPC: List all known recordings
    Status ListRecordings(ServerContext* context, const RecordingInfo* request,
                          RecordingInfoList* response) override;

    // RPC: Stream events (e.g., status updates)
    Status ReceiveRecordingEvents(ServerContext* context, const google::protobuf::Empty* request,
                                  ServerWriter<RecordingInfo>* writer) override;

  private:
    // Helper to validate request parameters
    bool ValidateRecordingParams(const RecordingInfo* info, std::string& error_msg);

    // Internal storage for active recordings
    // Key: file_name, Value: RecordingInfo
    std::map<std::string, RecordingInfo> recordings_ ABSL_GUARDED_BY(mu_);

    // Mutex to protect the recordings_ map
    absl::Mutex mu_;

    // Display manager to get hold of the requested display
    ::goldfish::display::IMultiDisplay& display_;

    // Recorer: for now, there is only one active recorder
    std::unique_ptr<::goldfish::display::VideoRecorder> recorder_;
};

}  // namespace incubating
}  // namespace control
}  // namespace emulation
}  // namespace android
