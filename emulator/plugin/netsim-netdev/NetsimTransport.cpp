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

#include "NetsimTransport.h"

#include <grpcpp/grpcpp.h>

#include <memory>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/time/time.h"

#include "aemu/base/utils/status_macros.h"
#include "android/emulation/control/utils/emulator_grpc_client.h"
#include "goldfish/network/GenericNetlinkMessage.h"
#include "goldfish/network/IOVector.h"
#include "netsim/packet_streamer.grpc.pb.h"
#include "netsim/packet_streamer.pb.h"

#define __packed
#define BIT(nr) (1ULL << (nr))
typedef int8_t s8;
typedef uint8_t u8;
typedef uint16_t u16;
#include "standard-headers/linux/mac80211_hwsim.h"

namespace goldfish::net {
namespace {

using netsim::packet::PacketRequest;

const absl::Duration kConnectionDeadline = absl::Seconds(15);

// Convert a protobuf bytes field into std::unique_ptr<vec<uint8_t>>.
//
// Release ownership of the bytes field and convert it to a vector using
// move iterators. No copy when called with a mutable reference.
std::unique_ptr<std::vector<uint8_t>> ToUniqueVec(std::string* bytes_field) {
    return std::make_unique<std::vector<uint8_t>>(std::make_move_iterator(bytes_field->begin()),
                                                  std::make_move_iterator(bytes_field->end()));
}

}  // namespace

/*int NetsimTransport::send(iovec* vecs, size_t len) {
    auto* transport = (NetsimWifiTransport*)impl;
    // Filter out spurious garbage data from the guest.
    goldfish::wifi::network::IOVector iov(vecs, vecs + len);
    const goldfish::wifi::network::GenericNetlinkMessage msg(iov);
    if (msg.genericNetlinkHeader()->cmd != HWSIM_CMD_FRAME) {
        return 0;
    }
    PacketRequest toSend;
    toSend.set_packet(std::string(msg.data(), msg.data() + msg.dataLen()));
    transport->Write(toSend);
    return msg.dataLen();
}*/

NetsimTransport::NetsimTransport(std::string endpoint, RecvCallback recv_cb)
        : mEndpoint(std::move(endpoint)), mRecvCb(std::move(recv_cb)) {}

NetsimTransport::~NetsimTransport() {
    cancel();
    mDone.WaitForNotification();
}

void NetsimTransport::cancel() {
    mStreamPacketsContext->TryCancel();
    mGrpcClient->disconnect();
}

absl::Status NetsimTransport::initialize() {
    PacketRequest initial_request;
    auto initial_info = initial_request.mutable_initial_info();
    initial_info->mutable_chip()->set_kind(netsim::common::ChipKind::WIFI);
    auto* device_info = initial_info->mutable_device_info();
    // TODO(whollins): set these from properties passed by the launcher.
    // avd.ini.displayname otherwise avd name.
    device_info->set_name("emulator-name");
    device_info->set_kind("EMULATOR");
    // device_info->set_version("emulator-version-string");
    //  TODO(whollins): read build.prop file from sdk and set.
    // device_info->set_sdk_version("35"); // ro.build.version.sdk
    // device_info->set_build_id("ZP1A.250125.001"); // ro.build.id
    // device_info->set_variant("sdk_gphone64_x86_64_minigbm-userdebug"); // ro.build.flavor
    // device_info->set_arch("x86_64");  // ro.product.cpu.abi

    VLOG(1) << "Creating gRPC channel to netsimd endpoint: " << mEndpoint;
    android::emulation::control::Endpoint endpoint_config;
    endpoint_config.set_target(mEndpoint);

    ASSIGN_OR_RETURN(mGrpcClient,
                     android::emulation::control::EmulatorGrpcClientBuilder()
                             .withEndpoint(endpoint_config)
                             // TODO(whollins): re-add interceptiors e.g.
                             //.withInterceptor(std::make_unique<MetricsInterceptorFactory>());
                             .buildBlocking());
    // TODO(whollins): Consider changing to non-blocking.
    RETURN_IF_ERROR(mGrpcClient->connect(kConnectionDeadline));
    ASSIGN_OR_RETURN(mPacketStreamerStub, mGrpcClient->stub<netsim::packet::PacketStreamer>());

    ASSIGN_OR_RETURN(mStreamPacketsContext, mGrpcClient->newContext());
    mPacketStreamerStub->async()->StreamPackets(mStreamPacketsContext.get(), this);
    StartCall();
    Write(initial_request);
    next_recv();

    LOG(INFO) << "Successfully initialized netsim WiFi";
    return absl::OkStatus();
}

void NetsimTransport::send(const uint8_t* buf, size_t size) {
    // Filter out spurious garbage data from the guest.
    const goldfish::network::GenericNetlinkMessage msg(buf, size);
    if (msg.genericNetlinkHeader()->cmd != HWSIM_CMD_FRAME) {
        VLOG(1) << "Not sending junk frame";
        return;
    }
    PacketRequest toSend;
    toSend.set_packet(std::string(msg.data(), msg.data() + msg.dataLen()));
    Write(toSend);
}
void NetsimTransport::OnWriteDone(bool ok) {
    if (ok) {
        std::lock_guard<std::mutex> lock(mWritelock);
        mWriteQueue.pop();
        mWriting = false;

        NextWrite_locked();
    } else {
        mWriteDone = true;
    }
}

void NetsimTransport::Write(netsim::packet::PacketRequest msg) {
    std::lock_guard<std::mutex> lock(mWritelock);
    if (mWriteDone) {
        // Can't send anymore
        return;
    }
    mWriteQueue.emplace(std::move(msg));

    NextWrite_locked();
}

void NetsimTransport::NextWrite_locked() {
    if (!mWriteQueue.empty() && !mWriting) {
        mWriting = true;
        StartWrite(&mWriteQueue.front());
    }
}

void NetsimTransport::next_recv() {
    std::lock_guard<std::mutex> lock(mReadlock);
    if (mReadDone) {
        // Can't read anymore
        return;
    }
    StartRead(&mReadBuffer);
}

void NetsimTransport::OnReadDone(bool ok) {
    if (ok) {
        if (mReadBuffer.has_packet()) {
            mRecvCb(ToUniqueVec(mReadBuffer.mutable_packet()));
            // Note that we don't automatically start another read in this case - the client must
            // call next_recv().
        } else {
            LOG(WARNING) << "Unexpected packet " << mReadBuffer.DebugString();
            next_recv();
        }
    } else {
        // Reading finished
        LOG(WARNING) << "Reading terminated";
        std::lock_guard<std::mutex> lock(mReadlock);
        mReadDone = true;
    }
}

void NetsimTransport::OnDone(const grpc::Status& s) {
    LOG(WARNING) << "Netsim Wifi " << mStreamPacketsContext->peer() << " is gone due to "
                 << s.error_message();
    mDone.Notify();
}

}  // namespace goldfish::net
