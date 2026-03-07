// Copyright 2015 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#pragma once

namespace android::base {

/**
 * Executes the specified program with the given arguments, abstracting away
 * operating system-specific differences.
 *
 * This function provides a simplified interface for executing external
 * programs, handling variations between POSIX and Windows systems:
 *
 * - **POSIX:** Directly calls `execv()` to replace the current process with the
 *   new program.
 * - **Windows:** Spawns a child process and waits for its completion. This
 *   workaround addresses limitations in the Windows console that hinder
 *   concurrent process execution. Additionally, it installs a console control
 *   handler to gracefully terminate the child process in response to events
 * like Ctrl-C or closing the console window.
 *
 * @param path The path to the executable file.
 * @param argv An array of strings representing the command-line arguments,
 *             The array must be terminated by a NULL pointer.
 *
 * @return This function only returns on error; a successful execution
 *         will not return.
 */
int SafeExecv(const char* path, char* const* argv);
}  // namespace android::base