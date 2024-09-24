
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
#include <cstdint>

#include "goldfish/devices/cable/cable.h"

namespace android {
namespace emulation {
namespace control {

#pragma pack(push, 1)
struct amessage {
    uint32_t command;     /* command identifier constant      */
    uint32_t arg0;        /* first argument                   */
    uint32_t arg1;        /* second argument                  */
    uint32_t data_length; /* length of payload (0 is allowed) */
    uint32_t data_check;  /* checksum of data payload         */
    uint32_t magic;       /* command ^ 0xffffffff             */
};

struct apacket {
    amessage message;
    char data[];
};
#pragma pack(pop)

class AdbMessageLogger {
  public:
    AdbMessageLogger(std::string prefix) : mPrefix(std::move(prefix)) {}
    ~AdbMessageLogger();
    void observe(const void* data, size_t size);

  private:
    std::string mPrefix;
    std::string mRawBytesInFlight;
    bool mOutOfSync{false};
};

class AdbLogger : public goldfish::devices::cable::IDataSniffer {
  public:
    AdbLogger(int hostPort, int guestPort);
    ~AdbLogger() = default;

    void toSocket(const void* data, size_t dataSize) override;
    void toPlug(const void* data, size_t dataSize) override;

  private:
    AdbMessageLogger mToGuest;
    AdbMessageLogger mToHost;
};
}  // namespace control
}  // namespace emulation
}  // namespace android