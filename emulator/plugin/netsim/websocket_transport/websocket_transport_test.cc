// Copyright 2026 The Android Open Source Project
// SPDX-License-Identifier: Apache-2.0

#include "emulator/plugin/netsim/websocket_transport/websocket_transport.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/notification.h"

#include "android/base/testing/needs_winsock.h"
#include "android/base/testing/test_system.h"
#include "android/goldfish/ini_file.h"
#include "emulator/plugin/netsim/websocket_transport/netsim_discovery.h"
#include "emulator/plugin/netsim/websocket_transport/websocket.h"
#include "goldfish/async/async_socket.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/testing/fake_async_socket.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/file/file.h"
#include "goldfish/network/endpoint.h"

namespace goldfish::netsim {
namespace {

using namespace std::chrono_literals;
using websocket::Frame;
using websocket::MaskKey;
using websocket::Opcode;
using websocket::StreamEvent;
using websocket::StreamParser;

class PacketQueue {
  public:
    PacketTransport::RecvCallback Callback(bool more = true) {
        return [this, more](Packet& packet) {
            {
                std::lock_guard<std::mutex> lock(mu_);
                packets_.push_back(std::move(packet));
            }
            cv_.notify_all();
            return more;
        };
    }

    std::optional<Packet> Pop(std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
        std::unique_lock<std::mutex> lock(mu_);
        if (!cv_.wait_for(lock, timeout, [this] { return !packets_.empty(); })) return std::nullopt;
        Packet packet = std::move(packets_.front());
        packets_.pop_front();
        return packet;
    }

  private:
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Packet> packets_;
};

std::vector<uint8_t> Bytes(size_t len, uint8_t seed = 0x42) {
    std::vector<uint8_t> out(len);
    for (size_t i = 0; i < len; ++i) out[i] = static_cast<uint8_t>(seed + i);
    return out;
}

TEST(IniDiscoveryTest, ParsesKeyValueLinesAndDiscoveryFile) {
    android::base::TestSystem test_system("/progdir");
    std::filesystem::path dir = test_system.GetTempRoot()->Path() / "ws_endpoint_test";
    ASSERT_TRUE(android::base::file::mkdir_recursive(dir, 0755).ok());
    test_system.EnvSet("TMPDIR", dir.string());

    android::goldfish::IniFile ini(dir / "netsim_3.ini");
    ini.SetString("grpc.port", "1234");
    ini.SetString("http.port", "[::1]:5678");
    ASSERT_TRUE(ini.Write());

    EXPECT_EQ(*ServerAddressFromIni(3, "grpc.port"), "localhost:1234");
    EXPECT_EQ(*ServerAddressFromIni(3, "http.port"), "[::1]:5678");
    EXPECT_EQ(ServerAddressFromIni(3, "missing.port").status().code(), absl::StatusCode::kNotFound);

    auto endpoints = ServerEndpointsFromIni(3, "http.port");
    ASSERT_TRUE(endpoints.ok()) << endpoints.status();
    ASSERT_FALSE(endpoints->empty());
    EXPECT_EQ(goldfish::network::GetPortFromEndpoint(endpoints->front()), 5678);

    test_system.EnvSet("TMPDIR", "");
#if !defined(_WIN32) && !defined(__APPLE__)
    test_system.EnvSet("ANDROID_EMULATOR_DISCOVERY_DIR", "/custom/discovery");
    EXPECT_EQ(IniFilePath(1), std::filesystem::path("/custom/discovery/netsim.ini"));
    test_system.EnvSet("ANDROID_EMULATOR_DISCOVERY_DIR", "");
#endif
    EXPECT_EQ(IniFilePath(1).filename(), "netsim.ini");
    EXPECT_EQ(IniFilePath(3).filename(), "netsim_3.ini");
}

TEST(IniDiscoveryTest, ParsesServerAddressHostAndPort) {
    EXPECT_EQ(*ParseAddress("localhost:8554"),
              std::make_pair(std::string("localhost"), std::string("8554")));
    EXPECT_EQ(*ParseAddress("127.0.0.1"),
              std::make_pair(std::string("127.0.0.1"), std::string("80")));
    EXPECT_EQ(*ParseAddress("[::1]:5678"), std::make_pair(std::string("::1"), std::string("5678")));
    EXPECT_EQ(*ParseAddress("[::1]"), std::make_pair(std::string("::1"), std::string("80")));
    EXPECT_EQ(*ParseAddress("::1"), std::make_pair(std::string("::1"), std::string("80")));
    EXPECT_EQ(ParseAddress("").status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(ParseAddress("[]").status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(ParseAddress("[::1").status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(ParseAddress("[::1]:").status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(ParseAddress("[::1]abc").status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(ParseAddress("localhost:").status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(ParseAddress(":8554").status().code(), absl::StatusCode::kInvalidArgument);
}

class RoundTrip : public testing::TestWithParam<size_t> {};

TEST_P(RoundTrip, MaskedAndUnmasked) {
    std::vector<uint8_t> payload = Bytes(GetParam());
    for (std::optional<MaskKey> mask :
         {std::optional<MaskKey>(), std::optional<MaskKey>(MaskKey{{1, 2, 3, 4}})}) {
        std::vector<uint8_t> encoded = websocket::EncodeFrame(Opcode::kBinary, payload, mask);
        size_t consumed = 0;
        auto frame = websocket::DecodeFrame(encoded, &consumed);
        ASSERT_TRUE(frame.ok()) << frame.status();
        ASSERT_TRUE(frame->has_value());
        EXPECT_EQ(consumed, encoded.size());
        EXPECT_TRUE((*frame)->fin);
        EXPECT_EQ((*frame)->opcode, Opcode::kBinary);
        EXPECT_EQ((*frame)->masked, mask.has_value());
        EXPECT_EQ((*frame)->payload, payload);
    }
}

INSTANTIATE_TEST_SUITE_P(Lengths, RoundTrip, testing::Values(0, 1, 125, 126, 65535, 65536));

TEST(WebSocketCodecTest, ValidatesHandshakeAndRejectsMalformedFrames) {
    std::string key = "dGhlIHNhbXBsZSBub25jZQ==";
    EXPECT_EQ(websocket::AcceptKey(key), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");

    std::string good =
            "HTTP/1.1 101 Switching Protocols\r\nupgrade: WebSocket\r\nConnection: "
            "keep-alive, Upgrade\r\n"
            "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n";
    EXPECT_TRUE(websocket::ValidateHandshakeResponse(good, key).ok());
    EXPECT_FALSE(websocket::ValidateHandshakeResponse("HTTP/1.1 404 Not Found\r\n", key).ok());

    size_t consumed = 0;
    EXPECT_FALSE(websocket::DecodeFrame(std::vector<uint8_t>{0xC2, 0x00}, &consumed).ok());
    EXPECT_FALSE(websocket::DecodeFrame(std::vector<uint8_t>{0x83, 0x00}, &consumed).ok());
    EXPECT_FALSE(websocket::DecodeFrame(
                         websocket::EncodeFrame(Opcode::kPing, Bytes(126), std::nullopt), &consumed)
                         .ok());
}

TEST(StreamParserTest, ReassemblesFragmentsAndHandlesInterleavedControlFrames) {
    StreamParser parser;
    std::string key = "dGhlIHNhbXBsZSBub25jZQ==";
    std::string handshake =
            "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n";
    parser.Append(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(handshake.data()),
                                           handshake.size()));
    absl::StatusOr<bool> hs = parser.ConsumeHandshake(key);
    ASSERT_TRUE(hs.ok()) << hs.status();
    EXPECT_TRUE(*hs);

    parser.Append(websocket::EncodeFrame(Opcode::kBinary, Packet{1, 2}, std::nullopt,
                                         /*fin=*/false));
    parser.Append(websocket::EncodeFrame(Opcode::kPing, Packet{9}, std::nullopt));
    parser.Append(websocket::EncodeFrame(Opcode::kContinuation, Packet{3}, std::nullopt));

    auto first = parser.NextEvent();
    ASSERT_TRUE(first.ok()) << first.status();
    ASSERT_TRUE(first->has_value());
    EXPECT_EQ((*first)->kind, StreamEvent::Kind::kPing);
    EXPECT_EQ((*first)->payload, (Packet{9}));

    auto second = parser.NextEvent();
    ASSERT_TRUE(second.ok()) << second.status();
    ASSERT_TRUE(second->has_value());
    EXPECT_EQ((*second)->kind, StreamEvent::Kind::kBinaryMessage);
    EXPECT_EQ((*second)->payload, (Packet{1, 2, 3}));
}

TEST(StreamParserTest, DecodesFrameStraddlingSocketBufferWrapAround) {
    StreamParser parser;
    Packet first_payload = Bytes(800, 0x10);
    Packet second_payload = Bytes(500, 0x20);
    std::vector<uint8_t> first_frame =
            websocket::EncodeFrame(Opcode::kBinary, first_payload, std::nullopt);
    std::vector<uint8_t> second_frame =
            websocket::EncodeFrame(Opcode::kBinary, second_payload, std::nullopt);

    // Append the first frame (804 bytes, allocating a 1206-byte ring capacity)
    // plus the first 10 bytes of the second frame, then consume the first frame
    // so consume_ advances near the end of the ring buffer (offset 804).
    parser.Append(first_frame);
    parser.Append(std::span<const uint8_t>(second_frame).subspan(0, 10));
    auto first = parser.NextEvent();
    ASSERT_TRUE(first.ok()) << first.status();
    ASSERT_TRUE(first->has_value());
    EXPECT_EQ((*first)->payload, first_payload);

    // Appending the remaining 494 bytes of the second frame wraps around the
    // 1206-byte ring capacity without resizing.
    parser.Append(std::span<const uint8_t>(second_frame).subspan(10));
    auto second = parser.NextEvent();
    ASSERT_TRUE(second.ok()) << second.status();
    ASSERT_TRUE(second->has_value());
    EXPECT_EQ((*second)->payload, second_payload);
}

TEST(WebSocketPathTest, CarriesInitialInfoAsQueryParams) {
    TransportOptions options;
    options.device = {.name = "Pixel 9", .version = "36.1", .sdk_version = "36", .arch = "x86_64"};
    ChipConfig chip{.kind = ChipKind::kBluetooth, .address = "00:11:22:33:44:55"};
    EXPECT_EQ(WebSocketPath(chip, options),
              "/v1/websocket/bt?name=Pixel%209&device_kind=EMULATOR&version=36.1&sdk_version=36"
              "&arch=x86_64&address=00%3A11%3A22%3A33%3A44%3A55");
}

class FakeWebSocketPeer {
  public:
    FakeWebSocketPeer()
            : loop_(goldfish::async::ThreadedEventLoop::Create(
                      goldfish::async::LibuvEventLoop::Create("TestWsLoop")))
            , socket_(std::make_shared<
                      testing::NiceMock<goldfish::async::testing::FakeAsyncSocket>>()) {
        socket_->setEventLoop(loop_.get());

        ON_CALL(socket_factory_, CreateSocket(testing::_, testing::_))
                .WillByDefault(testing::Return(socket_));
        ON_CALL(*socket_, Connected()).WillByDefault([this] {
            std::lock_guard<std::mutex> lock(mu_);
            return connected_;
        });
        ON_CALL(*socket_, Connect()).WillByDefault([this] {
            {
                std::lock_guard<std::mutex> lock(mu_);
                connected_ = true;
            }
            socket_->SimulateConnected(absl::OkStatus());
            return absl::OkStatus();
        });
        ON_CALL(*socket_, Close()).WillByDefault([this] {
            {
                std::lock_guard<std::mutex> lock(mu_);
                connected_ = false;
            }
            socket_->SimulateClose();
        });
        ON_CALL(*socket_, Send(testing::_, testing::_, testing::_))
                .WillByDefault([this](const char* buf, size_t size,
                                      goldfish::async::AsyncSocket::OnSendCallback on_send) {
                    return OnClientSend(std::string_view(buf, size), std::move(on_send));
                });
    }

    ~FakeWebSocketPeer() { loop_.reset(); }

    goldfish::async::EventLoop* loop() { return loop_.get(); }
    goldfish::async::AsyncSocketFactory* socket_factory() { return &socket_factory_; }

    TransportOptions Options() const {
        TransportOptions options;
        options.server_address = "127.0.0.1:8554";
        options.device.name = "Pixel 9";
        return options;
    }

    std::string HandshakeHead() const {
        std::lock_guard<std::mutex> lock(mu_);
        return handshake_head_;
    }

    void Send(Opcode opcode, const Packet& payload) {
        SendRaw(websocket::EncodeFrame(opcode, payload, std::nullopt));
    }

    void SendRaw(const std::vector<uint8_t>& bytes) {
        socket_->SimulateRead(
                std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    }

    std::optional<Frame> ReadFrame(std::chrono::milliseconds timeout = 5s) {
        std::unique_lock<std::mutex> lock(mu_);
        if (!cv_.wait_for(lock, timeout, [this] { return !frames_.empty(); })) {
            return std::nullopt;
        }
        Frame frame = std::move(frames_.front());
        frames_.pop_front();
        return frame;
    }

  private:
    absl::Status OnClientSend(std::string_view data,
                              goldfish::async::AsyncSocket::OnSendCallback on_send) {
        std::optional<std::string> handshake_reply;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (!handshake_done_) {
                handshake_head_.append(data.data(), data.size());
                if (handshake_head_.find("\r\n\r\n") != std::string::npos) {
                    handshake_done_ = true;
                    constexpr std::string_view kKeyHeader = "Sec-WebSocket-Key: ";
                    std::string key;
                    if (size_t pos = handshake_head_.find(kKeyHeader); pos != std::string::npos) {
                        pos += kKeyHeader.size();
                        key = handshake_head_.substr(pos, handshake_head_.find("\r\n", pos) - pos);
                    }
                    handshake_reply = absl::StrCat(
                            "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                            "Connection: Upgrade\r\nSec-WebSocket-Accept: ",
                            websocket::AcceptKey(key), "\r\n\r\n");
                }
            } else {
                const auto* bytes = reinterpret_cast<const uint8_t*>(data.data());
                raw_sent_.insert(raw_sent_.end(), bytes, bytes + data.size());
                while (true) {
                    size_t consumed = 0;
                    auto decoded = websocket::DecodeFrame(raw_sent_, &consumed);
                    if (!decoded.ok() || !decoded->has_value()) break;
                    raw_sent_.erase(raw_sent_.begin(), raw_sent_.begin() + consumed);
                    frames_.push_back(**std::move(decoded));
                    cv_.notify_all();
                }
            }
        }
        if (on_send) {
            loop_->Post([cb = std::move(on_send)]() mutable {
                     cb(absl::OkStatus());
                 }).IgnoreError();
        }
        if (handshake_reply) {
            socket_->SimulateRead(*handshake_reply);
        }
        return absl::OkStatus();
    }

    std::unique_ptr<goldfish::async::ThreadedEventLoop> loop_;
    testing::NiceMock<goldfish::async::testing::MockAsyncSocketFactory> socket_factory_;
    std::shared_ptr<testing::NiceMock<goldfish::async::testing::FakeAsyncSocket>> socket_;

    mutable std::mutex mu_;
    std::condition_variable cv_;
    bool connected_ = false;
    bool handshake_done_ = false;
    std::string handshake_head_;
    std::vector<uint8_t> raw_sent_;
    std::deque<Frame> frames_;
};

TEST(WebSocketTransportTest, SimProfileAndNullLoopValidation) {
    FakeWebSocketPeer peer;
    PacketQueue received;

    auto null_loop_transport =
            CreateWebSocketTransport(nullptr, peer.Options(), received.Callback());
    EXPECT_EQ(null_loop_transport->initialize({.kind = ChipKind::kBluetooth}).code(),
              absl::StatusCode::kInvalidArgument);

    auto transport = CreateWebSocketTransport(peer.loop(), peer.Options(), received.Callback(),
                                              peer.socket_factory());
    EXPECT_EQ(transport->initialize({.kind = ChipKind::kCellular, .sim_profile = "<xml/>"}).code(),
              absl::StatusCode::kUnimplemented);

    absl::Notification done;
    absl::Status loop_init_status;
    ASSERT_TRUE(peer.loop()
                        ->Post([&] {
                            loop_init_status =
                                    transport->initialize({.kind = ChipKind::kBluetooth});
                            done.Notify();
                        })
                        .ok());
    done.WaitForNotification();
    EXPECT_EQ(loop_init_status.code(), absl::StatusCode::kFailedPrecondition);
}

TEST(WebSocketTransportTest, HandshakeAndBidirectionalStreaming) {
    FakeWebSocketPeer peer;
    PacketQueue received;
    auto transport = CreateWebSocketTransport(peer.loop(), peer.Options(), received.Callback(),
                                              peer.socket_factory());
    ASSERT_TRUE(transport->initialize({.kind = ChipKind::kBluetooth}).ok());

    std::string head = peer.HandshakeHead();
    EXPECT_EQ(
            head.rfind("GET /v1/websocket/bt?name=Pixel%209&device_kind=EMULATOR HTTP/1.1\r\n", 0),
            0u)
            << head;

    transport->send({0x01, 0x03, 0x0c, 0x00});
    auto frame = peer.ReadFrame();
    ASSERT_TRUE(frame);
    EXPECT_TRUE(frame->masked);
    EXPECT_EQ(frame->opcode, Opcode::kBinary);
    EXPECT_EQ(frame->payload, (Packet{0x01, 0x03, 0x0c, 0x00}));

    peer.Send(Opcode::kBinary, {0x04, 0x0e});
    peer.Send(Opcode::kText, {'h', 'i'});
    peer.Send(Opcode::kBinary, Packet(70000, 0xAB));
    EXPECT_EQ(received.Pop(), (Packet{0x04, 0x0e}));
    EXPECT_EQ(received.Pop(), Packet(70000, 0xAB));
}

TEST(WebSocketTransportTest, ReassemblesFragmentsAndAnswersPing) {
    FakeWebSocketPeer peer;
    PacketQueue received;
    auto transport = CreateWebSocketTransport(peer.loop(), peer.Options(), received.Callback(),
                                              peer.socket_factory());
    ASSERT_TRUE(transport->initialize({.kind = ChipKind::kBluetooth}).ok());

    peer.SendRaw(
            websocket::EncodeFrame(Opcode::kBinary, Packet{1, 2}, std::nullopt, /*fin=*/false));
    peer.SendRaw(websocket::EncodeFrame(Opcode::kPing, Packet{9}, std::nullopt));
    peer.SendRaw(websocket::EncodeFrame(Opcode::kContinuation, Packet{3}, std::nullopt));
    EXPECT_EQ(received.Pop(), (Packet{1, 2, 3}));
    auto pong = peer.ReadFrame();
    ASSERT_TRUE(pong);
    EXPECT_EQ(pong->opcode, Opcode::kPong);
    EXPECT_EQ(pong->payload, (Packet{9}));
}

TEST(WebSocketTransportTest, PausesUntilNextRecv) {
    FakeWebSocketPeer peer;
    PacketQueue received;
    auto transport = CreateWebSocketTransport(
            peer.loop(), peer.Options(), received.Callback(/*more=*/false), peer.socket_factory());
    ASSERT_TRUE(transport->initialize({.kind = ChipKind::kBluetooth}).ok());

    peer.Send(Opcode::kBinary, {1});
    peer.Send(Opcode::kBinary, {2});
    EXPECT_EQ(received.Pop(), Packet{1});
    EXPECT_EQ(received.Pop(200ms), std::nullopt);
    transport->next_recv();
    EXPECT_EQ(received.Pop(), Packet{2});
}

TEST(WebSocketTransportTest, EchoesServerCloseAndSendsClientCloseOnDestruct) {
    FakeWebSocketPeer peer;
    PacketQueue received;
    auto transport = CreateWebSocketTransport(peer.loop(), peer.Options(), received.Callback(),
                                              peer.socket_factory());
    ASSERT_TRUE(transport->initialize({.kind = ChipKind::kBluetooth}).ok());

    transport.reset();
    auto frame = peer.ReadFrame();
    ASSERT_TRUE(frame);
    EXPECT_EQ(frame->opcode, Opcode::kClose);
}

}  // namespace
}  // namespace goldfish::netsim
