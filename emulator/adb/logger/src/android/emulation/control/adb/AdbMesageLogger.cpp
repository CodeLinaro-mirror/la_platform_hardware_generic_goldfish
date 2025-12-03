
// Copyright (C) 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS);
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <algorithm>
#include <string_view>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"

#include "android/emulation/control/adb/AdbMessageLogger.h"

namespace goldfish::adb {

// Maximum number of characters or bytes we are willing to log.
constexpr size_t MAX_DATA_LOG_LENGTH = 512;

static bool isValidHeader(amessage* message) {
    return (message->magic ^ 0xffffffff) == message->command;
}

template <typename Sink>
void AbslStringify(Sink& sink, apacket* packet) {
    absl::Format(&sink, "{");  // Start of JSON object

    // Message fields
    absl::Format(&sink, "\"mesg\":{");
    absl::Format(&sink, "\"command\":\"%c%c%c%c\",",
                 static_cast<char>(packet->message.command & 0xFF),
                 static_cast<char>((packet->message.command >> 8) & 0xFF),
                 static_cast<char>((packet->message.command >> 16) & 0xFF),
                 static_cast<char>((packet->message.command >> 24) & 0xFF));
    absl::Format(&sink, "\"arg0\":0x%08x,", packet->message.arg0);
    absl::Format(&sink, "\"arg1\":0x%08x,", packet->message.arg1);
    absl::Format(&sink, "\"data_length\":%u,", packet->message.data_length);
    absl::Format(&sink, "\"data_check\":0x%08x,", packet->message.data_check);
    absl::Format(&sink, "\"magic\":0x%08x,\"magic_valid\":%s", packet->message.magic,
                 isValidHeader(&packet->message) ? "true" : "false");
    absl::Format(&sink, "},");  // End of "mesg" object

    // Data field
    if (packet->message.data_length > 0) {
        absl::Format(&sink, "\"data\":\"");
        for (size_t i = 0; i < std::min<size_t>(packet->message.data_length, MAX_DATA_LOG_LENGTH);
             ++i) {
            if (std::isprint(packet->data[i])) {
                absl::Format(&sink, "%c", packet->data[i]);
            } else {
                // Escape special characters for JSON
                switch (packet->data[i]) {
                case '"':
                    absl::Format(&sink, "\\\"");
                    break;
                case '\\':
                    absl::Format(&sink, "\\\\");
                    break;
                case '\b':
                    absl::Format(&sink, "\\b");
                    break;
                case '\f':
                    absl::Format(&sink, "\\f");
                    break;
                case '\n':
                    absl::Format(&sink, "\\n");
                    break;
                case '\r':
                    absl::Format(&sink, "\\r");
                    break;
                case '\t':
                    absl::Format(&sink, "\\t");
                    break;
                default:
                    absl::Format(&sink, "\\u%04x", packet->data[i]);
                }
            }
        }
        if (packet->message.data_length > MAX_DATA_LOG_LENGTH) {
            absl::Format(&sink, "...<snip>");
        }
        absl::Format(&sink, "\"");
    }
    // End of JSON object
}

AdbMessageLogger::~AdbMessageLogger() {
    VLOG(1) << "~AdbMessageLogger()";
}

void AdbMessageLogger::observe(const void* data, size_t size) {
    if (mOutOfSync) {
        return;
    }

    mRawBytesInFlight.append(static_cast<const char*>(data), size);

    constexpr size_t amessage_size = sizeof(amessage);

    // TODO(jansen): Should we attempt to get in sync again?
    while (mRawBytesInFlight.size() >= amessage_size && !mOutOfSync) {
        // We have at least a header available.
        amessage* header = (amessage*)mRawBytesInFlight.data();
        auto available_data = mRawBytesInFlight.size() - amessage_size;
        if (available_data < header->data_length) {
            // We have the header, but not the data.
            break;
        }

        // We have a full packet, time to log this thing!
        apacket* packet = (apacket*)mRawBytesInFlight.data();
        LOG(INFO) << mPrefix << packet;

        mOutOfSync = !isValidHeader(&packet->message);
        if (mOutOfSync) {
            LOG(INFO) << "Adb stream out of sync, disabling further logging";
        }

        // And clear remove this packet from the buffer.
        mRawBytesInFlight.erase(0, amessage_size + header->data_length);
    }
}

AdbLogger::AdbLogger(int hostPort, int guestPort)
        : mToGuest(AdbMessageLogger(absl::StrFormat(">> (%d) ", guestPort)))
        , mToHost(AdbMessageLogger(absl::StrFormat("<< (%d) ", hostPort))) {}

void AdbLogger::toSocket(const void* data, size_t dataSize) {
    mToGuest.observe(data, dataSize);
}
void AdbLogger::toPlug(const void* data, size_t dataSize) {
    mToHost.observe(data, dataSize);
}

}  // namespace goldfish::adb
