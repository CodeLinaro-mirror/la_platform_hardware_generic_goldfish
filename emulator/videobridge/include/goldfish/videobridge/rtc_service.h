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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "rtc_service_v2.grpc.pb.h"
#pragma clang diagnostic pop

#include <memory>

#include "goldfish/videobridge/switchboard.h"

namespace goldfish::videobridge {

namespace v2 = ::android::emulation::control::v2;

/**
 * @class RtcService
 * @brief Concrete gRPC service implementing WebRTC signaling protocols (v2).
 *
 * RtcService maps incoming gRPC stream requests, client JSEP messages, and
 * signaling stream subscriptions directly to the Switchboard engine.
 */
class RtcService final : public v2::Rtc::Service {
  public:
    explicit RtcService(std::shared_ptr<Switchboard> switchboard);
    ~RtcService() override = default;

    ::grpc::Status RequestRtcStream(::grpc::ServerContext* context,
                                    const v2::RtcStreamRequest* request,
                                    v2::RtcStreamResponse* response) override;

    ::grpc::Status SendJsepMessage(::grpc::ServerContext* context,
                                   const v2::SendJsepMessageRequest* request,
                                   v2::SendJsepMessageResponse* response) override;

    ::grpc::Status ReceiveJsepMessageStream(
            ::grpc::ServerContext* context, const v2::ReceiveJsepMessageRequest* request,
            ::grpc::ServerWriter<v2::ReceiveJsepMessageResponse>* writer) override;

    ::grpc::Status ReceiveJsepMessage(::grpc::ServerContext* context,
                                      const v2::ReceiveJsepMessageRequest* request,
                                      v2::ReceiveJsepMessageResponse* response) override;

  private:
    std::shared_ptr<Switchboard> switchboard_;
};

}  // namespace goldfish::videobridge
