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

#include "android/crashreport/annotation_circular_streambuf.h"

namespace android {
namespace crashreport {

#define _CRUMBSTR(x)      \
    case (Breadcrumb::x): \
        return #x;

constexpr const char* crumb_to_str(Breadcrumb value) {
    switch (value) {
        _CRUMBSTR(init);
        _CRUMBSTR(grpc);
        _CRUMBSTR(events);
        _CRUMBSTR(CRUMB_MAX);
    }
}

constexpr int kMaxThreadIdLength = 7;  // 7 digits for the thread id is what Google uses everywhere.

// Returns the current thread id as a string of at most kMaxThreadIdLength
// characters.
static std::string getStrThreadID() {
    static thread_local std::string cached_id;

    // If the ID is not cached yet, compute and store it
    if (cached_id.empty()) {
        std::stringstream ss;
        ss << std::this_thread::get_id();
        std::string result = ss.str();

        cached_id = result.length() > kMaxThreadIdLength
                            ? result.substr(result.length() - kMaxThreadIdLength)
                            : result;
    }

    return cached_id;
}

class BreadcrumbTrackerImpl {
  public:
    BreadcrumbTrackerImpl() {}

    std::ostream& getStreamForThread() {
        std::lock_guard<std::mutex> mLock(mMapAccess);

        auto id = getStrThreadID();
        auto tracker = std::make_unique<DefaultAnnotationCircularStreambuf>(id);
        mThreadStreams[id] = std::make_unique<std::ostream>(tracker.get());
        mBuffers.push_back(std::move(tracker));

        std::unique_ptr<std::ostream>& streamPtr = mThreadStreams[id];
        return *(streamPtr);
    }

    std::ostream& getStreamFor(Breadcrumb crumb) {
        std::lock_guard<std::mutex> mLock(mMapAccess);
        if (mStreams.count(crumb)) {
            std::unique_ptr<std::ostream>& streamPtr = mStreams[crumb];
            return *(streamPtr);
        }

        auto tracker = std::make_unique<DefaultAnnotationCircularStreambuf>(crumb_to_str(crumb));
        mStreams[crumb] = std::make_unique<std::ostream>(tracker.get());
        mBuffers.push_back(std::move(tracker));

        std::unique_ptr<std::ostream>& streamPtr = mStreams[crumb];
        return *(streamPtr);
    }

    // TESTING ONLY!
    std::unique_ptr<std::istream> getRdStreamFor(Breadcrumb crumb) {
        if (!mStreams.count(crumb)) {
            return nullptr;
        }

        auto buf =
                reinterpret_cast<DefaultAnnotationCircularStreambuf*>(getStreamFor(crumb).rdbuf());
        buf->sync();
        return std::make_unique<std::istream>(buf);
    }

    static BreadcrumbTrackerImpl* get() {
        static BreadcrumbTrackerImpl sBreadcrumbTrackerImpl;
        return &sBreadcrumbTrackerImpl;
    };

  private:
    std::vector<std::string> mThreadIds;
    std::vector<std::unique_ptr<DefaultAnnotationCircularStreambuf>> mBuffers;
    std::unordered_map<std::string, std::unique_ptr<std::ostream>> mThreadStreams;
    std::unordered_map<Breadcrumb, std::unique_ptr<std::ostream>> mStreams;
    std::mutex mMapAccess;
};

std::ostream& BreadcrumbTracker::stream() {
    // Note: this will only be initialized once, this call is basically
    // cached across various threads.
    static thread_local std::ostream& stream = BreadcrumbTrackerImpl::get()->getStreamForThread();
    DD_AN("Requesting: %s -> %p", getStrThreadID().c_str(), &stream);
    return stream;
}

std::ostream& BreadcrumbTracker::stream(Breadcrumb crumb) {
    return BreadcrumbTrackerImpl::get()->getStreamFor(crumb);
}

std::unique_ptr<std::istream> BreadcrumbTracker::rd(Breadcrumb crumb) {
    return BreadcrumbTrackerImpl::get()->getRdStreamFor(crumb);
}

std::unique_ptr<std::istream> BreadcrumbTracker::rd() {
    auto buf = reinterpret_cast<DefaultAnnotationCircularStreambuf*>(stream().rdbuf());
    buf->sync();
    return std::make_unique<std::istream>(buf);
}

}  // namespace crashreport
}  // namespace android
