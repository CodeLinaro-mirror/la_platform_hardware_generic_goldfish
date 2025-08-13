/* Copyright 2025 The Android Open Source Project
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

#include "absl/log/log.h"
#include "goldfish/gvk/util/GetPFN.h"

namespace goldfish::gvk::util {

template <typename PFN>
bool initPFN(PFN& dst, const GetPFN& getPFN, const char* name, const char* from) {
  dst = reinterpret_cast<PFN>(getPFN(name));
  if (dst) {
    return true;
  } else {
    LOG(ERROR) << "Could not load `" << name << "` from `" << from << "`.";
    return false;
  }
}

}  // namespace goldfish::gvk::util
