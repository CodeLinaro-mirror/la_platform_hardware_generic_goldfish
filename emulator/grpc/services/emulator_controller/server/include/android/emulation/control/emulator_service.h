// Copyright (C) 2018 The Android Open Source Project
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

#include <grpc++/grpc++.h>

#include <memory>

#include "android/goldfish/vm_interface.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/avd_info/avd_info.h"
#include "goldfish/display/QemuMultidisplay/multi_display.h"

extern "C" {
struct QemuConsole;
}

namespace android {
namespace emulation {
namespace control {

std::shared_ptr<grpc::Service> getEmulatorController(
        android::goldfish::VmOperations* vmInterface, QemuConsole* keyboardConsole,
        ::goldfish::avd_info::AvdUniverse* avdUniverse,
        ::goldfish::display::IMultiDisplay* multiDisplay, ::goldfish::async::EventLoop* qemuLoop);

}  // namespace control
}  // namespace emulation
}  // namespace android
