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

#include <cstddef>
#include <functional>
#include <utility>

/**
 * @brief Provides a generic intrusive smart pointer, IntrusivePtr<T>.
 *
 * To use this class with a type `T`, you must provide two free functions
 * in the same namespace as `T` (or in the global namespace) so they can be
 * found by Argument-Dependent Lookup (ADL):
 *
 * @code
 * void intrusive_ptr_add_ref(T* p);
 * void intrusive_ptr_release(T* p);
 * @endcode
 *
 * These functions are responsible for incrementing and decrementing the
 * object's internal reference count.
 *
 * @par Example Usage with a C library (e.g., Pixman)
 *
 * Here's how to use `IntrusivePtr` with a C type like `pixman_image_t`, which
 * uses `pixman_image_ref` and `pixman_image_unref` for reference counting.
 *
 * @code
 * #include <pixman.h>
 * #include "goldfish/base/IntrusivePtr.h"
 *
 * // Define the required functions in the global namespace for pixman_image_t.
 * inline void intrusive_ptr_add_ref(pixman_image_t* p) {
 *     pixman_image_ref(p);
 * }
 *
 * inline void intrusive_ptr_release(pixman_image_t* p) {
 *     pixman_image_unref(p);
 * }
 *
 * inline void intrusive_ptr_ctor(pixman_image_t*) {
 *      // do nothing, the counter initialized to 1
 * }
 *
 * // Now you can use IntrusivePtr to manage pixman_image_t objects.
 * using PixmanImagePtr = goldfish::base::IntrusivePtr<pixman_image_t>;
 *
 * void process_image(pixman_image_t* image_raw) {
 *     // The raw pointer is now managed by the smart pointer.
 *     // The reference count is incremented upon construction.
 *     PixmanImagePtr image(image_raw);
 *
 *     // Use the smart pointer like a regular pointer.
 *     int width = pixman_image_get_width(image.get());
 *
 *     // The image is automatically released when 'image' goes out of scope,
 *     // decrementing the reference count.
 * }
 * @endcode
 */

namespace goldfish::base {

/**
 * @brief A smart pointer for objects with internal reference counting.
 *
 * IntrusivePtr is a smart pointer that assumes the managed object `T`
 * provides its own reference counting mechanism. This is more efficient
 * than std::shared_ptr as it avoids a separate memory allocation for a
 * control block.
 *
 * This class provides RAII semantics, ensuring that the reference count is
 * properly managed throughout the object's lifetime.
 *
 * @note The IntrusivePtr object itself requires synchronization if shared between threads, even if
 * the managed type is thread-safe.
 * @tparam T The type of the object to manage.
 */
template <class T>
class IntrusivePtr {
  public:
    using element_type = T;

    constexpr IntrusivePtr() noexcept : mPtr(nullptr) {}

    explicit IntrusivePtr(T* p) noexcept : mPtr(p) {
        if (mPtr) {
            intrusive_ptr_ctor(mPtr);
        }
    }

    IntrusivePtr(const IntrusivePtr& other) noexcept : mPtr(other.mPtr) {
        if (mPtr) {
            intrusive_ptr_add_ref(mPtr);
        }
    }

    IntrusivePtr(IntrusivePtr&& other) noexcept : mPtr(std::exchange(other.mPtr, nullptr)) {}

    ~IntrusivePtr() {
        if (mPtr) {
            intrusive_ptr_release(mPtr);
        }
    }

    IntrusivePtr& operator=(const IntrusivePtr& other) noexcept {
        IntrusivePtr(other).swap(*this);
        return *this;
    }

    IntrusivePtr& operator=(IntrusivePtr&& other) noexcept {
        IntrusivePtr(std::move(other)).swap(*this);
        return *this;
    }

    void reset() noexcept {
        if (mPtr) {
            intrusive_ptr_release(mPtr);
            mPtr = nullptr;
        }
    }

    void swap(IntrusivePtr& other) noexcept { std::swap(mPtr, other.mPtr); }

    T* get() const noexcept { return mPtr; }
    T& operator*() const noexcept { return *mPtr; }
    T* operator->() const noexcept { return mPtr; }
    explicit operator bool() const noexcept { return mPtr != nullptr; }

  private:
    T* mPtr;
};

template <class T>
inline void swap(const IntrusivePtr<T>& a, const IntrusivePtr<T>& b) noexcept {
    a.swap(b);
}

template <class T, class U>
inline bool operator==(const IntrusivePtr<T>& a, const IntrusivePtr<U>& b) noexcept {
    return a.get() == b.get();
}

template <class T, class U>
inline bool operator!=(const IntrusivePtr<T>& a, const IntrusivePtr<U>& b) noexcept {
    return a.get() != b.get();
}

template <class T>
inline bool operator==(const IntrusivePtr<T>& a, std::nullptr_t) noexcept {
    return a.get() == nullptr;
}

template <class T>
inline bool operator==(std::nullptr_t, const IntrusivePtr<T>& b) noexcept {
    return nullptr == b.get();
}

template <class T>
inline bool operator!=(const IntrusivePtr<T>& a, std::nullptr_t) noexcept {
    return a.get() != nullptr;
}

template <class T>
inline bool operator!=(std::nullptr_t, const IntrusivePtr<T>& b) noexcept {
    return nullptr != b.get();
}

template <class T, class U>
inline bool operator<(const IntrusivePtr<T>& a, const IntrusivePtr<U>& b) {
    return std::less<>()(a.get(), b.get());
}

}  // namespace goldfish::base
