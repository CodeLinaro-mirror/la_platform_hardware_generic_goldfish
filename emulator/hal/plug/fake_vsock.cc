/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "emulator/hal/plug/test/fake_vsock.h"

#include <utility>

#include "goldfish/vsock/connect.h"
#include "goldfish/vsock/listen.h"

// This file provides a fake implementation of the vsock::listen function to
// satisfy the linker for unit tests that depend on ConnectorRegistry.
namespace goldfish::vsock {
namespace {
FakeListenFn g_fake_listen_fn = [](uint32_t, const HostPortListener&) { return true; };
FakeConnectFn g_fake_connect_fn = [](uint32_t, const devices::cable::PlugPtr&) { return nullptr; };
}  // namespace

void SetFakeListenFn(FakeListenFn fn) {
    g_fake_listen_fn = std::move(fn);
}
void SetFakeConnectFn(FakeConnectFn fn) {
    g_fake_connect_fn = std::move(fn);
}

bool Listen(uint32_t port, HostPortListener listener) {
    return g_fake_listen_fn(port, std::move(listener));
}

devices::cable::SocketPtr Connect(uint32_t guest_port, devices::cable::PlugPtr plug) {
    return g_fake_connect_fn(guest_port, std::move(plug));
}

}  // namespace goldfish::vsock
