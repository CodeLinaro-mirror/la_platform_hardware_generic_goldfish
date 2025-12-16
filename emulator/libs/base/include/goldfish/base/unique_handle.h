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

#include <type_traits>
#include <utility>

namespace goldfish::base {

template <typename Type, Type kEmpty, typename Deleter>
struct UniqueHandle : private Deleter {
    ~UniqueHandle() {
        if (ok()) {
            (*this)(value_);
        }
    }

    UniqueHandle() : Deleter(typename Deleter::Empty()), value_(kEmpty) {}

    explicit UniqueHandle(Type value, Deleter d = Deleter())
            : Deleter(std::move(d)), value_(value) {}

    UniqueHandle(UniqueHandle&& rhs) noexcept(std::is_nothrow_move_constructible_v<Deleter>)
            : Deleter(std::move(static_cast<Deleter&>(rhs))), value_(rhs.release()) {}

    UniqueHandle& operator=(UniqueHandle&& rhs) noexcept(noexcept(swap(*this, rhs))) {
        UniqueHandle tmp(std::move(rhs));
        swap(*this, tmp);
        return *this;
    }

    explicit operator bool() const { return ok(); }
    bool ok() const { return value_ != kEmpty; }  // NOLINT

    Type get() const { return value_; }  // NOLINT

    Type release() { return std::exchange(value_, kEmpty); }  // NOLINT

    void reset(Type value = kEmpty) {  // NOLINT
        Type old_value = std::exchange(value_, value);
        if (old_value != kEmpty) {
            (*this)(old_value);  // Deleter::operator()
        }
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    // NOLINTNEXTLINE
    friend void swap(UniqueHandle& lhs,
                     UniqueHandle& rhs) noexcept(std::is_nothrow_swappable_v<Deleter>) {
        using std::swap;
        swap(static_cast<Deleter&>(lhs), static_cast<Deleter&>(rhs));
        swap(lhs.value_, rhs.value_);
    }

  private:
    Type value_ = kEmpty;
};

}  // namespace goldfish::base
