// Copyright (C) 2025 The Android Open Source Project
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
#include <memory>

#include "goldfish/async/event_loop.h"

namespace goldfish::async {

/**
 * @brief An implementation of the EventLoop interface that integrates with the
 * existing QEMU main event loop.
 */
class QemuEventLoop : public EventLoop {
  public:
    using EventLoop::EventLoop;
    /**
     * @brief Creates an instance of the QEMU-based event loop.
     *
     * This event loop is tied to the main QEMU thread. It does not support
     * the run() method, as the QEMU loop is managed by the application's
     * main function.
     *
     * @note All created instances of this class will post tasks to the same
     * underlying QEMU event loop, so there is usually no need to create more
     * than one.
     *
     * @return A std::unique_ptr to a new QemuEventLoop instance.
     */
    static std::unique_ptr<QemuEventLoop> Create(std::string name = "QemuMainLoop");
};

}  // namespace goldfish::async
