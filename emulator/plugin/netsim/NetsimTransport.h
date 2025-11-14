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

#include <queue>

#include "absl/synchronization/notification.h"
#include "netsim/packet_streamer.grpc.pb.h"
#include "netsim/packet_streamer.pb.h"

namespace android::emulation::control {
class BlockingEmulatorGrpcClient;
}

namespace goldfish::netsim {

// Convert a protobuf bytes field into std::unique_ptr<vec<uint8_t>>.
//
// Release ownership of the bytes field and convert it to a vector using
// move iterators. No copy when called with a mutable reference.
std::unique_ptr<std::vector<uint8_t>> ToUniqueVec(std::string* bytes_field);

class NetsimTransport : public grpc::ClientBidiReactor<::netsim::packet::PacketRequest,
                                                       ::netsim::packet::PacketResponse> {
 public:
  using RecvCallback = std::function<bool(::netsim::packet::PacketResponse *packet)>;

  NetsimTransport(std::string endpoint, RecvCallback recv_cb);
  ~NetsimTransport() override;

  absl::Status initialize(::netsim::startup::Chip chip);

  void send(::netsim::packet::PacketRequest msg);

  void next_recv();

 private:
  void cancel();
  void OnDone(const grpc::Status& s) override;

  void OnReadDone(bool ok) override;

  void OnWriteDone(bool ok) override;
  void NextWrite_locked();

  std::string mEndpoint;
  RecvCallback mRecvCb;

  std::string mKindName;

  std::unique_ptr<android::emulation::control::BlockingEmulatorGrpcClient> mGrpcClient;
  std::unique_ptr<::netsim::packet::PacketStreamer::Stub> mPacketStreamerStub;

  std::unique_ptr<grpc::ClientContext> mStreamPacketsContext;

  std::mutex mWritelock;
  std::queue<::netsim::packet::PacketRequest> mWriteQueue;
  bool mWriting{false};
  bool mWriteDone{false};

  ::netsim::packet::PacketResponse mReadBuffer;
  std::mutex mReadlock;
  bool mReadDone{false};

  absl::Notification mDone;
};

}  // namespace goldfish::netsim
