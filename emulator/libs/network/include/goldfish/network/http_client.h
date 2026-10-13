// Copyright 2026 The Android Open Source Project
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
#include <memory>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "absl/types/span.h"

namespace goldfish::network {

class HttpClient {
  public:
    virtual ~HttpClient() = default;

    // If `cancel` is non-empty, it is polled periodically during the request; once it returns true,
    // the transfer is aborted with `absl::StatusCode::kCancelled`.
    virtual absl::StatusOr<std::string> Get(std::string_view url, absl::Duration timeout,
                                            const std::function<bool()>& cancel) = 0;

    virtual absl::StatusOr<std::string> Post(std::string_view url, std::string_view body,
                                             absl::Span<const std::string_view> headers,
                                             absl::Duration timeout,
                                             const std::function<bool()>& cancel) = 0;
};

std::unique_ptr<HttpClient> CreateCurlHttpClient();

}  // namespace goldfish::network
