/* Copyright 2026 The Android Open Source Project
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

#include <memory>
#include <ostream>

#include "google/protobuf/text_format.h"

#include "goldfish/metrics/metrics_writer.h"

namespace goldfish::metrics {

class TextMetricsWriter : public MetricsWriter {
  public:
    TextMetricsWriter(std::ostream& out) : out_(out) { printer_.SetSingleLineMode(true); }

    void Write(MetricsEvent event) override;

  private:
    std::ostream& out_;
    google::protobuf::TextFormat::Printer printer_;
};

class StreamOwnerTextMetricsWriter : public TextMetricsWriter {
  public:
    StreamOwnerTextMetricsWriter(std::unique_ptr<std::ostream> out)
            : TextMetricsWriter(*out), owned_stream_(std::move(out)) {}

  private:
    std::unique_ptr<std::ostream> owned_stream_;
};

}  // namespace goldfish::metrics
