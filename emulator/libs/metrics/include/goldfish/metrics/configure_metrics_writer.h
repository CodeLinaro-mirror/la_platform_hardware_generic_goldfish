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

#include <fstream>
#include <iostream>

#include "goldfish/async/event_loop.h"
#include "goldfish/file/file.h"
#include "goldfish/metrics/metrics_reporter.h"
#include "goldfish/metrics/metrics_writer.h"
#include "goldfish/metrics/studio_file_metrics_writer.h"
#include "goldfish/metrics/text_metrics_writer.h"

namespace goldfish::metrics {

enum class MetricsWriterType : uint32_t { kNone, kConsole, kFile, kStudio, kPlaystore };

struct MetricsWriterConfig {
    MetricsWriterType type;
    fs::path file_path;
    fs::path studio_spool_dir;

    bool user_upload_consent{false};
};

inline void ConfigureMetricsWriter(MetricsReporter& reporter, MetricsWriterConfig config,
                                   goldfish::async::EventLoop& event_loop) {
    switch (config.type) {
        using enum MetricsWriterType;
    case kConsole:
        reporter.SetWriter(std::make_unique<::goldfish::metrics::TextMetricsWriter>(std::cout));
        break;
    case kFile:
        reporter.SetWriter(std::make_unique<::goldfish::metrics::StreamOwnerTextMetricsWriter>(
                std::make_unique<std::ofstream>(config.file_path, std::ios::app)));
        break;
    case kStudio:
        reporter.SetWriter(std::make_unique<::goldfish::metrics::StudioFileMetricsWriter>(
                config.studio_spool_dir, reporter.session_id(), event_loop));
        break;
    case kPlaystore:
        // TODO
        break;
    case kNone:
    default:
        break;
    }
}

}  // namespace goldfish::metrics
