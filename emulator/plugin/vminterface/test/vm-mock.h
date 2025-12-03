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

#include <stdbool.h>
#include <stdint.h>
#include <sys/cdefs.h>

__BEGIN_DECLS

// clang-format off
// IWYU pragma: begin_keep
// Include the QEMU headers for RunState and ShutdownCause
#include "qemu/osdep.h"
#include "system/runstate.h"
// IWYU pragma: end_keep
// clang-format on

// Getter and setter for RunState
RunState runstate_get();
void mock_runstate_set(RunState state);

// Getter and setter for ShutdownCause
ShutdownCause mock_shutdown_cause_get();
void mock_shutdown_cause_set(ShutdownCause cause);

// Getter and setter for iothread_locked
bool mock_iothread_locked_get();
void mock_iothread_locked_set(bool locked);

__END_DECLS
