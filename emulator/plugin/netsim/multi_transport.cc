// Copyright 2026 The Android Open Source Project
// SPDX-License-Identifier: Apache-2.0

#include "emulator/plugin/netsim/multi_transport.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"

#include "android/base/system.h"

namespace goldfish::netsim {
namespace {

constexpr TransportType kDefaultTransport = TransportType::kAuto;

std::optional<std::string> ReadEnvVar(std::string_view name) {
    std::optional<std::string> value = android::base::System::Get()->EnvGet(name);
    if (!value || value->empty()) {
        return std::nullopt;
    }
    return value;
}

bool ShouldFallBackToGrpc(const absl::Status& status) {
    switch (status.code()) {
    case absl::StatusCode::kNotFound:
    case absl::StatusCode::kUnavailable:
    case absl::StatusCode::kUnimplemented:
        return true;
    default:
        return false;
    }
}

}  // namespace

absl::StatusOr<TransportType> ParseTransportType(std::string_view value) {
    std::string lower = absl::AsciiStrToLower(value);
    if (lower == "auto") return TransportType::kAuto;
    if (lower == "grpc") return TransportType::kGrpc;
    if (lower == "websocket") return TransportType::kWebSocket;
    return absl::InvalidArgumentError(absl::StrCat("Invalid transport value '", value,
                                                   "': expected 'auto', 'grpc', or 'websocket'"));
}

absl::StatusOr<TransportType> TransportTypeFromEnv() {
    std::string_view var_name = kPacketStreamEnvVar;
    std::optional<std::string> value = ReadEnvVar(var_name);
    if (!value) {
        var_name = kTransportEnvVar;
        value = ReadEnvVar(var_name);
    }
    if (!value) return kDefaultTransport;
    absl::StatusOr<TransportType> parsed = ParseTransportType(*value);
    if (!parsed.ok()) {
        return absl::InvalidArgumentError(
                absl::StrCat("Invalid ", var_name, " value '", *value,
                             "': expected 'auto', 'grpc', or 'websocket' (or unset ", var_name,
                             " to default to 'auto')"));
    }
    return parsed;
}

MultiTransport::MultiTransport(TransportType type, RecvCallback recv_cb,
                               TransportFactory grpc_factory, TransportFactory ws_factory)
        : type_(type)
        , recv_cb_(std::move(recv_cb))
        , grpc_factory_(std::move(grpc_factory))
        , ws_factory_(std::move(ws_factory)) {}

MultiTransport::~MultiTransport() {
    Use(nullptr);
}

absl::Status MultiTransport::initialize(const ChipConfig& chip) {
    if (!recv_cb_) {
        return absl::InvalidArgumentError(
                "MultiTransport::initialize requires a non-empty RecvCallback");
    }
    {
        absl::MutexLock lock(&mu_);
        if (initialized_) {
            return absl::FailedPreconditionError(
                    "MultiTransport is already initialized; create a separate MultiTransport "
                    "instance per chip");
        }
        initialized_ = true;
    }

    absl::Status status = absl::InvalidArgumentError(
            absl::StrCat("Unsupported TransportType value ", static_cast<int>(type_),
                         "; expected kAuto (0), kGrpc (1), or kWebSocket (2)"));
    switch (type_) {
    case TransportType::kGrpc:
        status = InitializeWithFactory(grpc_factory_, "gRPC", chip);
        break;
    case TransportType::kWebSocket:
        status = InitializeWithFactory(ws_factory_, "WebSocket", chip);
        break;
    case TransportType::kAuto: {
        absl::Status ws_status = InitializeWithFactory(ws_factory_, "WebSocket", chip);
        if (ws_status.ok() || !ShouldFallBackToGrpc(ws_status)) {
            status = ws_status;
            break;
        }
        LOG(INFO) << "Netsim WebSocket transport unavailable (" << ws_status
                  << "); falling back to gRPC";
        absl::Status grpc_status = InitializeWithFactory(grpc_factory_, "gRPC", chip);
        if (grpc_status.ok()) {
            status = grpc_status;
            break;
        }
        status = absl::Status(
                grpc_status.code(),
                absl::StrCat("Netsim kAuto transport initialization failed: gRPC fallback failed (",
                             grpc_status.message(), ") after WebSocket failed (",
                             ws_status.ToString(), ")"));
        break;
    }
    }
    if (!status.ok()) {
        absl::MutexLock lock(&mu_);
        initialized_ = false;
    }
    return status;
}

void MultiTransport::send(Packet packet) {
    if (std::shared_ptr<PacketTransport> transport = active()) {
        transport->send(std::move(packet));
    }
}

void MultiTransport::next_recv() {
    if (std::shared_ptr<PacketTransport> transport = active()) {
        transport->next_recv();
    }
}

absl::Status MultiTransport::InitializeWithFactory(const TransportFactory& factory,
                                                   std::string_view name, const ChipConfig& chip) {
    if (!factory) {
        return absl::UnimplementedError(
                absl::StrCat("Netsim ", name,
                             " transport factory is not configured; provide a non-null "
                             "TransportFactory to MultiTransport"));
    }
    std::unique_ptr<PacketTransport> transport = factory(recv_cb_);
    if (!transport) {
        return absl::UnimplementedError(absl::StrCat(
                "Netsim ", name,
                " transport factory returned nullptr; ensure the factory constructs a valid "
                "PacketTransport instance"));
    }
    // Published before initialize() so that a next_recv() from the first
    // callback reaches the transport that delivered it.
    std::shared_ptr<PacketTransport> shared = Use(std::move(transport));
    absl::Status status = shared->initialize(chip);
    if (!status.ok()) {
        Use(nullptr);
    }
    return status;
}

std::shared_ptr<PacketTransport> MultiTransport::Use(std::unique_ptr<PacketTransport> transport) {
    std::shared_ptr<PacketTransport> shared = std::move(transport);
    std::shared_ptr<PacketTransport> old;
    {
        absl::MutexLock lock(&mu_);
        old = std::move(active_);
        active_ = shared;
    }
    return shared;
}

std::shared_ptr<PacketTransport> MultiTransport::active() {
    absl::MutexLock lock(&mu_);
    return active_;
}

}  // namespace goldfish::netsim
