/* Copyright (C) 2024 The Android Open Source Project
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#pragma once
#include <cassert>

#include "absl/log/log.h"

#ifdef NDEBUG
#define NOT_NULL(P) P
#else
#define NOT_NULL(P) (assert(P), P)
#endif

/* The `FAILURE` and `FAILURE_STR` macros provide a convenient way to log an
 * error and return a failure value in a single statement.
 *
 * When a function needs to return a specific value to indicate an error (e.g.,
 * `nullptr`, `std::nullopt`, an empty handle), you can use `FAILURE` to wrap
 * the return value.
 *
 * Example:
 *   int foo(...) {
 *       ...
 *       if (error) {
 *           // This logs an error message and returns ERROR42.
 *           return FAILURE(ERROR42);
 *       }
 *       ...
 *   }
 *
 *   int bar(...) {
 *       ...
 *       if (error) {
 *           // This logs an error message and returns ERROR42.
 *           return FAILURE_STR("something is not right", ERROR42);
 *       }
 *       ...
 *   }
 *
 * The logged message includes the function name (`__func__`), a string literal
 * (if provided, see FAILURE_STR) or the stringified expression (e.g. "ERROR42"))
 * and an optional prefix if `FAILURE_DEBUG_PREFIX` is defined.
 */
#ifdef FAILURE_DEBUG_PREFIX
#define FAILURE_STR(LITERAL, X) \
  ([]() { LOG(ERROR) << FAILURE_DEBUG_PREFIX << ":" << __func__ << ": failure: " << LITERAL; }(), X)
#else
#define FAILURE_STR(LITERAL, X) ([]() { LOG(ERROR) << __func__ << ": failure: " << LITERAL; }(), X)
#endif

#define FAILURE(X) FAILURE_STR(#X, X)