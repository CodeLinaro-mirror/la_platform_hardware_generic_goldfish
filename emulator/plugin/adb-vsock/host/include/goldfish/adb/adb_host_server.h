// Copyright 2016 The Android Open Source Project
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
#include <optional>
#include <string>

namespace goldfish::adb {

/**
 * @brief Utility class for interacting with the host ADB Server.
 */
struct AdbHostServer {
    /**
     * @brief Notify the host server that an emulator instance is listening for connections.
     *
     * @param adbEmulatorPort The localhost TCP port the emulator is listening on.
     * @param adbClientPort The localhost TCP port the ADB server is listening on.
     * @return True on success, false on error.
     */
    static bool notify(int adbEmulatorPort, int adbClientPort = getClientPort());

    /**
     * @brief Default ADB client port value.
     */
    static constexpr int kDefaultAdbClientPort = 5037;

    /**
     * @brief Get the ADB client port to use for this emulator instance.
     *
     * @return The ADB client port.
     */
    static int getClientPort();

    /**
     * @brief Run a shell command on an emulator instance via the local ADB server.
     *
     * @param shellCommand The shell command to execute.
     * @param serialNumber The optional serial number of the emulator (e.g. 5554 for emulator-5554,
     * or 0 for any).
     * @param adbClientPort The localhost TCP port the ADB server is listening on.
     * @return True on success, false on error.
     */
    static bool runShellCommand(const std::string& shellCommand, int serialNumber = 0,
                                int adbClientPort = getClientPort());

    /**
     * @brief Get the ADB protocol version reported by the running daemon.
     *
     * @return The ADB protocol version, or std::nullopt if unavailable
     */
    static std::optional<int> getProtocolVersion();
};

}  // namespace goldfish::adb
