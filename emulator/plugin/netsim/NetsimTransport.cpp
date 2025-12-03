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
#include <grpcpp/support/status.h>

#include <memory>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/time/time.h"

#include "aemu/base/utils/status_macros.h"
#include "android/emulation/control/utils/emulator_grpc_client.h"
#include "goldfish/avd/avd-info.h"
#include "goldfish/tools/aemu_version.h"
#include "netsim/packet_streamer.grpc.pb.h"
#include "netsim/packet_streamer.pb.h"

namespace goldfish::netsim {
namespace {
const absl::Duration kConnectionDeadline = absl::Seconds(15);
}  // namespace

std::unique_ptr<std::vector<uint8_t>> ToUniqueVec(std::string* bytes_field) {
    return std::make_unique<std::vector<uint8_t>>(std::make_move_iterator(bytes_field->begin()),
                                                  std::make_move_iterator(bytes_field->end()));
}

NetsimTransport::NetsimTransport(std::string endpoint, RecvCallback recv_cb)
        : mEndpoint(std::move(endpoint)), mRecvCb(std::move(recv_cb)) {}

NetsimTransport::~NetsimTransport() {
    cancel();
    mDone.WaitForNotification();
}

void NetsimTransport::cancel() {
    if (mStreamPacketsContext) {
        mStreamPacketsContext->TryCancel();
    } else {
        // We'll never be notified if the context was never created.
        mDone.Notify();
    }
    if (mGrpcClient) {
        mGrpcClient->disconnect();
    }
}

absl::Status NetsimTransport::initialize(::netsim::startup::Chip chip) {
    mKindName = ::netsim::common::ChipKind_Name(chip.kind());
    auto& avdprops = goldfish::avd_info::getAvd().props();

    ::netsim::packet::PacketRequest initial_request;
    auto* initial_info = initial_request.mutable_initial_info();
    *initial_info->mutable_chip() = std::move(chip);
    auto* device_info = initial_info->mutable_device_info();
    device_info->set_name(avdprops.avd_name);
    device_info->set_kind("EMULATOR");
    device_info->set_version(VERSION);
    device_info->set_sdk_version(avdprops.build_sdk);
    device_info->set_build_id(avdprops.build_id);
    device_info->set_variant(avdprops.build_flavour);
    device_info->set_arch(avdprops.avd_abi);

    VLOG(1) << "Netsim Transport " << mEndpoint
            << " - creating gRPC channel to netsimd endpoint: " << mEndpoint;
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
    ASSIGN_OR_RETURN(mPacketStreamerStub, mGrpcClient->stub<::netsim::packet::PacketStreamer>());

    ASSIGN_OR_RETURN(mStreamPacketsContext, mGrpcClient->newContext());
    mPacketStreamerStub->async()->StreamPackets(mStreamPacketsContext.get(), this);
    StartCall();
    send(initial_request);
    next_recv();

    LOG(INFO) << "Netsim Transport " << mKindName << " - successfully initialized";
    return absl::OkStatus();
}

void NetsimTransport::send(::netsim::packet::PacketRequest msg) {
    std::lock_guard<std::mutex> lock(mWritelock);
    if (mWriteDone) {
        // Can't send anymore
        return;
    }
    mWriteQueue.emplace(std::move(msg));

    NextWrite_locked();
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
        if (mRecvCb(&mReadBuffer)) {
            next_recv();
        }
    } else {
        // Reading finished
        VLOG(1) << "Netsim Transport " << mKindName << " - reading terminated";
        std::lock_guard<std::mutex> lock(mReadlock);
        mReadDone = true;
    }
}

void NetsimTransport::OnDone(const grpc::Status& s) {
    if (s.error_code() == grpc::StatusCode::CANCELLED) {
        LOG(INFO) << "Netsim Transport " << mKindName << " - connection to "
                  << mStreamPacketsContext->peer() << " was cancelled";
    } else {
        LOG(WARNING) << "Netsim Transport " << mKindName << " - connection to "
                     << mStreamPacketsContext->peer() << " is gone due to " << s.error_message();
    }
    mDone.Notify();
}

}  // namespace goldfish::netsim
