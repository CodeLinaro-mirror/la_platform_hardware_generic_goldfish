// Copyright (C) 2025 The Android Open Source Project
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

#include <functional>

#include "goldfish/async/event_loop.h"

namespace goldfish::async {

/**
 * A tool which posts a task when the all child tasks finish (via refcounting).
 *
 * See when_all_test.cc for an example.
 */
template <class Results>
struct WhenAll {
    using OnAllDone = std::function<void(Results)>;

    WhenAll(EventLoop* dst_loop, OnAllDone on_all_done)
            : dst_loop_(dst_loop), on_all_done_(std::move(on_all_done)) {}

    WhenAll(EventLoop* dst_loop, OnAllDone on_all_done, Results results)
            : dst_loop_(dst_loop)
            , on_all_done_(std::move(on_all_done))
            , results_(std::move(results)) {}

    ~WhenAll() {
        dst_loop_
                ->Post([on_all_done = std::move(this->on_all_done_),
                        results = std::move(this->results_)]() mutable {
                    on_all_done(std::move(results));
                })
                .IgnoreError();
    }

    Results& MutableResults() { return results_; }

    WhenAll(const WhenAll&) = delete;
    WhenAll(WhenAll&&) = delete;
    WhenAll& operator=(const WhenAll&) = delete;
    WhenAll& operator=(WhenAll&&) = delete;

  private:
    EventLoop* const dst_loop_;
    OnAllDone on_all_done_;
    Results results_;
};

}  // namespace goldfish::async