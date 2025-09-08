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
 * @brief A framework-internal helper to access private members of HalPlug.
 *
 * Used by our test framework.
 */
class HalPlugTesting {
  public:
    static void establishConnection(HalPlug* plug, std::shared_ptr<HalSocket> socket) {
        // Because this class is a friend of HalPlug, it is allowed to call
        // the private establishConnection method.
        plug->establishConnection(std::move(socket));
    }
};

}  // namespace devices
}  // namespace goldfish
