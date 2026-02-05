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

#include "absl/synchronization/mutex.h"

#include "goldfish/base/intrusive_ptr.h"

extern "C" {
#include "pixman.h"
}

namespace goldfish::display::details {
// Protexcts the reference counting of pixman images, which is not thread safe.
inline absl::Mutex g_pixman_mutex;
}  // namespace goldfish::display::details

inline void intrusive_ptr_add_ref(pixman_image_t* p) {
    // Note:
    // - the pixman display interface is slated to be replaced with gfxstream,
    // so this is a temporary workaround to improve thread-safety without
    // a major refactor.
    // - we do not have to many threads contending for pixman image refs
    //.   - We have a display producer (no multi threading yet)
    //    - We have a display consumer (embedded/fishtank)
    //.   - Concurrent unit tests (those have many threads)
    absl::MutexLock lock(goldfish::display::details::g_pixman_mutex);
    pixman_image_ref(p);
}

inline void intrusive_ptr_release(pixman_image_t* p) {
    absl::MutexLock lock(goldfish::display::details::g_pixman_mutex);
    pixman_image_unref(p);
}

inline void intrusive_ptr_ctor(pixman_image_t*) {
    // do nothing, the counter initialized to 1
}

namespace goldfish::display {

using PixmanImagePtr = ::goldfish::base::IntrusivePtr<::pixman_image_t>;

}  // namespace goldfish::display
