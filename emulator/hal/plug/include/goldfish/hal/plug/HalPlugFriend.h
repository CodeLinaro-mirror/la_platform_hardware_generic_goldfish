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
#pragma once

#include "goldfish/hal/plug/HalPlug.h"

namespace goldfish {
namespace devices {

/**
 * @class HalPlugFriend
 * @brief A framework-internal helper to access private members of HalPlug.
 *
 * This class uses the 'friend' mechanism to provide a controlled and explicit
 * way for the connection framework (specifically, the ConnectorRegistry) to
 * call the otherwise private `establishConnection` method on a HalPlug.
 *
 * This enforces at compile time that only authorized framework code can perform
 * this sensitive setup operation, preventing accidental misuse by HAL
 * implementers.
 */
class HalPlugFriend {
 public:
  static void establishConnection(HalPlug* plug, std::shared_ptr<HalSocket> socket) {
    // Because this class is a friend of HalPlug, it is allowed to call
    // the private establishConnection method.
    plug->establishConnection(std::move(socket));
  }
};

}  // namespace devices
}  // namespace goldfish
