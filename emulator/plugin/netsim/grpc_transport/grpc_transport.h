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
#include <queue>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
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

    absl::Status initialize(const ChipConfig& chip) override
            ABSL_LOCKS_EXCLUDED(mWritelock, mReadlock);

    void send(Packet packet) override ABSL_LOCKS_EXCLUDED(mWritelock);

    void next_recv() override ABSL_LOCKS_EXCLUDED(mReadlock);

  private:
    void cancel();
    void send_proto(::netsim::packet::PacketRequest msg) ABSL_LOCKS_EXCLUDED(mWritelock);
    void OnDone(const grpc::Status& s) override;

    void OnReadDone(bool ok) override ABSL_LOCKS_EXCLUDED(mReadlock);

    void OnWriteDone(bool ok) override ABSL_LOCKS_EXCLUDED(mWritelock);
    void NextWrite_locked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mWritelock);

    PacketTransport::RecvCallback mRecvCb;

    std::string mKindName;
    ChipKind mKind{ChipKind::kBluetooth};

    std::unique_ptr<::netsim::packet::PacketStreamer::Stub> mPacketStreamerStub;

    std::unique_ptr<grpc::ClientContext> mStreamPacketsContext;

    std::queue<::netsim::packet::PacketRequest> mWriteQueue ABSL_GUARDED_BY(mWritelock);
    bool mWriting ABSL_GUARDED_BY(mWritelock){false};
    bool mWriteDone ABSL_GUARDED_BY(mWritelock){false};

    ::netsim::packet::PacketResponse mReadBuffer ABSL_GUARDED_BY(mReadlock);
    Packet mReadPacket;
    bool mReadDone ABSL_GUARDED_BY(mReadlock){false};
    bool mReading ABSL_GUARDED_BY(mReadlock){false};
    bool mResumeRequested ABSL_GUARDED_BY(mReadlock){false};

    absl::Notification mDone;
    absl::Mutex mWritelock;
    absl::Mutex mReadlock;
};

}  // namespace goldfish::netsim
