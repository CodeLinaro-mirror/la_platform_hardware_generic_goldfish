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

#include "goldfish/parsing/resizable_display_config.h"

#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"

namespace goldfish::parsing {

std::optional<std::vector<ResizableDisplayConfig>> ParseResizableDisplayConfig(
        const std::string_view config_str) {
    std::vector<ResizableDisplayConfig> resizable_configs;
    if (config_str.empty()) {
        return resizable_configs;
    }

    for (const auto& config : absl::StrSplit(config_str, ',')) {
        const std::vector<std::string_view> parts = absl::StrSplit(config, '-');

        if ((parts.size() < 4) || (parts.size() > 5)) {
            return std::nullopt;
        } else {
            ResizableDisplayConfig rdc;
            rdc.name = absl::StripAsciiWhitespace(parts[0]);
            if (rdc.name.empty()) {
                return std::nullopt;
            }

            if (!absl::SimpleAtoi(parts[1], &rdc.id) || !absl::SimpleAtoi(parts[2], &rdc.width) ||
                !absl::SimpleAtoi(parts[3], &rdc.height)) {
                return std::nullopt;
            }
            if (parts.size() == 5) {
                if (!absl::SimpleAtoi(parts[4], &rdc.dpi)) {
                    return std::nullopt;
                }
            }

            resizable_configs.push_back(std::move(rdc));
        }
    }

    return resizable_configs;
}

}  // namespace goldfish::parsing
