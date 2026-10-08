// Copyright 2026 The Android Open Source Project
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

/**
 * @file vsock_perf.cc
 * @brief Google Benchmark suite for GoldfishVirtioVsockDevice and the
 *        HalPlug / MarshallingHalSocket / HalPlugToIPlugAdapter ADB bridge.
 *
 * Baseline Results (--config=release):
 * 1. Direct Host -> Guest (BM_VsockHostToGuestDirect):
 *    - 4 KiB:   115 ns   (33.1 GiB/s)
 *    - 16 KiB:  304 ns   (50.3 GiB/s)
 *    - 64 KiB:  2.23 us  (27.4 GiB/s)
 *    - 1 MiB:   75.9 us  (12.9 GiB/s)
 * 2. Multi-Stream O(streams) Scan Overhead (BM_VsockHostToGuestStreamScaling, 16 KiB):
 *    - 0 idle streams:   366 ns   (41.7 GiB/s)
 *    - 32 idle streams:  444 ns   (34.3 GiB/s)
 *    - 128 idle streams: 1,528 ns ( 9.99 GiB/s) -- 4.2x slowdown from std::set scan
 * 3. Direct Guest -> Host (BM_VsockGuestToHostDirect, 4 KiB iovec pages):
 *    - 4 KiB:   46.1 ns  ( 82.8 GiB/s)
 *    - 64 KiB:  393 ns   (155.3 GiB/s)
 *    - 1 MiB:   6.08 us  (160.6 GiB/s)
 * 4. End-to-End Guest -> Host via HalPlugToIPlugAdapter (adb pull path):
 *    - 4 KiB:   12.6 us  (310 MiB/s)
 *    - 64 KiB:  34.5 us  (1.77 GiB/s)
 *    - 1 MiB:   1,026 us (974 MiB/s) -- 3.5x slower than Host -> Guest at 1 MiB
 *      because HalPlugToIPlugAdapter::OnReceive allocates a std::string and
 *      calls client_loop_->Post() for every 4 KiB virtqueue iovec page (256
 *      separate tasks per 1 MiB packet) without write-buffer coalescing.
 * 5. End-to-End Host -> Guest via MarshallingHalSocket (adb push path):
 *    - 16 KiB:  19.0 us  (822 MiB/s)
 *    - 64 KiB:  32.1 us  (1.90 GiB/s)
 *    - 1 MiB:   292 us   (3.34 GiB/s)
 */

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"

#include "goldfish/adb/adb_message_logger.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/hal_plug_factory.h"
#include "goldfish/devices/internal/hal_plug.h"
#include "goldfish/vsock/clear.h"
#include "goldfish/vsock/connect.h"
#include "goldfish/vsock/vsock_low_level.h"

// clang-format off
// IWYU pragma: begin_keep
extern "C" {
#include "qemu/osdep.h"
#include "migration/qemu-file-types.h"
#include "standard-headers/linux/virtio_vsock.h"

size_t qemu_get_buffer(QEMUFile* /*f*/, uint8_t* /*buf*/, size_t /*size*/) {
    return 0;
}

void qemu_put_buffer(QEMUFile* /*f*/, const uint8_t* /*buf*/, size_t /*size*/) {}
}
// IWYU pragma: end_keep
// clang-format on

namespace goldfish::vsock {
namespace {

using goldfish::devices::HalPlug;
using goldfish::devices::HalPlugFactory;
using goldfish::devices::cable::IPlug;
using goldfish::devices::cable::ISocket;
using goldfish::devices::cable::PlugPtr;
using goldfish::devices::cable::SocketPtr;

constexpr uint64_t kGuestCid = 3;
constexpr uint64_t kHostCid = 2;
constexpr uint32_t kAdbGuestPort = 5555;
constexpr uint32_t kDefaultGuestBufAlloc = 16U << 20;  // 16 MiB window
constexpr size_t kVirtqueuePageSize = 4096;
constexpr size_t kLibuvReadChunkSize = 16384;

// Builds a synthetic ADB WRTE packet with a valid 24-byte AMessage header so
// AdbMessageLogger stays in sync across repeated benchmark iterations.
std::vector<char> MakeValidAdbPacket(size_t total_size) {
    constexpr uint32_t kAdbWrte = 0x45545257;  // "WRTE"
    std::vector<char> buf(std::max(total_size, sizeof(adb::AMessage)), 'P');
    const auto data_len = static_cast<uint32_t>(buf.size() - sizeof(adb::AMessage));
    adb::AMessage hdr = {
        .command = kAdbWrte,
        .arg0 = 1,
        .arg1 = 2,
        .data_length = data_len,
        .data_check = 0,
        .magic = kAdbWrte ^ 0xffffffffU,
    };
    std::memcpy(buf.data(), &hdr, sizeof(hdr));
    return buf;
}

// Simulates the QEMU VirtIOVSock device layer (vsock_low_level.c) without QEMU
// virtqueue overhead, draining host->guest packets in 4 KiB virtqueue segments
// and updating guest credit counters.
class FakeVirtioVsockBackend {
  public:
    FakeVirtioVsockBackend() {
        static const GoldfishVirtIOVSockDevAPI kApi = {
            .haveHostToGuestPackets = &FakeVirtioVsockBackend::OnHavePackets,
            .sendPacketHostToGuest = &FakeVirtioVsockBackend::OnSendPacket,
            .haveHostToGuestEvents = &FakeVirtioVsockBackend::OnHaveEvents,
            .sendEventHostToGuest = &FakeVirtioVsockBackend::OnSendEvent,
        };
        impl_ = goldfish_virtio_vsock_impl_realize(this, &kApi);
    }

    ~FakeVirtioVsockBackend() {
        goldfish::vsock::clear();
        goldfish_virtio_vsock_impl_unrealize(impl_);
    }

    void* impl() const { return impl_; }

    // Completes the VIRTIO_VSOCK_OP_REQUEST -> VIRTIO_VSOCK_OP_RESPONSE
    // handshake for the most recently requested host port.
    uint32_t AcceptLastConnectionRequest(uint32_t guest_port = kAdbGuestPort,
                                         uint32_t buf_alloc = kDefaultGuestBufAlloc) {
        const uint32_t host_port = last_requested_host_port_;
        struct virtio_vsock_hdr resp = {
            .src_cid = kGuestCid,
            .dst_cid = kHostCid,
            .src_port = guest_port,
            .dst_port = host_port,
            .len = 0,
            .type = VIRTIO_VSOCK_TYPE_STREAM,
            .op = VIRTIO_VSOCK_OP_RESPONSE,
            .flags = 0,
            .buf_alloc = buf_alloc,
            .fwd_cnt = 0,
        };
        goldfish_virtio_vsock_accept_guest_to_host_control(impl_, &resp);
        return host_port;
    }

    // Sends a VIRTIO_VSOCK_OP_CREDIT_UPDATE from the guest to keep the
    // sliding window open during long streaming benchmarks.
    void AcknowledgeGuestRx(uint32_t host_port, uint32_t guest_port = kAdbGuestPort,
                            uint32_t buf_alloc = kDefaultGuestBufAlloc) {
        struct virtio_vsock_hdr credit = {
            .src_cid = kGuestCid,
            .dst_cid = kHostCid,
            .src_port = guest_port,
            .dst_port = host_port,
            .len = 0,
            .type = VIRTIO_VSOCK_TYPE_STREAM,
            .op = VIRTIO_VSOCK_OP_CREDIT_UPDATE,
            .flags = 0,
            .buf_alloc = buf_alloc,
            .fwd_cnt = guest_rx_bytes_,
        };
        goldfish_virtio_vsock_accept_guest_to_host_control(impl_, &credit);
    }

    // Pushes a single credit-window batch of Guest -> Host RW packets (up to
    // available host receive credit, sliced into <= 64 KiB Linux virtio-vsock
    // packets and 4 KiB virtqueue page iovecs), then runs
    // goldfish_virtio_vsock_handle_host_to_guest() at the end of the batch just
    // like virtio_vsock_handle_guest_to_host() in vsock_low_level.c.
    // Returns the number of payload bytes sent in this batch.
    size_t PushGuestToHostWindowBatch(uint32_t host_port, const char* data, size_t max_size,
                                      uint32_t guest_port = kAdbGuestPort) {
        constexpr size_t kLinuxMaxPktBufSize = 64 * 1024;
        const uint32_t in_flight = guest_tx_bytes_ - host_fwd_cnt_;
        const size_t avail_credit = (host_buf_alloc_ > in_flight)
                                            ? static_cast<size_t>(host_buf_alloc_ - in_flight)
                                            : 0;
        if (avail_credit == 0) {
            goldfish_virtio_vsock_handle_host_to_guest(impl_);
            return 0;
        }

        const size_t batch_size = std::min(max_size, avail_credit);
        size_t sent_in_batch = 0;
        while (sent_in_batch < batch_size) {
            const size_t pkt_size = std::min(kLinuxMaxPktBufSize, batch_size - sent_in_batch);
            struct virtio_vsock_hdr hdr = {
                .src_cid = kGuestCid,
                .dst_cid = kHostCid,
                .src_port = guest_port,
                .dst_port = host_port,
                .len = static_cast<uint32_t>(pkt_size),
                .type = VIRTIO_VSOCK_TYPE_STREAM,
                .op = VIRTIO_VSOCK_OP_RW,
                .flags = 0,
                .buf_alloc = kDefaultGuestBufAlloc,
                .fwd_cnt = guest_rx_bytes_,
            };

            void* stream = goldfish_virtio_vsock_accept_guest_to_host_rw_start(impl_, &hdr);
            if (!stream) {
                break;
            }

            int erase_stream = 0;
            size_t offset = 0;
            while (offset < pkt_size) {
                const size_t chunk = std::min(kVirtqueuePageSize, pkt_size - offset);
                erase_stream = goldfish_virtio_vsock_accept_guest_to_host_rw(
                        impl_, stream, data + sent_in_batch + offset, chunk);
                if (erase_stream) {
                    break;
                }
                offset += chunk;
            }
            goldfish_virtio_vsock_accept_guest_to_host_rw_end(impl_, stream, erase_stream);
            guest_tx_bytes_ += static_cast<uint32_t>(pkt_size);
            sent_in_batch += pkt_size;
        }

        goldfish_virtio_vsock_handle_host_to_guest(impl_);
        return sent_in_batch;
    }

    // Pushes a Guest -> Host RW transfer across one or more host credit windows.
    void PushGuestToHostRw(uint32_t host_port, const char* data, size_t size,
                           uint32_t guest_port = kAdbGuestPort) {
        size_t offset = 0;
        while (offset < size) {
            offset +=
                    PushGuestToHostWindowBatch(host_port, data + offset, size - offset, guest_port);
        }
    }

    uint32_t guest_rx_bytes() const { return guest_rx_bytes_; }
    uint64_t host_credit_updates() const { return host_credit_updates_; }

  private:
    static void OnHavePackets(void* /*dev*/) {}
    static void OnHaveEvents(void* /*dev*/) {}

    static VirtIOVSockSendResult OnSendPacket(void* dev, struct virtio_vsock_hdr* hdr,
                                              const void* data) {
        auto* self = static_cast<FakeVirtioVsockBackend*>(dev);
        self->host_buf_alloc_ = hdr->buf_alloc;
        self->host_fwd_cnt_ = hdr->fwd_cnt;
        if (hdr->op == VIRTIO_VSOCK_OP_REQUEST) {
            self->last_requested_host_port_ = hdr->src_port;
            return MakeVirtIOVSockSendResult(false, true, 0U);
        }
        if (hdr->op == VIRTIO_VSOCK_OP_CREDIT_UPDATE) {
            self->host_credit_updates_++;
            return MakeVirtIOVSockSendResult(false, true, 0U);
        }
        if (hdr->op == VIRTIO_VSOCK_OP_RW && hdr->len > 0) {
            // Simulate vsock_low_level.c copying into 4 KiB virtqueue elements.
            const auto* ptr = static_cast<const char*>(data);
            size_t remaining = hdr->len;
            while (remaining > 0) {
                const size_t chunk = std::min(kVirtqueuePageSize, remaining);
                std::memcpy(self->scratch_page_, ptr, chunk);
                benchmark::DoNotOptimize(self->scratch_page_);
                ptr += chunk;
                remaining -= chunk;
            }
            self->guest_rx_bytes_ += hdr->len;
            return MakeVirtIOVSockSendResult(false, true, hdr->len);
        }
        return MakeVirtIOVSockSendResult(false, true, 0U);
    }

    static VirtIOVSockSendResult OnSendEvent(void* /*dev*/,
                                             const struct virtio_vsock_event* /*event*/) {
        return MakeVirtIOVSockSendResult(false, true, 1U);
    }

    void* impl_ = nullptr;
    uint32_t last_requested_host_port_ = 0;
    uint32_t guest_rx_bytes_ = 0;
    uint32_t guest_tx_bytes_ = 0;
    uint32_t host_buf_alloc_ = 64 * 1024;
    uint32_t host_fwd_cnt_ = 0;
    uint64_t host_credit_updates_ = 0;
    char scratch_page_[kVirtqueuePageSize]{};
};

class NoopPlug : public IPlug {
  public:
    void OnConnect() override {}
    bool OnReceive(const void* data, size_t size) override {
        benchmark::DoNotOptimize(data);
        received_bytes_ += size;
        return true;
    }
    SocketPtr OnUnplug() override { return {}; }
    void AbslStringifyImpl(absl::FormatSink& s) const override { absl::Format(&s, "[NoopPlug]"); }

    size_t received_bytes() const { return received_bytes_; }

  private:
    size_t received_bytes_ = 0;
};

// 1. Direct Host -> Guest (SendAsync -> SocketBuffer -> VirtQueue copy).
void BM_VsockHostToGuestDirect(benchmark::State& state) {
    const auto payload_size = static_cast<size_t>(state.range(0));
    const bool enable_adb_sniffer = state.range(1) != 0;
    const std::vector<char> payload = MakeValidAdbPacket(payload_size);

    FakeVirtioVsockBackend backend;
    auto* plug = new NoopPlug();
    SocketPtr socket = Connect(kAdbGuestPort, PlugPtr(plug));
    if (enable_adb_sniffer) {
        socket->SetDataSniffer(std::make_unique<adb::AdbLogger>(5555, kAdbGuestPort, false));
    }
    const uint32_t host_port = backend.AcceptLastConnectionRequest();

    for (auto _ : state) {
        socket->SendAsync(payload.data(), payload.size());
        backend.AcknowledgeGuestRx(host_port);
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(payload.size()));
}

// 2. Host -> Guest scaling with N concurrent idle VSOCK streams in mStreams.
// Quantifies the O(streams) iteration inside GoldfishVirtioVsockDevice::sendPacketsLocked().
void BM_VsockHostToGuestStreamScaling(benchmark::State& state) {
    constexpr size_t kChunkSize = kLibuvReadChunkSize;
    const int num_idle_streams = static_cast<int>(state.range(0));
    const std::vector<char> payload(kChunkSize, 'S');

    FakeVirtioVsockBackend backend;
    std::vector<SocketPtr> idle_sockets;
    idle_sockets.reserve(num_idle_streams);
    for (int i = 0; i < num_idle_streams; ++i) {
        idle_sockets.push_back(Connect(kAdbGuestPort + 1 + i, PlugPtr(new NoopPlug())));
        backend.AcceptLastConnectionRequest(kAdbGuestPort + 1 + i);
    }

    SocketPtr active_socket = Connect(kAdbGuestPort, PlugPtr(new NoopPlug()));
    const uint32_t active_host_port = backend.AcceptLastConnectionRequest(kAdbGuestPort);

    for (auto _ : state) {
        active_socket->SendAsync(payload.data(), payload.size());
        backend.AcknowledgeGuestRx(active_host_port);
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(kChunkSize));
}

// 3. Direct Guest -> Host (4 KiB virtqueue iovec segments -> IPlug::OnReceive).
void BM_VsockGuestToHostDirect(benchmark::State& state) {
    const auto payload_size = static_cast<size_t>(state.range(0));
    const std::vector<char> payload(payload_size, 'G');

    FakeVirtioVsockBackend backend;
    auto* plug = new NoopPlug();
    SocketPtr socket = Connect(kAdbGuestPort, PlugPtr(plug));
    const uint32_t host_port = backend.AcceptLastConnectionRequest();

    const uint64_t start_credits = backend.host_credit_updates();
    for (auto _ : state) {
        backend.PushGuestToHostRw(host_port, payload.data(), payload.size());
    }

    state.counters["credit_updates"] =
            benchmark::Counter(static_cast<double>(backend.host_credit_updates() - start_credits),
                               benchmark::Counter::kAvgThreads, benchmark::Counter::OneK::kIs1000);
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(payload_size));
}

// Helper HalPlug for benchmarking the full HalPlugFactory + MarshallingHalSocket
// + HalPlugToIPlugAdapter pipeline used by vsock_port_fwd.cc (ADB forwarding).
class BenchmarkHalPlug : public HalPlug {
  public:
    BenchmarkHalPlug() = default;

    void OnConnect() override { connected_.Notify(); }

    void OnReceive(std::string_view data) override {
        absl::MutexLock lock(&mu_);
        received_bytes_ += data.size();
    }

    void OnClose() override {}

    void SendToGuest(std::string data) { Socket()->Send(std::move(data)); }

    void WaitForConnected() { connected_.WaitForNotification(); }

    void ResetAndAwaitBytes(size_t expected_bytes) {
        auto done = [this, expected_bytes]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
            return received_bytes_ >= expected_bytes;
        };
        absl::MutexLock lock(&mu_);
        mu_.Await(absl::Condition(&done));
        received_bytes_ = 0;
    }

  protected:
    void AbslStringifyImpl(absl::FormatSink& s) const override {
        absl::Format(&s, "[BenchmarkHalPlug]");
    }

  private:
    absl::Notification connected_;
    absl::Mutex mu_;
    size_t received_bytes_ ABSL_GUARDED_BY(mu_) = 0;
};

// 4. End-to-End Guest -> Host through HalPlugToIPlugAdapter (adb pull path).
// Each credit-window batch requires a guest_to_host_vq kick on qemu_loop
// followed by a VIRTIO_VSOCK_OP_CREDIT_UPDATE reply to unlock the next window.
void BM_VsockGuestToHostHalPlugAdapter(benchmark::State& state) {
    const auto payload_size = static_cast<size_t>(state.range(0));
    const std::vector<char> payload(payload_size, 'R');

    FakeVirtioVsockBackend backend;
    auto client_loop = async::ThreadedEventLoop::Create(async::LibuvEventLoop::Create());
    auto qemu_loop = async::ThreadedEventLoop::Create(async::LibuvEventLoop::Create());

    auto hal_plug = std::make_shared<BenchmarkHalPlug>();
    uint32_t host_port = 0;
    qemu_loop
            ->PostAndWait([&]() {
                HalPlugFactory::Connect(
                        kAdbGuestPort, [hal_plug]() { return hal_plug; }, client_loop.get(),
                        qemu_loop.get(), [](bool) {});
                host_port = backend.AcceptLastConnectionRequest();
            })
            .IgnoreError();
    hal_plug->WaitForConnected();

    const uint64_t start_credits = backend.host_credit_updates();
    for (auto _ : state) {
        size_t offset = 0;
        while (offset < payload.size()) {
            qemu_loop
                    ->PostAndWait([&]() {
                        offset += backend.PushGuestToHostWindowBatch(
                                host_port, payload.data() + offset, payload.size() - offset);
                    })
                    .IgnoreError();
        }
        hal_plug->ResetAndAwaitBytes(payload_size);
    }

    qemu_loop->PostAndWait([&]() { goldfish::vsock::clear(); }).IgnoreError();
    state.counters["credit_updates"] =
            benchmark::Counter(static_cast<double>(backend.host_credit_updates() - start_credits),
                               benchmark::Counter::kAvgThreads, benchmark::Counter::OneK::kIs1000);
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(payload_size));
}

// 5. End-to-End Host -> Guest through MarshallingHalSocket (adb push path).
// Simulates HostToGuestConnection sending 16 KiB LibuvSocket read chunks via
// Socket()->Send(std::string(data)) across to the QEMU loop and VsockStream.
void BM_VsockHostToGuestHalPlugMarshalling(benchmark::State& state) {
    const auto total_payload_size = static_cast<size_t>(state.range(0));
    const std::string chunk(kLibuvReadChunkSize, 'W');

    FakeVirtioVsockBackend backend;
    auto client_loop = async::ThreadedEventLoop::Create(async::LibuvEventLoop::Create());
    auto qemu_loop = async::ThreadedEventLoop::Create(async::LibuvEventLoop::Create());

    auto hal_plug = std::make_shared<BenchmarkHalPlug>();
    uint32_t host_port = 0;
    qemu_loop
            ->PostAndWait([&]() {
                HalPlugFactory::Connect(
                        kAdbGuestPort, [hal_plug]() { return hal_plug; }, client_loop.get(),
                        qemu_loop.get(), [](bool) {});
                host_port = backend.AcceptLastConnectionRequest();
            })
            .IgnoreError();
    hal_plug->WaitForConnected();

    for (auto _ : state) {
        size_t sent = 0;
        while (sent < total_payload_size) {
            const size_t n = std::min(kLibuvReadChunkSize, total_payload_size - sent);
            hal_plug->SendToGuest(std::string(chunk.data(), n));
            sent += n;
        }
        qemu_loop->PostAndWait([&]() { backend.AcknowledgeGuestRx(host_port); }).IgnoreError();
    }

    qemu_loop->PostAndWait([&]() { goldfish::vsock::clear(); }).IgnoreError();
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(total_payload_size));
}

BENCHMARK(BM_VsockHostToGuestDirect)
        ->Args({4 << 10, 0})
        ->Args({16 << 10, 0})
        ->Args({64 << 10, 0})
        ->Args({256 << 10, 0})
        ->Args({1 << 20, 0})
        ->Args({64 << 10, 1})
        ->Args({1 << 20, 1});

BENCHMARK(BM_VsockHostToGuestStreamScaling)->Arg(0)->Arg(8)->Arg(32)->Arg(128);

BENCHMARK(BM_VsockGuestToHostDirect)->RangeMultiplier(4)->Range(4 << 10, 1 << 20);

BENCHMARK(BM_VsockGuestToHostHalPlugAdapter)
        ->RangeMultiplier(4)
        ->Range(4 << 10, 1 << 20)
        ->UseRealTime();

BENCHMARK(BM_VsockHostToGuestHalPlugMarshalling)
        ->RangeMultiplier(4)
        ->Range(16 << 10, 1 << 20)
        ->UseRealTime();

}  // namespace
}  // namespace goldfish::vsock

BENCHMARK_MAIN();
