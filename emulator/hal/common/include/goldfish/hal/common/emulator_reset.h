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

namespace goldfish::devices {

/**
 * @brief A struct to encapsulate the registration and unregistration of
 * emulator reset event callbacks.
 *
 * This struct provides a generic way for HAL devices to interact with the
 * QEMU reset mechanism without being directly coupled to QEMU's implementation
 * details. It holds function pointers for registering and unregistering a
 * reset handler.
 */
struct EmulatorResetCallbacks {
    /**
     * @brief A function pointer type for the reset handler.
     * @param opaque An opaque pointer passed during registration.
     */
    using QEMUResetHandler = void(void* opaque);
    /**
     * @brief A function pointer type for registering a reset handler.
     * @param func The reset handler function to register.
     * @param opaque An opaque pointer to be passed to the handler.
     */
    using RegisterEmulatorReset = void (*)(QEMUResetHandler* func, void* opaque);
    /**
     * @brief A function pointer type for unregistering a reset handler.
     * @param func The reset handler function to unregister.
     * @param opaque The opaque pointer that was used for registration.
     */
    using UnregisterEmulatorReset = void (*)(QEMUResetHandler* func, void* opaque);

    RegisterEmulatorReset do_register;
    UnregisterEmulatorReset do_unregister;
};

}  // namespace goldfish::devices
