// Copyright 2026 The Android Open Source Project
// SPDX-License-Identifier: Apache-2.0

#include "emulator/plugin/netsim/websocket_transport/websocket_transport.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"

#include "emulator/plugin/netsim/websocket_transport/netsim_discovery.h"
#include "emulator/plugin/netsim/websocket_transport/websocket.h"
#include "goldfish/async/async_socket.h"
#include "goldfish/async/async_socket_factory.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/async/libuv_socket_factory.h"
#include "goldfish/async/uv_to_absl.h"
#include "goldfish/network/endpoint.h"

namespace goldfish::netsim {
namespace {

using websocket::FrameHeader;
using websocket::Opcode;
using websocket::StreamEvent;

struct OutFrame {
    FrameHeader header;
    Packet payload;
};

using Frames = std::vector<OutFrame>;

OutFrame MakeFrame(Opcode opcode, Packet payload) {
    websocket::MaskKey mask = websocket::MakeMaskKey();
    FrameHeader header = websocket::EncodeHeader(opcode, payload.size(), mask, /*fin=*/true);
    websocket::ApplyMask(payload, mask);
    return OutFrame{header, std::move(payload)};
}

Frames Single(OutFrame frame) {
    Frames frames;
    frames.push_back(std::move(frame));
    return frames;
}

void ResetSocketCallbacks(goldfish::async::AsyncSocket& socket) {
    socket.SetOnCloseCallback([] {});
    socket.SetOnConnectedCallback([](goldfish::async::AsyncSocket&, absl::Status) {});
    socket.SetOnReadCallbackNoFlowControl([](std::string_view, absl::Status) {});
}

void ResetAndCloseSocket(std::shared_ptr<goldfish::async::AsyncSocket> socket) {
    if (!socket) return;
    ResetSocketCallbacks(*socket);
    socket->Close();
}

std::string UrlEscape(std::string_view value) {
    std::string out;
    for (unsigned char c : value) {
        if (absl::ascii_isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
            out.push_back(c);
        } else {
            absl::StrAppendFormat(&out, "%%%02X", c);
        }
    }
    return out;
}

class Session : public std::enable_shared_from_this<Session> {
  public:
    Session(goldfish::async::EventLoop* loop, goldfish::async::AsyncSocketFactory* socket_factory,
            TransportOptions options, PacketTransport::RecvCallback recv_cb)
            : loop_(loop)
            , socket_factory_(socket_factory)
            , options_(std::move(options))
            , recv_cb_(std::move(recv_cb)) {}

    void Shutdown() ABSL_LOCKS_EXCLUDED(mu_) {
        if (!loop_) return;
        if (loop_->IsOnLoopThread()) {
            ShutdownOnLoop();
            return;
        }
        if (!loop_->Post([self = shared_from_this()] { self->ShutdownOnLoop(); }).ok()) {
            StopAccepting();
            NotifyClosed();
        } else {
            closed_.WaitForNotification();
        }
    }

    absl::Status Initialize(const ChipConfig& chip) {
        kind_name_ = WebSocketKindName(chip.kind);
        if (loop_->IsOnLoopThread()) {
            return AnnotateInitError(absl::FailedPreconditionError(
                    "WebSocketPacketTransport::initialize cannot be called on its own EventLoop "
                    "thread because it blocks waiting for the asynchronous WebSocket handshake"));
        }

        absl::StatusOr<std::string> address = ResolveServerAddress(options_, "http.port");
        if (!address.ok()) return AnnotateInitError(address.status());
        server_address_ = *std::move(address);

        absl::StatusOr<std::vector<goldfish::network::Endpoint>> endpoints =
                ResolveServerEndpoints(server_address_);
        if (!endpoints.ok()) return AnnotateInitError(endpoints.status(), server_address_);
        endpoints_ = *std::move(endpoints);

        key_ = websocket::MakeKey();
        std::string req =
                websocket::HandshakeRequest(server_address_, WebSocketPath(chip, options_), key_);
        request_.assign(req.begin(), req.end());

        absl::Status post_status = loop_->Post([self = shared_from_this()] {
            if (self->closing_) {
                self->NotifyStarted(
                        absl::CancelledError("Transport destroyed during initialization"));
                self->NotifyClosed();
                return;
            }
            self->TryNextEndpoint();
        });
        if (!post_status.ok()) return AnnotateInitError(post_status, server_address_);

        started_.WaitForNotification();
        if (!start_status_.ok()) {
            closed_.WaitForNotification();
            return AnnotateInitError(start_status_, server_address_);
        }
        LOG(INFO) << "Netsim WebSocket transport: " << kind_name_ << " initialized";
        return absl::OkStatus();
    }

    void Send(Packet packet) ABSL_LOCKS_EXCLUDED(mu_) {
        OutFrame frame = MakeFrame(Opcode::kBinary, std::move(packet));
        bool post = false;
        {
            absl::MutexLock lock(mu_);
            if (!open_) return;
            pending_.push_back(std::move(frame));
            if (!flush_posted_) {
                flush_posted_ = true;
                post = true;
            }
        }
        if (post && loop_) {
            loop_->Post([self = shared_from_this()] { self->FlushPending(); }).IgnoreError();
        }
    }

    void NextRecv() ABSL_LOCKS_EXCLUDED(mu_) {
        bool post = false;
        {
            absl::MutexLock lock(mu_);
            if (in_callback_) {
                resume_requested_ = true;
                return;
            }
            if (paused_) {
                paused_ = false;
                post = true;
            }
        }
        if (post && loop_) {
            loop_->Post([self = shared_from_this()] { self->Resume(); }).IgnoreError();
        }
    }

  private:
    absl::Status AnnotateInitError(const absl::Status& status,
                                   std::string_view server_address = "") const {
        if (status.ok()) return status;
        if (server_address.empty()) {
            return absl::Status(
                    status.code(),
                    absl::StrFormat("Failed to initialize Netsim WebSocket transport for chip "
                                    "'%s' (instance=%d): %s",
                                    kind_name_, options_.instance, status.message()));
        }
        return absl::Status(
                status.code(),
                absl::StrFormat("Failed to initialize Netsim WebSocket transport for chip "
                                "'%s' (instance=%d, server='%s'): %s",
                                kind_name_, options_.instance, server_address, status.message()));
    }

    void ShutdownOnLoop() ABSL_LOCKS_EXCLUDED(mu_) {
        if (closing_) return;
        StopAccepting();
        if (!socket_ || !handshake_done_ || !socket_->Connected()) {
            CloseSocket();
            return;
        }
        socket_->OnFlowControlEvent(false);
        Write(Single(MakeFrame(Opcode::kClose, {})), /*close_after=*/true);
    }

    void NotifyStarted(absl::Status status) {
        if (!started_notified_.exchange(true)) {
            start_status_ = std::move(status);
            started_.Notify();
        }
    }

    void NotifyClosed() {
        if (!handshake_done_) {
            NotifyStarted(absl::CancelledError("Transport closed before handshake completed"));
        }
        if (!closed_notified_.exchange(true)) {
            closed_.Notify();
        }
    }

    bool IsPaused() ABSL_LOCKS_EXCLUDED(mu_) {
        absl::MutexLock lock(mu_);
        return paused_;
    }

    void TryNextEndpoint() ABSL_LOCKS_EXCLUDED(mu_) {
        if (closing_) {
            if (!socket_) NotifyClosed();
            return;
        }
        while (next_endpoint_ < endpoints_.size()) {
            const goldfish::network::Endpoint& endpoint = endpoints_[next_endpoint_++];
            current_endpoint_ = goldfish::network::ToString(endpoint);
            std::shared_ptr<goldfish::async::AsyncSocket> socket =
                    socket_factory_->CreateSocket(loop_, endpoint);
            if (!socket) {
                last_error_ = absl::InternalError(absl::StrFormat(
                        "Failed to create async TCP socket for endpoint '%s'", current_endpoint_));
                continue;
            }

            socket_ = socket;
            socket->SetOnReadCallbackNoFlowControl(
                    [self = shared_from_this()](std::string_view data, absl::Status status) {
                        self->OnRead(data, std::move(status));
                    });
            socket->SetOnCloseCallback([self = shared_from_this()] { self->OnClose(); });
            socket->SetOnConnectedCallback(
                    [self = shared_from_this()](goldfish::async::AsyncSocket&,
                                                absl::Status status) {
                        self->OnConnect(std::move(status));
                    });
            absl::Status status = socket->Connect();
            if (status.ok()) {
                return;
            }
            last_error_ = absl::Status(
                    status.code(),
                    absl::StrFormat("Failed to initiate TCP connect to endpoint '%s': %s",
                                    current_endpoint_, status.message()));
            ResetAndCloseSocket(std::move(socket_));
        }
        absl::Status base_error = last_error_.ok()
                                          ? goldfish::async::UvErrToAbslStatus(UV_ECONNREFUSED)
                                          : last_error_;
        Fail(absl::Status(
                base_error.code(),
                absl::StrFormat("Failed to connect to WebSocket server '%s' after trying %d "
                                "endpoint(s) (last endpoint '%s'): %s",
                                server_address_, endpoints_.size(), current_endpoint_,
                                base_error.message())));
    }

    void OnConnect(absl::Status status) ABSL_LOCKS_EXCLUDED(mu_) {
        if (closing_) return;
        if (!status.ok()) {
            last_error_ = absl::Status(status.code(),
                                       absl::StrFormat("TCP connection to endpoint '%s' failed: %s",
                                                       current_endpoint_, status.message()));
            ResetAndCloseSocket(std::move(socket_));
            TryNextEndpoint();
            return;
        }
        SendBytes(request_);
    }

    void OnRead(std::string_view data, absl::Status status) ABSL_LOCKS_EXCLUDED(mu_) {
        if (closing_) return;
        if (!status.ok()) {
            Fail(status);
            return;
        }
        if (data.empty()) return;
        const auto* bytes = reinterpret_cast<const uint8_t*>(data.data());
        parser_.Append(std::span<const uint8_t>(bytes, data.size()));
        if (!handshake_done_) {
            ProcessHandshake();
        } else {
            Deliver();
        }
    }

    void OnClose() ABSL_LOCKS_EXCLUDED(mu_) {
        if (auto sock = std::move(socket_)) {
            ResetSocketCallbacks(*sock);
        }
        if (!closing_) {
            closing_ = true;
            StopAccepting();
            if (!handshake_done_) {
                NotifyStarted(absl::UnavailableError(absl::StrFormat(
                        "Connection to '%s' closed before WebSocket handshake completed "
                        "(received %d bytes); verify that the target endpoint is a WebSocket "
                        "server (http.port) rather than a gRPC port (grpc.port)",
                        server_address_, parser_.UnconsumedBytes())));
            }
        }
        NotifyClosed();
    }

    void ProcessHandshake() ABSL_LOCKS_EXCLUDED(mu_) {
        absl::StatusOr<bool> completed = parser_.ConsumeHandshake(key_);
        if (!completed.ok()) {
            Fail(completed.status());
            return;
        }
        if (!*completed) return;
        handshake_done_ = true;
        reading_ = true;
        {
            absl::MutexLock lock(mu_);
            open_ = true;
        }
        NotifyStarted(absl::OkStatus());
        Deliver();
    }

    void Resume() ABSL_LOCKS_EXCLUDED(mu_) {
        if (closing_) return;
        Deliver();
        if (!IsPaused() && reading_ && socket_) {
            socket_->OnFlowControlEvent(true);
        }
    }

    void FlushPending() ABSL_LOCKS_EXCLUDED(mu_) {
        Frames frames;
        {
            absl::MutexLock lock(mu_);
            frames.swap(pending_);
            flush_posted_ = false;
        }
        if (!frames.empty()) Write(std::move(frames));
    }

    void Write(Frames frames, bool close_after = false) ABSL_LOCKS_EXCLUDED(mu_) {
        if (closing_ || !socket_) return;
        size_t total_size = 0;
        for (const OutFrame& frame : frames) {
            total_size += frame.header.size + frame.payload.size();
        }
        std::vector<uint8_t> bytes;
        bytes.reserve(total_size);
        for (const OutFrame& frame : frames) {
            bytes.insert(bytes.end(), frame.header.bytes.begin(),
                         frame.header.bytes.begin() + frame.header.size);
            bytes.insert(bytes.end(), frame.payload.begin(), frame.payload.end());
        }
        SendBytes(bytes, close_after);
    }

    void SendBytes(std::span<const uint8_t> bytes, bool close_after = false)
            ABSL_LOCKS_EXCLUDED(mu_) {
        if (closing_ || !socket_) return;
        absl::Status status =
                socket_->Send(reinterpret_cast<const char*>(bytes.data()), bytes.size(),
                              [self = shared_from_this(), close_after](absl::Status s) {
                                  if (close_after) {
                                      self->CloseSocket();
                                      return;
                                  }
                                  if (!s.ok() && !absl::IsCancelled(s)) {
                                      self->Fail(s);
                                  }
                              });
        if (!status.ok()) {
            if (close_after) {
                CloseSocket();
            } else {
                Fail(status);
            }
        }
    }

    void Deliver() ABSL_LOCKS_EXCLUDED(mu_) {
        std::shared_ptr<Session> keep_alive = shared_from_this();
        while (true) {
            if (IsPaused()) {
                if (socket_) socket_->OnFlowControlEvent(false);
                return;
            }
            absl::StatusOr<std::optional<StreamEvent>> event = parser_.NextEvent();
            if (!event.ok()) return Fail(event.status());
            if (!event->has_value()) return;
            switch ((*event)->kind) {
            case StreamEvent::Kind::kPing:
                Write(Single(MakeFrame(Opcode::kPong, std::move((*event)->payload))));
                continue;
            case StreamEvent::Kind::kClose:
                StopAccepting();
                if (socket_) socket_->OnFlowControlEvent(false);
                Write(Single(MakeFrame(Opcode::kClose, std::move((*event)->payload))),
                      /*close_after=*/true);
                return;
            case StreamEvent::Kind::kBinaryMessage:
                break;
            }

            Packet packet = std::move((*event)->payload);
            {
                absl::MutexLock lock(mu_);
                in_callback_ = true;
                resume_requested_ = false;
            }
            bool more = recv_cb_(packet);
            absl::MutexLock lock(mu_);
            in_callback_ = false;
            paused_ = !more && !resume_requested_;
        }
    }

    void StopAccepting() ABSL_LOCKS_EXCLUDED(mu_) {
        {
            absl::MutexLock lock(mu_);
            open_ = false;
        }
        reading_ = false;
    }

    void Fail(const absl::Status& status) ABSL_LOCKS_EXCLUDED(mu_) {
        if (closing_) return;
        LOG(WARNING) << "Netsim WebSocket transport: " << kind_name_ << " - " << status;
        parser_.Reset();
        StopAccepting();
        if (!handshake_done_) {
            NotifyStarted(status);
        }
        CloseSocket();
    }

    void CloseSocket() {
        if (closing_) return;
        closing_ = true;
        if (!socket_) {
            NotifyClosed();
            return;
        }
        socket_->Close();
    }

    goldfish::async::EventLoop* const loop_;
    goldfish::async::AsyncSocketFactory* const socket_factory_;
    const TransportOptions options_;
    const PacketTransport::RecvCallback recv_cb_;
    std::string kind_name_;
    std::string server_address_;
    std::string current_endpoint_;
    std::string key_;
    std::vector<goldfish::network::Endpoint> endpoints_;
    size_t next_endpoint_ = 0;
    std::shared_ptr<goldfish::async::AsyncSocket> socket_;
    absl::Status last_error_;

    absl::Notification started_;
    std::atomic<bool> started_notified_{false};
    absl::Status start_status_;
    absl::Notification closed_;
    std::atomic<bool> closed_notified_{false};

    absl::Mutex mu_;
    bool open_ ABSL_GUARDED_BY(mu_) = false;
    Frames pending_ ABSL_GUARDED_BY(mu_);
    bool flush_posted_ ABSL_GUARDED_BY(mu_) = false;
    bool paused_ ABSL_GUARDED_BY(mu_) = false;
    bool in_callback_ ABSL_GUARDED_BY(mu_) = false;
    bool resume_requested_ ABSL_GUARDED_BY(mu_) = false;

    Packet request_;
    websocket::StreamParser parser_;
    bool handshake_done_ = false;
    bool closing_ = false;
    bool reading_ = false;
};

class WebSocketPacketTransport : public PacketTransport {
  public:
    WebSocketPacketTransport(goldfish::async::EventLoop* loop, TransportOptions options,
                             RecvCallback recv_cb,
                             goldfish::async::AsyncSocketFactory* socket_factory)
            : loop_(loop)
            , socket_factory_(socket_factory ? socket_factory : &default_socket_factory_)
            , options_(std::move(options))
            , recv_cb_(std::move(recv_cb)) {}

    ~WebSocketPacketTransport() override {
        if (auto session = TakeSession()) session->Shutdown();
    }

    absl::Status initialize(const ChipConfig& chip) override ABSL_LOCKS_EXCLUDED(mu_) {
        const std::string_view kind_name = WebSocketKindName(chip.kind);
        if (!loop_) {
            return absl::InvalidArgumentError(absl::StrFormat(
                    "WebSocketPacketTransport::initialize requires a non-null EventLoop "
                    "(chip='%s')",
                    kind_name));
        }
        if (!recv_cb_) {
            return absl::InvalidArgumentError(absl::StrFormat(
                    "WebSocketPacketTransport::initialize requires a non-empty RecvCallback "
                    "(chip='%s')",
                    kind_name));
        }
        if (chip.sim_profile) {
            return absl::UnimplementedError(absl::StrFormat(
                    "WebSocketPacketTransport does not support ChipConfig::sim_profile "
                    "(chip='%s'); use CreatePacketTransport (gRPC) for cellular SIM profile "
                    "initialization or omit sim_profile",
                    kind_name));
        }
        {
            absl::MutexLock lock(mu_);
            if (initialized_) {
                return absl::FailedPreconditionError(absl::StrFormat(
                        "WebSocketPacketTransport for chip '%s' is already initialized; create a "
                        "separate PacketTransport instance per chip",
                        kind_name));
            }
            initialized_ = true;
        }
        auto session = std::make_shared<Session>(loop_, socket_factory_, options_, recv_cb_);
        absl::Status status = session->Initialize(chip);
        absl::MutexLock lock(mu_);
        if (!status.ok()) {
            initialized_ = false;
            return status;
        }
        session_ = std::move(session);
        return absl::OkStatus();
    }

    void send(Packet packet) override ABSL_LOCKS_EXCLUDED(mu_) {
        if (auto session = GetSession()) session->Send(std::move(packet));
    }

    void next_recv() override ABSL_LOCKS_EXCLUDED(mu_) {
        if (auto session = GetSession()) session->NextRecv();
    }

  private:
    std::shared_ptr<Session> GetSession() ABSL_LOCKS_EXCLUDED(mu_) {
        absl::MutexLock lock(mu_);
        return session_;
    }

    std::shared_ptr<Session> TakeSession() ABSL_LOCKS_EXCLUDED(mu_) {
        absl::MutexLock lock(mu_);
        return std::move(session_);
    }

    goldfish::async::EventLoop* const loop_;
    goldfish::async::LibuvAsyncSocketFactory default_socket_factory_;
    goldfish::async::AsyncSocketFactory* const socket_factory_;
    const TransportOptions options_;
    const RecvCallback recv_cb_;
    absl::Mutex mu_;
    bool initialized_ ABSL_GUARDED_BY(mu_) = false;
    std::shared_ptr<Session> session_ ABSL_GUARDED_BY(mu_);
};

void AppendParam(std::string& path, char& sep, std::string_view key, std::string_view value) {
    if (value.empty()) return;
    absl::StrAppend(&path, std::string_view(&sep, 1), key, "=", UrlEscape(value));
    sep = '&';
}

}  // namespace

std::string_view WebSocketKindName(ChipKind kind) {
    switch (kind) {
    case ChipKind::kBluetooth:
        return "bt";
    case ChipKind::kWifi:
        return "wifi";
    case ChipKind::kUwb:
        return "uwb";
    case ChipKind::kNfc:
        return "nfc";
    case ChipKind::kCellular:
        return "cellular";
    case ChipKind::kCellularData:
        return "cellular_data";
    case ChipKind::kEthernet:
        return "ethernet";
    }
    return "unknown";
}

std::string WebSocketPath(const ChipConfig& chip, const TransportOptions& options) {
    std::string path = absl::StrCat("/v1/websocket/", WebSocketKindName(chip.kind));
    char sep = '?';
    const DeviceInfo& device = options.device;
    AppendParam(path, sep, "name", device.name);
    AppendParam(path, sep, "device_kind", device.kind);
    AppendParam(path, sep, "version", device.version);
    AppendParam(path, sep, "sdk_version", device.sdk_version);
    AppendParam(path, sep, "build_id", device.build_id);
    AppendParam(path, sep, "variant", device.variant);
    AppendParam(path, sep, "arch", device.arch);
    AppendParam(path, sep, "avd_path", device.avd_path);
    AppendParam(path, sep, "chip_id", chip.id);
    AppendParam(path, sep, "manufacturer", chip.manufacturer);
    AppendParam(path, sep, "product_name", chip.product_name);
    AppendParam(path, sep, "address", chip.address);
    if (chip.loopback) AppendParam(path, sep, "loopback", "1");
    return path;
}

std::unique_ptr<PacketTransport> CreateWebSocketTransport(
        goldfish::async::EventLoop* loop, TransportOptions options,
        PacketTransport::RecvCallback recv_cb,
        goldfish::async::AsyncSocketFactory* socket_factory) {
    return std::make_unique<WebSocketPacketTransport>(loop, std::move(options), std::move(recv_cb),
                                                      socket_factory);
}

std::unique_ptr<PacketTransport> CreateWebSocketTransport(
        goldfish::async::EventLoop* loop, PacketTransport::RecvCallback recv_cb,
        goldfish::async::AsyncSocketFactory* socket_factory) {
    return CreateWebSocketTransport(loop, TransportOptions{}, std::move(recv_cb), socket_factory);
}

}  // namespace goldfish::netsim
