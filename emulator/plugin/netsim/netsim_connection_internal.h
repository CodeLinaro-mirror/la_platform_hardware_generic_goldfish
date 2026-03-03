// Copyright 2026 The Android Open Source Project
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

#include "android/emulation/control/emulator_grpc_client.h"
#include "absl/status/status.h"

namespace goldfish::netsim {

absl::StatusOr<std::shared_ptr<android::emulation::control::EmulatorGrpcClientBase>> get_connected_netsim_grpc_client();

}  // namespace goldfish::netsim
