#include "logging.h"

#include <unistd.h>

#include <functional>
#include <iostream>
#include <string_view>
#include <vector>

#include "absl/log/globals.h"
#include "absl/log/log_sink_registry.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"

#include "android/base/color_log_sink.h"
#include "android/cmdline_option.h"

void configureLogging(const AndroidOptions& opts, SetVLogLevel setVLogLevel) {
    absl::LogSeverityAtLeast launcherLogLevel =
            opts.verbose ? absl::LogSeverityAtLeast::kInfo : absl::LogSeverityAtLeast::kWarning;
    absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
    absl::SetMinLogLevel(launcherLogLevel);

    // TODO switch launcher and qemu to color log sink
    //static android::base::ColorLogSink logSink(&std::cout, isatty(fileno(stdout)));
    //absl::AddLogSink(&logSink);
    //absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfinity);

    if (int v_level; opts.V && absl::SimpleAtoi(opts.V, &v_level)) {
        absl::SetGlobalVLogLevel(v_level);
    }

    if (!opts.vmodule) {
        return;
    }

    std::vector<std::pair<std::string_view, int>> glob_levels;
    for (absl::string_view glob_level : absl::StrSplit(opts.vmodule, ',')) {
        const size_t eq = glob_level.rfind('=');
        if (eq == glob_level.npos) continue;
        const absl::string_view glob = glob_level.substr(0, eq);
        int level;
        if (!absl::SimpleAtoi(glob_level.substr(eq + 1), &level)) continue;
        glob_levels.emplace_back(glob, level);
    }
    for (const auto& it : glob_levels) {
        const absl::string_view glob = it.first;
        const int level = it.second;
        setVLogLevel(glob, level);
    }
}
