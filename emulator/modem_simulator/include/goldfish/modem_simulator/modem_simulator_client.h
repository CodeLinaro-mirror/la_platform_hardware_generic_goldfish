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

#include "goldfish/modem_simulator/i_modem_simulator_client.h"

namespace goldfish::modem_simulator {

struct ModemSimulatorClient : public IModemSimulatorClient {
    ModemSimulatorClient(int serverPort);

    absl::StatusOr<CellInfo> SetCellInfo(const CellInfo&) override;
    absl::StatusOr<CellInfo> GetCellInfo() override;
    absl::StatusOr<Call> CreateCall(const Call&) override;
    absl::StatusOr<Call> UpdateCall(const Call&) override;
    absl::Status DeleteCall(const Call&) override;
    absl::StatusOr<std::vector<Call>> ListCalls() override;
    absl::Status ReceiveSmsUtf8(std::string_view sender, std::string_view message) override;
    absl::Status ReceiveSmsEncoded(std::vector<uint8_t> binary) override;
    absl::Status UpdateClock() override;

  private:
    const int serverPort_;
};

}  // namespace goldfish::modem_simulator
