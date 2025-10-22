
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

#include "emulator_controller.grpc.pb.h"
#include "goldfish/async/event_loop.h"

// Necessary on Windows.
#undef send

extern "C" {
typedef struct QemuConsole QemuConsole;
}

namespace android {
namespace emulation {
namespace control {
namespace keyboard {

using ::goldfish::async::EventLoop;

/**
 * @class IKeyEventSender
 * @brief An interface for sending keyboard events to the emulator.
 *
 * This interface provides a way to send various types of keyboard events,
 * including key presses, key releases, and text input, to the emulator's
 * input system. Implementations of this interface handle the translation
 * of high-level keyboard events into the low-level events required by the
 * emulator.
 *
 * @see KeyEventSenderImpl
 */
class IKeyEventSender {
  public:
    /**
     * @brief Virtual destructor for the IKeyEventSender interface.
     *
     * Ensures proper cleanup of derived classes.
     */
    virtual ~IKeyEventSender() = default;

    /**
     * @brief Sends a keyboard event to the emulator.
     *
     * This method takes a KeyboardEvent protobuf message, which encapsulates
     * the details of the keyboard event, and dispatches it to the emulator.
     * The implementation is responsible for interpreting the event and
     * sending the appropriate low-level commands to the emulator.
     *
     * @param request The KeyboardEvent protobuf message describing the event.
     * @note This is thread safe, the actual events will be posted on a qemuLoop

     * @see KeyboardEvent
     * @see KeyEventSenderImpl::doSend
     */
    virtual void send(const KeyboardEvent request) = 0;
};

/**
 * @brief Creates a keyboard event sender that sends events to the given console.
 *
 * This function creates an instance of a concrete IKeyEventSender implementation
 * (KeyEventSenderImpl) that is capable of sending keyboard events to the
 * specified QemuConsole.
 *
 * @param console A pointer to the QemuConsole to which events will be sent.
 * @param qemuLoop A pointer to the Qemu Event loop used to post actual events.
 * @return A unique pointer to the created IKeyEventSender instance.
 *
 * @see IKeyEventSender
 * @see KeyEventSenderImpl
 * @see QemuConsole
 */
std::unique_ptr<IKeyEventSender> createKeyEventSender(QemuConsole* console, EventLoop* qemuLoop);

}  // namespace keyboard
}  // namespace control
}  // namespace emulation
}  // namespace android
