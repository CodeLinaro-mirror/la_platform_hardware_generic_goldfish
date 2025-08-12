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
#include "goldfish/async/event_loop.h"

namespace goldfish::async {

// Gets the singleton instance of the QEMU-based event loop.
//
// This event loop is tied to the main QEMU thread. It does not support
// the run() method, as the QEMU loop is managed by the application's
// main function.
//
// Before using the event loop, you MUST call initializeQemuEventLoop()
// from the main QEMU thread.
EventLoop* getQemuEventLoop();

// Initializes the QEMU event loop.
// This function must be called from the main QEMU thread before any other
// thread attempts to use the event loop. It marks the calling thread as the
// event loop's thread.
void initializeQemuEventLoop();

}  // namespace goldfish::async
