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

#include <string_view>

#include "goldfish/async/event_loop.h"
#include "goldfish/devices/connector_registry.h"

namespace goldfish::devices::unix_pipe {

using goldfish::async::EventLoop;
using namespace std::string_view_literals;

/**
 * A device which connects a guest vsock (5002 port) to a AF_UNIX socket
 * (specified by "pipe:unix:$path\0") on the host.
 *
 * It is used in a g3 environment.
 */
class IUnixPipe : public HalPlug {
  public:
    static constexpr std::string_view serviceName = "unix"sv;

    static void RegisterDevice(IConnectorRegistry* registry, EventLoop* client_loop,
                               EventLoop* qemu_loop);
};

}  // namespace goldfish::devices::unix_pipe
