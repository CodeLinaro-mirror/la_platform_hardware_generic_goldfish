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

#include "goldfish/metrics/text_metrics_writer.h"

#include "absl/log/log.h"
#include "google/protobuf/text_format.h"

namespace goldfish::metrics {

void TextMetricsWriter::Write(MetricsEvent event) {
    std::string buf;
    LOG_IF(ERROR, !printer_.PrintToString(event.as_event, &buf))
            << "Failed to format metrics event";
    out_ << "event time " << event.time_ms << "ms\n{ " << buf << "}" << std::endl;
}

}  // namespace goldfish::metrics
