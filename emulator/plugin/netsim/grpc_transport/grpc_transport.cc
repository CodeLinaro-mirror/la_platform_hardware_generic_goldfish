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

#include "grpc_transport.h"

#include <grpcpp/grpcpp.h>
#include <grpcpp/support/status.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/time/time.h"

#include "android/emulation/control/emulator_grpc_client.h"
#include "android/status/status_macros.h"
#include "emulator/plugin/netsim/netsim_connection_internal.h"
#include "goldfish/avd/avd_info.h"
#include "goldfish/tools/aemu_version.h"
#include "netsim/common.pb.h"
#include "netsim/packet_streamer.grpc.pb.h"
#include "netsim/packet_streamer.pb.h"

namespace goldfish::netsim {
namespace {

::netsim::common::ChipKind ToProto(ChipKind kind) {
    switch (kind) {
    case ChipKind::kBluetooth:
        return ::netsim::common::BLUETOOTH;
    case ChipKind::kWifi:
        return ::netsim::common::WIFI;
    case ChipKind::kUwb:
        return ::netsim::common::UWB;
    case ChipKind::kNfc:
        return ::netsim::common::NFC;
    case ChipKind::kCellular:
        return ::netsim::common::CELLULAR;
    case ChipKind::kCellularData:
        return ::netsim::common::CELLULAR_DATA;
    case ChipKind::kEthernet:
        return ::netsim::common::ETHERNET;
    }
    return ::netsim::common::UNSPECIFIED;
}

}  // namespace

NetsimTransport::NetsimTransport(PacketTransport::RecvCallback recv_cb)
        : mRecvCb(std::move(recv_cb)) {}

NetsimTransport::~NetsimTransport() {
    cancel();
    mDone.WaitForNotification();
}

void NetsimTransport::cancel() {
    if (mDone.HasBeenNotified()) {
        return;
    }
    if (mStreamPacketsContext) {
        mStreamPacketsContext->TryCancel();
    } else {
        // We'll never be notified if the context was never created.
        mDone.Notify();
    }
}

absl::Status NetsimTransport::initialize(const ChipConfig& chip_config) {
    mKind = chip_config.kind;
    ::netsim::common::ChipKind proto_kind = ToProto(chip_config.kind);
    mKindName = ::netsim::common::ChipKind_Name(proto_kind);

    ::netsim::startup::Chip chip;
    chip.set_kind(proto_kind);
    chip.set_id(chip_config.id);
    chip.set_manufacturer(chip_config.manufacturer);
    chip.set_product_name(chip_config.product_name);
    chip.set_address(chip_config.address);
    if (chip_config.sim_profile) {
        chip.set_sim_profile(*chip_config.sim_profile);
    }
    chip.set_loopback(chip_config.loopback);

    auto& avdprops = goldfish::avd::GetAvd().Props();

    ::netsim::packet::PacketRequest initial_request;
    auto* initial_info = initial_request.mutable_initial_info();
    *initial_info->mutable_chip() = std::move(chip);
    auto* device_info = initial_info->mutable_device_info();
    device_info->set_name(avdprops.avd_name);
    device_info->set_kind("EMULATOR");
    device_info->set_version(std::string(::goldfish::version::GetEmulatorVersion()));
    device_info->set_sdk_version(avdprops.build_sdk);
    device_info->set_build_id(avdprops.build_id);
    device_info->set_variant(avdprops.build_flavor);
    device_info->set_arch(avdprops.avd_abi);

    // Hold the ptr until we're done initializing, after that the stub has access to the channel.
    ASSIGN_OR_RETURN(
            std::shared_ptr<android::emulation::control::EmulatorGrpcClientBase> grpc_client,
            get_connected_netsim_grpc_client());

    ASSIGN_OR_RETURN(mPacketStreamerStub, grpc_client->Stub<::netsim::packet::PacketStreamer>());

    ASSIGN_OR_RETURN(mStreamPacketsContext, grpc_client->NewContext());
    mPacketStreamerStub->async()->StreamPackets(mStreamPacketsContext.get(), this);
    StartCall();
    send_proto(std::move(initial_request));
    next_recv();

    LOG(INFO) << "Netsim Transport: " << mKindName << " - successfully initialized";
    return absl::OkStatus();
}

void NetsimTransport::send_proto(::netsim::packet::PacketRequest msg) {
    std::lock_guard<std::mutex> lock(mWritelock);
    if (mWriteDone) {
        // Can't send anymore
        return;
    }
    mWriteQueue.emplace(std::move(msg));

    NextWrite_locked();
}

void NetsimTransport::send(Packet packet) {
    if (packet.empty()) return;
    ::netsim::packet::PacketRequest request;
    if (mKind == ChipKind::kBluetooth) {
        auto* hci = request.mutable_hci_packet();
        hci->set_packet_type(static_cast<::netsim::packet::HCIPacket::PacketType>(packet[0]));
        hci->set_packet(packet.data() + 1, packet.size() - 1);
    } else {
        request.set_packet(packet.data(), packet.size());
    }
    send_proto(std::move(request));
}

void NetsimTransport::OnWriteDone(bool ok) {
    std::lock_guard<std::mutex> lock(mWritelock);
    if (ok) {
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
        return;
    }
    if (mReading) {
        mResumeRequested = true;
        return;
    }
    mReading = true;
    StartRead(&mReadBuffer);
}

void NetsimTransport::OnReadDone(bool ok) {
    if (ok) {
        bool valid = false;
        {
            std::lock_guard<std::mutex> lock(mReadlock);
            mReadPacket.clear();
            if (mKind == ChipKind::kBluetooth && mReadBuffer.has_hci_packet()) {
                mReadPacket.reserve(1 + mReadBuffer.hci_packet().packet().size());
                mReadPacket.push_back(static_cast<uint8_t>(mReadBuffer.hci_packet().packet_type()));
                mReadPacket.insert(mReadPacket.end(), mReadBuffer.hci_packet().packet().begin(),
                                   mReadBuffer.hci_packet().packet().end());
                valid = true;
            } else if (mReadBuffer.has_packet()) {
                mReadPacket.assign(mReadBuffer.packet().begin(), mReadBuffer.packet().end());
                valid = true;
            } else {
                LOG(WARNING) << "Unexpected packet " << mReadBuffer.DebugString();
            }
            mResumeRequested = false;
        }

        bool continue_read = !valid || mRecvCb(mReadPacket);

        {
            std::lock_guard<std::mutex> lock(mReadlock);
            if (!mReadDone && (continue_read || mResumeRequested)) {
                mResumeRequested = false;
                StartRead(&mReadBuffer);
            } else {
                mReading = false;
            }
        }
    } else {
        // Reading finished
        VLOG(1) << "Netsim Transport: " << mKindName << " - reading terminated";
        std::lock_guard<std::mutex> lock(mReadlock);
        mReadDone = true;
        mReading = false;
    }
}

void NetsimTransport::OnDone(const grpc::Status& s) {
    if (s.error_code() == grpc::StatusCode::CANCELLED) {
        LOG(INFO) << "Netsim Transport: " << mKindName << " - connection to "
                  << mStreamPacketsContext->peer() << " was cancelled";
    } else {
        LOG(WARNING) << "Netsim Transport: " << mKindName << " - connection to "
                     << mStreamPacketsContext->peer() << " is gone due to " << s.error_message();
    }
    mDone.Notify();
}

std::unique_ptr<PacketTransport> CreatePacketTransport(PacketTransport::RecvCallback recv_cb) {
    return std::make_unique<NetsimTransport>(std::move(recv_cb));
}

}  // namespace goldfish::netsim
