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
#include "goldfish/base/IntrusivePtr.h"

extern "C" {
#include "pixman.h"
}

inline void intrusive_ptr_add_ref(pixman_image_t* p) {
    pixman_image_ref(p);
}

inline void intrusive_ptr_release(pixman_image_t* p) {
    pixman_image_unref(p);
}

inline void intrusive_ptr_ctor(pixman_image_t*) {
    // do nothing, the counter initialized to 1
}

namespace goldfish::display {

using PixmanImagePtr = ::goldfish::base::IntrusivePtr<::pixman_image_t>;

}  // namespace goldfish::display
