// Copyright 2025 The Android Open Source Project
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

#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/synchronization/notification.h"

#include "emulator/plugin/netsim/packet_transport.h"
#include "netsim/packet_streamer.grpc.pb.h"
#include "netsim/packet_streamer.pb.h"

namespace goldfish::netsim {

class NetsimTransport : public PacketTransport,
                        public grpc::ClientBidiReactor<::netsim::packet::PacketRequest,
                                                       ::netsim::packet::PacketResponse> {
  public:
    explicit NetsimTransport(PacketTransport::RecvCallback recv_cb);
    ~NetsimTransport() override;

    absl::Status initialize(const ChipConfig& chip) override;

    void send(Packet packet) override;

    void next_recv() override;

  private:
    void cancel();
    void send_proto(::netsim::packet::PacketRequest msg);
    void OnDone(const grpc::Status& s) override;

    void OnReadDone(bool ok) override;

    void OnWriteDone(bool ok) override;
    void NextWrite_locked();

    PacketTransport::RecvCallback mRecvCb;

    std::string mKindName;
    ChipKind mKind{ChipKind::kBluetooth};

    std::unique_ptr<::netsim::packet::PacketStreamer::Stub> mPacketStreamerStub;

    std::unique_ptr<grpc::ClientContext> mStreamPacketsContext;

    std::mutex mWritelock;
    std::queue<::netsim::packet::PacketRequest> mWriteQueue;
    bool mWriting{false};
    bool mWriteDone{false};

    ::netsim::packet::PacketResponse mReadBuffer;
    Packet mReadPacket;
    std::mutex mReadlock;
    bool mReadDone{false};
    bool mReading{false};
    bool mResumeRequested{false};

    absl::Notification mDone;
};

}  // namespace goldfish::netsim
