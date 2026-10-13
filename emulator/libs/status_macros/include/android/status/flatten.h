/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <type_traits>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace android::status {

/**
 * @brief Flattens status return types to avoid nested StatusOr types.
 *
 * Transforms types as follows:
 * - void -> absl::Status
 * - absl::Status -> absl::Status
 * - absl::StatusOr<T> -> absl::StatusOr<T>
 * - T -> absl::StatusOr<T>
 */
template <typename T>
struct flatten_status_or {
    using type = absl::StatusOr<T>;
};

template <>
struct flatten_status_or<void> {
    using type = absl::Status;
};

template <>
struct flatten_status_or<absl::Status> {
    using type = absl::Status;
};

template <typename T>
struct flatten_status_or<absl::StatusOr<T>> {
    using type = absl::StatusOr<T>;
};

template <typename T>
using flatten_status_or_t = typename flatten_status_or<std::decay_t<T>>::type;

template <typename T>
using flatten_t = flatten_status_or_t<T>;

}  // namespace android::status
