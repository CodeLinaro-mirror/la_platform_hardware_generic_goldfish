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
#include "goldfish/vsock/listen.h"

// This file provides a fake implementation of the vsock::listen function to
// satisfy the linker for unit tests that depend on ConnectorRegistry.
namespace goldfish::vsock {
bool listen(uint32_t port, HostPortListener listener) {
    // This is a fake for linking purposes and should not be called by these tests.
    // Returning true to indicate success.
    return true;
}
}  // namespace goldfish::vsock
