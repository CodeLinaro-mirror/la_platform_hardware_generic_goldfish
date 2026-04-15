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
#include "android/crashreport/breadcrumb.h"

#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "annotation_circular_streambuf.h"

namespace android::crashreport {

namespace {

#define CRUMBSTR(x)       \
    case (Breadcrumb::x): \
        return #x;

constexpr const char* CrumbToStr(Breadcrumb value) {
    switch (value) {
        CRUMBSTR(kInit);
        CRUMBSTR(kGrpc);
        CRUMBSTR(kEvents);
        CRUMBSTR(kQemu);
        CRUMBSTR(kCrumbMax);
    }
}

constexpr int kMaxThreadIdLength = 7;  // 7 digits for the thread id is what Google uses everywhere.

// Returns the current thread id as a string of at most kMaxThreadIdLength
// characters.
std::string GetStrThreadId() {
    static thread_local std::string cached_id;

    // If the ID is not cached yet, compute and store it
    if (cached_id.empty()) {
        std::stringstream ss;
        ss << std::this_thread::get_id();
        const std::string result = ss.str();

        cached_id = result.length() > kMaxThreadIdLength
                            ? result.substr(result.length() - kMaxThreadIdLength)
                            : result;
    }

    return cached_id;
}
}  // namespace

class BreadcrumbTrackerImpl {
  public:
    BreadcrumbTrackerImpl() = default;

    std::ostream& GetStreamForThread() {
        const std::lock_guard<std::mutex> lock(map_access_);

        auto id = GetStrThreadId();
        auto tracker = std::make_unique<DefaultAnnotationCircularStreambuf>(id);
        thread_streams_[id] = std::make_unique<std::ostream>(tracker.get());
        buffers_.push_back(std::move(tracker));

        std::unique_ptr<std::ostream>& stream_ptr = thread_streams_[id];
        return *(stream_ptr);
    }

    std::ostream& GetStreamFor(Breadcrumb crumb) {
        const std::lock_guard<std::mutex> lock(map_access_);
        if (streams_.contains(crumb)) {
            std::unique_ptr<std::ostream>& stream_ptr = streams_[crumb];
            return *(stream_ptr);
        }

        auto tracker = std::make_unique<DefaultAnnotationCircularStreambuf>(CrumbToStr(crumb));
        streams_[crumb] = std::make_unique<std::ostream>(tracker.get());
        buffers_.push_back(std::move(tracker));

        std::unique_ptr<std::ostream>& stream_ptr = streams_[crumb];
        return *(stream_ptr);
    }

    // TESTING ONLY!
    std::unique_ptr<std::istream> GetRdStreamFor(Breadcrumb crumb) {
        if (!streams_.contains(crumb)) {
            return nullptr;
        }

        auto* buf =
                reinterpret_cast<DefaultAnnotationCircularStreambuf*>(GetStreamFor(crumb).rdbuf());
        buf->sync();
        return std::make_unique<std::istream>(buf);
    }

    static BreadcrumbTrackerImpl* Get() {
        static BreadcrumbTrackerImpl s_breadcrumb_tracker_impl;
        return &s_breadcrumb_tracker_impl;
    };

  private:
    std::vector<std::string> thread_ids_;
    std::vector<std::unique_ptr<DefaultAnnotationCircularStreambuf>> buffers_;
    std::unordered_map<std::string, std::unique_ptr<std::ostream>> thread_streams_;
    std::unordered_map<Breadcrumb, std::unique_ptr<std::ostream>> streams_;
    std::mutex map_access_;
};

std::ostream& BreadcrumbTracker::Stream() {
    // Note: this will only be initialized once, this call is basically
    // cached across various threads.
    static thread_local std::ostream& stream = BreadcrumbTrackerImpl::Get()->GetStreamForThread();
    DD_AN("Requesting: %s -> %p", GetStrThreadId().c_str(), &stream);
    return stream;
}

std::ostream& BreadcrumbTracker::Stream(Breadcrumb crumb) {
    return BreadcrumbTrackerImpl::Get()->GetStreamFor(crumb);
}

std::unique_ptr<std::istream> BreadcrumbTracker::Rd(Breadcrumb crumb) {
    return BreadcrumbTrackerImpl::Get()->GetRdStreamFor(crumb);
}

std::unique_ptr<std::istream> BreadcrumbTracker::Rd() {
    auto* buf = reinterpret_cast<DefaultAnnotationCircularStreambuf*>(Stream().rdbuf());
    buf->sync();
    return std::make_unique<std::istream>(buf);
}

}  // namespace android::crashreport
