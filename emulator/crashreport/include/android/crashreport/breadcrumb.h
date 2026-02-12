// Copyright 2023 The Android Open Source Project
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
#include <cstdint>
#include <iostream>
#include <memory>

namespace android::crashreport {

/**
 * @brief Breadcrumb types.
 */
enum class Breadcrumb : std::uint8_t {
    kInit,     ///< Initialization breadcrumb.
    kGrpc,     ///< gRPC breadcrumb.
    kEvents,   ///< Events breadcrumb.
    kCrumbMax  ///< Maximum breadcrumb value.
};

/**
 * @brief A Breadcrumb tracker.
 *
 * Provides access to a stream on which you can leave behind breadcrumbs
 * that will end up in the crash report.  Every tag will get at most 8kb
 * on which they can leave behind a trail of things they have done.
 *
 * The crumbs will end up in the annotations as the enum name as
 * defined in "android/utils/debug.h", or as annotations with the
 * 'thread-id'.
 */
class BreadcrumbTracker {
  public:
    /**
     * @brief Gets the stream for a specific breadcrumb type.
     *
     * @param crumb The breadcrumb type.
     * @return A reference to the output stream for the given breadcrumb.
     */
    static std::ostream& Stream(Breadcrumb crumb);

    /**
     * @brief Gets the stream for the current thread.
     *
     * @return A reference to the output stream for the current thread.
     */
    static std::ostream& Stream();

    /**
     * @brief Gets a read stream for a specific breadcrumb (for testing).
     *
     * @param crumb The breadcrumb type.
     * @return A unique pointer to the input stream, or nullptr if no crumb
     * was written.
     */
    static std::unique_ptr<std::istream> Rd(Breadcrumb crumb);

    /**
     * @brief Gets a read stream for the current thread (for testing).
     *
     * @return A unique pointer to the input stream.
     */
    static std::unique_ptr<std::istream> Rd();
};

/**
 * @brief Macro to leave a breadcrumb for a specific type.
 *
 * @param x The breadcrumb type.
 *
 * Example usage:
 * @code
 * CRUMB(grpc) << "foo";
 * // ...
 * CRUMB(grpc) << "bar";
 * // .. CRASH ..
 * // Your annotation should contain something like this:
 * // 'grpc' : foobar
 * @endcode
 */
#define CRUMB(x) \
    (android::crashreport::BreadcrumbTracker::Stream(android::crashreport::Breadcrumb::x))

/**
 * @brief Macro to leave a breadcrumb for the current thread.
 *
 * The breadcrumb will be bucketized by thread-id.
 *
 * Example usage:
 * @code
 * TCRUMB() << "foo";
 * // ...
 * TCRUMB() << "bar";
 * // .. CRASH ..
 * // Your annotation should contain something like this:
 * // 'thread-id' : foobar
 * @endcode
 */
#define TCRUMB() (android::crashreport::BreadcrumbTracker::Stream())

}  // namespace android::crashreport
