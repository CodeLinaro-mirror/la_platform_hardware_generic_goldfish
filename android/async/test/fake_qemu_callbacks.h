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

#include <cstdint>

// This file provides declarations for the fake QEMU main loop functions
// implemented in fake_qemu_callbacks.cpp. These functions are used for
// unit testing of the QemuEventLoop.

extern "C" {
// Advances the fake clock and processes any events that occurred during that
// time.
void fake_qemu_advance_ms(int64_t ms);

// Resets all fake QEMU state to its initial values.
void fake_qemu_reset();

// Starts the fake I/O loop in a background thread.
void fake_qemu_start_io_loop();

// Stops the fake I/O loop.
void fake_qemu_stop_io_loop();
}
