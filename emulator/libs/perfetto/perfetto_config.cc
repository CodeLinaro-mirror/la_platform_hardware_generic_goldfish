// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <chrono>
#include <fstream>
#include <thread>
#include <vector>

#include "absl/strings/str_split.h"

#include "goldfish/perfetto/perfetto.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
#include "goldfish/perfetto/perfetto_categories.h"
#include "perfetto/tracing/core/trace_config.h"
#include "perfetto/tracing/tracing.h"
#include "perfetto/tracing/track_event.h"

PERFETTO_TRACK_EVENT_STATIC_STORAGE();

namespace goldfish::perfetto {
void Initialize() {
    ::perfetto::TracingInitArgs args;
    args.backends = ::perfetto::kInProcessBackend;
    ::perfetto::Tracing::Initialize(args);
    ::perfetto::TrackEvent::Register();
}

EmulatorTracingSession::EmulatorTracingSession(const std::filesystem::path& trace_file,
                                               std::string_view enabled_categories) {
#ifdef _WIN32
    int fd = _open(trace_file.string().c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY,
                   _S_IREAD | _S_IWRITE);
#else
    int fd = open(trace_file.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
#endif

    if (fd == -1) {
        return;
    }

    ::perfetto::TraceConfig cfg;

    // If you see traced_buf_buffer_skipped_bytes or data_loss increasing, the buffer is too small.
    // If you see traced_buf_bytes_overwritten, the ring buffer is wrapping around before you it
    // has been streamed to disk.
    cfg.add_buffers()->set_size_kb(32 * 1024);
    cfg.set_flush_period_ms(1000);  // Flush every 1 second
    cfg.set_write_flush_mode(::perfetto::TraceConfig::WRITE_FLUSH_DISABLED);
    auto* ds_cfg = cfg.add_data_sources()->mutable_config();
    ds_cfg->set_name("track_event");

    ::perfetto::protos::gen::TrackEventConfig te_cfg;
    te_cfg.add_disabled_categories("*");

    for (std::string_view cat : absl::StrSplit(enabled_categories, ',')) {
        if (!cat.empty()) {
            te_cfg.add_enabled_categories(std::string(cat));
        }
    }

    ds_cfg->set_track_event_config_raw(te_cfg.SerializeAsString());

    session_ = ::perfetto::Tracing::NewTrace(::perfetto::kInProcessBackend);
    session_->Setup(cfg, fd);
    session_->StartBlocking();
}

EmulatorTracingSession::~EmulatorTracingSession() {
    Stop();
}

void EmulatorTracingSession::Stop() {
    if (!session_) return;

    ::perfetto::TrackEvent::Flush();
    session_->StopBlocking();
    session_.reset();
}

}  // namespace goldfish::perfetto
