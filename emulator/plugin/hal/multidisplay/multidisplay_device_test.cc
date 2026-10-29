// Copyright (C) 2026 The Android Open Source Project
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

#include "goldfish/devices/multidisplay/multidisplay_device.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "absl/synchronization/notification.h"

#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/devices/test_connector_registry.h"

namespace {
std::atomic<const std::function<void()>*> g_qom_lookup_hook{nullptr};
}  // namespace

// Minimal QOM stubs for virtio_gpu_glue.c so unit tests can verify which event
// loop thread invokes virtioGpuGlueGetInstance() without linking QEMU.
extern "C" {
struct Object;

// NOLINTNEXTLINE(readability-identifier-naming): QEMU C ABI stub
Object* object_resolve_path_type(const char* /*path*/, const char* /*type_name*/,
                                 bool* /*ambiguous*/) {
    if (const auto* hook = g_qom_lookup_hook.load(std::memory_order_acquire)) {
        (*hook)();
    }
    return nullptr;
}

// NOLINTNEXTLINE(readability-identifier-naming): QEMU C ABI stub
Object* object_dynamic_cast_assert(Object* obj, const char* /*type_name*/, const char* /*file*/,
                                   int /*line*/, const char* /*func*/) {
    return obj;
}
}  // extern "C"

namespace goldfish::devices::multidisplay {
namespace {

using goldfish::async::LibuvEventLoop;
using goldfish::async::ThreadedEventLoop;
using goldfish::async::testing::TestEventLoop;

std::string MakeBindPacket(uint32_t display_id, uint32_t cb_handle) {
    const uint32_t payload_size = 1 + 2 * sizeof(uint32_t);
    std::string packet(sizeof(uint32_t) + payload_size, '\0');
    std::memcpy(packet.data(), &payload_size, sizeof(uint32_t));
    packet[sizeof(uint32_t)] = static_cast<char>(MultiDisplayDevice::kCmdBind);
    std::memcpy(packet.data() + sizeof(uint32_t) + 1, &display_id, sizeof(uint32_t));
    std::memcpy(packet.data() + sizeof(uint32_t) + 1 + sizeof(uint32_t), &cb_handle,
                sizeof(uint32_t));
    return packet;
}

class ScopedQomHook {
  public:
    explicit ScopedQomHook(std::function<void()> fn) : fn_(std::move(fn)) {
        g_qom_lookup_hook.store(&fn_, std::memory_order_release);
    }
    ~ScopedQomHook() { g_qom_lookup_hook.store(nullptr, std::memory_order_release); }

  private:
    std::function<void()> fn_;
};

TEST(MultiDisplayDeviceTest, BindDispatchesVirtioGpuReadbackOntoQemuLoopNotClientLoop) {
    auto client_loop = TestEventLoop::Create("client");
    auto qemu_loop = TestEventLoop::Create("qemu");
    TestConnectorRegistry registry;

    auto device = std::make_shared<MultiDisplayDevice>(client_loop.get());
    MultiDisplayDevice::RegisterDevice(device, &registry, client_loop.get(), qemu_loop.get());
    auto* constructed = registry.ConstructHalDevice<MultiDisplayDevice>();
    ASSERT_EQ(constructed, device.get());

    std::atomic<int> qom_calls{0};
    std::atomic<bool> called_on_qemu_loop{false};
    std::atomic<bool> called_on_client_loop{false};
    ScopedQomHook hook([&]() {
        qom_calls.fetch_add(1, std::memory_order_acq_rel);
        if (qemu_loop->IsOnLoopThread()) {
            called_on_qemu_loop.store(true, std::memory_order_release);
        }
        if (client_loop->IsOnLoopThread()) {
            called_on_client_loop.store(true, std::memory_order_release);
        }
    });

    device->SendAddDisplay(/*display_id=*/1, /*width=*/8, /*height=*/8, /*dpi=*/160, /*flag=*/0);
    client_loop->RunAll();

    // Deliver kCmdBind on client_loop: handle updates immediately, but QOM/GPU
    // readback must be queued onto qemu_loop rather than executed on client_loop.
    ASSERT_TRUE(client_loop->Post([&]() { device->OnReceive(MakeBindPacket(1, 42)); }).ok());
    client_loop->RunAll();

    EXPECT_EQ(device->GetDisplayColorBuffer(1), 42U);
    EXPECT_EQ(qom_calls.load(std::memory_order_acquire), 0);
    EXPECT_EQ(qemu_loop->TaskCount(), 1U);

    qemu_loop->RunAll();
    EXPECT_GT(qom_calls.load(std::memory_order_acquire), 0);
    EXPECT_TRUE(called_on_qemu_loop.load(std::memory_order_acquire));
    EXPECT_FALSE(called_on_client_loop.load(std::memory_order_acquire));

    registry.Close();
    client_loop->RunAll();
}

TEST(MultiDisplayDeviceTest, TeardownBeforeQemuLoopDrainsDoesNotUseAfterFree) {
    auto client_loop = TestEventLoop::Create("client");
    auto qemu_loop = TestEventLoop::Create("qemu");
    auto registry = std::make_unique<TestConnectorRegistry>();

    auto device = std::make_shared<MultiDisplayDevice>(client_loop.get(), qemu_loop.get());
    std::weak_ptr<MultiDisplayDevice> weak_device = device;
    MultiDisplayDevice::RegisterDevice(device, registry.get(), client_loop.get(), qemu_loop.get());
    (void)registry->ConstructHalDevice<MultiDisplayDevice>();

    std::atomic<int> qom_calls{0};
    ScopedQomHook hook([&]() { qom_calls.fetch_add(1, std::memory_order_acq_rel); });

    device->SendAddDisplay(/*display_id=*/2, /*width=*/4, /*height=*/4, /*dpi=*/160, /*flag=*/0);
    client_loop->RunAll();
    device->OnReceive(MakeBindPacket(2, 99));
    ASSERT_EQ(qemu_loop->TaskCount(), 1U);

    // Destroy MultiDisplayDevice while the readback callback is still pending on qemu_loop.
    registry->Close();
    client_loop->RunAll();
    registry.reset();
    device.reset();
    ASSERT_TRUE(weak_device.expired());

    qemu_loop->RunAll();
    EXPECT_EQ(qom_calls.load(std::memory_order_acquire), 0);
}

TEST(MultiDisplayDeviceTest, RebindErasesUnreferencedFramebuffer) {
    auto client_loop = TestEventLoop::Create("client");
    auto qemu_loop = TestEventLoop::Create("qemu");
    TestConnectorRegistry registry;

    auto device = std::make_shared<MultiDisplayDevice>(client_loop.get(), qemu_loop.get());
    MultiDisplayDevice::RegisterDevice(device, &registry, client_loop.get(), qemu_loop.get());
    (void)registry.ConstructHalDevice<MultiDisplayDevice>();

    device->SendAddDisplay(/*display_id=*/1, /*width=*/2, /*height=*/2, /*dpi=*/160, /*flag=*/0);
    client_loop->RunAll();

    device->OnReceive(MakeBindPacket(1, 10));
    EXPECT_EQ(device->GetDisplayColorBuffer(1), 10U);

    device->OnReceive(MakeBindPacket(1, 20));
    EXPECT_EQ(device->GetDisplayColorBuffer(1), 20U);

    // Handle 10 was unreferenced on rebind, so ReadDisplayColorBuffer leaves outptr untouched.
    std::vector<uint8_t> canary(size_t{2} * 2 * 4, 0xAB);
    device->ReadDisplayColorBuffer(10, canary.data());
    EXPECT_EQ(canary[0], 0xAB);

    // Handle 20 exists and zeroes outptr.
    device->ReadDisplayColorBuffer(20, canary.data());
    EXPECT_EQ(canary[0], 0x00);

    qemu_loop->RunAll();
    registry.Close();
    client_loop->RunAll();
}

TEST(MultiDisplayDeviceTest, ConcurrentBindDeleteAndReadAcrossThreadsIsRaceFree) {
    auto client_loop = ThreadedEventLoop::Create(LibuvEventLoop::Create("client_thread"));
    auto qemu_loop = ThreadedEventLoop::Create(LibuvEventLoop::Create("qemu_thread"));
    ASSERT_NE(client_loop, nullptr);
    ASSERT_NE(qemu_loop, nullptr);
    TestConnectorRegistry registry;

    auto device = std::make_shared<MultiDisplayDevice>(client_loop.get(), qemu_loop.get());
    MultiDisplayDevice::RegisterDevice(device, &registry, client_loop.get(), qemu_loop.get());
    (void)registry.ConstructHalDevice<MultiDisplayDevice>();

    std::atomic<bool> saw_off_qemu_thread{false};
    ScopedQomHook hook([&]() {
        if (!qemu_loop->IsOnLoopThread()) {
            saw_off_qemu_thread.store(true, std::memory_order_release);
        }
    });

    std::atomic<bool> stop_reader{false};
    std::thread reader([&]() {
        std::vector<uint8_t> buf(size_t{4} * 4 * 4, 0);
        while (!stop_reader.load(std::memory_order_acquire)) {
            uint32_t cb = device->GetDisplayColorBuffer(1);
            if (cb != 0) {
                device->ReadDisplayColorBuffer(cb, buf.data());
            }
            std::this_thread::yield();
        }
    });

    constexpr int kIterations = 64;
    for (int i = 1; i <= kIterations; ++i) {
        absl::Notification step_done;
        ASSERT_TRUE(client_loop
                            ->Post([&, i]() {
                                device->SendAddDisplay(1, 4, 4, 160, 0);
                                device->OnReceive(MakeBindPacket(1, static_cast<uint32_t>(i)));
                                if (i % 4 == 0) {
                                    device->SendDelDisplay(1);
                                }
                                step_done.Notify();
                            })
                            .ok());
        step_done.WaitForNotification();
    }

    absl::Notification qemu_drained;
    ASSERT_TRUE(qemu_loop->Post([&]() { qemu_drained.Notify(); }).ok());
    qemu_drained.WaitForNotification();

    stop_reader.store(true, std::memory_order_release);
    reader.join();

    EXPECT_FALSE(saw_off_qemu_thread.load(std::memory_order_acquire));

    absl::Notification closed;
    ASSERT_TRUE(client_loop
                        ->Post([&]() {
                            registry.Close();
                            closed.Notify();
                        })
                        .ok());
    closed.WaitForNotification();
}

}  // namespace
}  // namespace goldfish::devices::multidisplay
