#pragma once

#include "android/cmdline_definitions.h"

/**
 * @brief Configures the logging behavior based on command-line options.
 *
 * Sets the minimum log level and handles per-module log level settings.
 *
 * @param opts The AndroidOptions struct containing the command-line options.
 */
void configureLogging(const AndroidOptions& opts);