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

#include "goldfish/adb/adb_host_server.h"

#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/strip.h"

#include "android/base/system.h"
#include "android/sockets/scoped_socket.h"
#include "android/status/status_macros.h"

namespace goldfish::adb {

using android::base::ScopedSocket;
using android::base::System;

static ScopedSocket connectToAdbServer(int adbClientPort) {
    ScopedSocket socket(android::base::socketTcp4LoopbackClient(adbClientPort));
    if (!socket.valid()) {
        socket.reset(android::base::socketTcp6LoopbackClient(adbClientPort));
    }
    return socket;
}

// Sends a message to the ADB server on the socket file descriptor.
// The message will be formatted according to the ADB protocol
static bool sendMessage(int fd, const std::string& message) {
    // A client sends a request using the following format:
    //
    // 1. A 4-byte hexadecimal string giving the length of the payload
    // 2. Followed by the payload itself.
    auto wireformat = absl::StrFormat("%04x%s", (uint32_t)message.size(), message);
    return android::base::socketSendAll(fd, wireformat.c_str(), wireformat.size());
}

// Reads a response from the ADB server on the socket file descriptor.
// At most bytesToRead will be read, the return string will be resized
// to the number of bytes that have been read, and errno will be set.
static std::string readResponse(int fd, int bytesToRead) {
    std::string buffer(bytesToRead, '\0');
    unsigned int bytesRead = 0;
    while (bytesToRead > 0) {
        auto ret = android::base::socketRecv(fd, buffer.data() + bytesRead, bytesToRead);

        if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        if (ret <= 0) {
            break;
        }

        bytesToRead -= ret;
        bytesRead += ret;
    }
    buffer.resize(bytesRead);
    return buffer;
}

// Reads a protocol string from ADB
static absl::StatusOr<std::string> readProtocolString(int fd) {
    std::string lengthHex = readResponse(fd, 4);
    if (lengthHex.empty()) {
        return absl::UnavailableError("no response from ADB server");
    }
    uint32_t bytesToRead = 0;
    if (lengthHex.size() != 4 || !absl::SimpleHexAtoi(lengthHex, &bytesToRead)) {
        return absl::InvalidArgumentError(absl::StrCat(
                "adb protocol fault (couldn't read status length: '", lengthHex, "')"));
    }

    std::string payload = readResponse(fd, bytesToRead);
    if (payload.size() != bytesToRead) {
        return absl::InvalidArgumentError(
                absl::StrCat("adb protocol fault (couldn't read status message: expected ",
                             bytesToRead, " bytes, read ", payload.size(), ")"));
    }

    return payload;
}

static bool isUnauthorizedMessage(std::string_view msg) {
    return msg.find("unauthorized") != std::string_view::npos;
}

// Reads status response from ADB ("OKAY" or "FAIL" + error message) and returns
// absl::OkStatus() on OKAY, absl::UnauthenticatedError on auth mismatch, or an error Status.
static absl::Status readAdbStatus(int fd, std::string_view requestName, int serialNumber = 0) {
    auto response = readResponse(fd, 4);
    if (response == "OKAY") {
        return absl::OkStatus();
    }
    if (response.empty()) {
        return absl::UnavailableError("no response from ADB server");
    }
    if (response.size() != 4) {
        return absl::InvalidArgumentError(
                absl::StrCat("adb protocol fault (couldn't read status: expected 4 bytes, got ",
                             response.size(), ": '", response, "')"));
    }
    std::string errorMsg;
    if (response == "FAIL") {
        if (auto msg = readProtocolString(fd); msg.ok()) {
            errorMsg = std::string(absl::StripTrailingAsciiWhitespace(*msg));
        }
    }
    absl::Status status =
            isUnauthorizedMessage(errorMsg)
                    ? absl::UnauthenticatedError(errorMsg)
                    : absl::InternalError(absl::StrCat(
                              requestName, " rejected with ", response,
                              (!errorMsg.empty() ? absl::StrCat(": ", errorMsg) : "")));
    if (absl::IsUnauthenticated(status)) {
        LOG(WARNING) << "ADB host server reported device unauthorized for "
                     << (serialNumber > 0 ? absl::StrCat("emulator-", serialNumber) : "device")
                     << ": " << errorMsg
                     << " (ADB authentication mismatch between host keys and guest image).";
    } else {
        VLOG(1) << requestName << " rejected with " << response
                << (!errorMsg.empty() ? absl::StrCat(": ", errorMsg) : "");
    }
    return status;
}

absl::StatusOr<int> AdbHostServer::getProtocolVersion() {
    int port = AdbHostServer::getClientPort();

    // Okay. let's see if we can connect to this adb server.
    ScopedSocket fd = connectToAdbServer(port);
    if (!fd.valid()) {
        LOG(ERROR) << "Unable to connect to adb daemon on port: " << port;
        return absl::UnavailableError(
                absl::StrCat("unable to connect to adb daemon on port: ", port));
    }

    // And ask it for its protocol version.
    if (!sendMessage(fd.get(), "host:version")) {
        return absl::InternalError("failed to send host:version request");
    }

    android::base::socketSetNonBlocking(fd.get());

    // The server should answer a request with one of the following:
    //
    // 1. For success, the 4-byte "OKAY" string
    // 2. For failure, the 4-byte "FAIL" string, followed by a
    //    4-byte hex length, followed by a string giving the reason for failure.
    RETURN_IF_ERROR(readAdbStatus(fd.get(), "host:version request"));

    // for 'host:version', a 4-byte hex string corresponding to the server's
    // internal version number
    ASSIGN_OR_RETURN(auto version_result, readProtocolString(fd.get()));
    uint32_t version = 0;
    if (!absl::SimpleHexAtoi(version_result, &version)) {
        android::base::socketSetBlocking(fd.get());
        return absl::InternalError("failed to read protocol version from adb server");
    }

    android::base::socketSetBlocking(fd.get());
    return version;
}

bool AdbHostServer::notify(int adbEmulatorPort, int adbClientPort) {
    // First connect to ADB server.
    ScopedSocket socket = connectToAdbServer(adbClientPort);

    if (!socket.valid()) {
        // This can happen frequently when there is no ADB Server running
        // on the host, so don't complain with a warning or error message.
        return false;
    }
    // Send the special message
    auto message = absl::StrFormat("host:emulator:%d", adbEmulatorPort);
    return sendMessage(socket.get(), message);
}

absl::Status AdbHostServer::runShellCommand(const std::string& shellCommand, int serialNumber,
                                            int adbClientPort) {
    ScopedSocket socket = connectToAdbServer(adbClientPort);
    if (!socket.valid()) {
        VLOG(1) << "runShellCommand: failed to connect to ADB server on port " << adbClientPort;
        return absl::UnavailableError(
                absl::StrCat("failed to connect to ADB server on port ", adbClientPort));
    }

    std::string transport = (serialNumber > 0)
                                    ? absl::StrFormat("host:transport:emulator-%d", serialNumber)
                                    : std::string("host:transport-any");

    if (!sendMessage(socket.get(), transport)) {
        VLOG(1) << "runShellCommand: failed to send transport request: " << transport;
        return absl::InternalError(absl::StrCat("failed to send transport request: ", transport));
    }

    RETURN_IF_ERROR(readAdbStatus(socket.get(), "transport request", serialNumber));

    auto shell = absl::StrFormat("shell:%s", shellCommand);
    if (!sendMessage(socket.get(), shell)) {
        VLOG(1) << "runShellCommand: failed to send shell command: " << shellCommand;
        return absl::InternalError(absl::StrCat("failed to send shell command: ", shellCommand));
    }

    return readAdbStatus(socket.get(), "shell command", serialNumber);
}

int AdbHostServer::getClientPort() {
    int clientPort = kDefaultAdbClientPort;
    const std::string_view kVarName = "ANDROID_ADB_SERVER_PORT";
    std::string env = System::Get()->EnvGet(kVarName);
    if (!env.empty()) {
        long port = strtol(env.c_str(), NULL, 0);
        if (port <= 0 || port >= 65536) {
            LOG(ERROR) << "Env. var " << kVarName << " must be a number "
                       << "in 1..65535 range. Got " << port;
            return -1;
        }
        clientPort = static_cast<int>(port);
    }
    return clientPort;
}

}  // namespace goldfish::adb
