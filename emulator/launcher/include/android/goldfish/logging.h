#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/log/globals.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"

struct AndroidOptions;

using SetVLogLevel = std::function<void(std::string_view, int)>;
/**
 * @brief Configures the logging behavior based on command-line options.
 *
 * Sets the minimum log level and handles per-module log level settings.
 *
 * @param opts The AndroidOptions struct containing the command-line options.
 * @param setVLogLevel Function to set vmodule log levels. Defaults to absl::SetVLogLevel.
 */
void configureLogging(const AndroidOptions& opts, SetVLogLevel setVLogLevel = &absl::SetVLogLevel);