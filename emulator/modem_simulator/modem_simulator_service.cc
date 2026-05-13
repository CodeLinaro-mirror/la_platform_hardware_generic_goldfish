/* Copyright 2025 The Android Open Source Project
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

#include "goldfish/modem_simulator/modem_simulator_service.h"

#include <chrono>
#include <filesystem>
#include <thread>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/notification.h"
#include "goldfish/file/file.h"

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/fs/shared_select.h"

// clang-format off
// IWYU pragma: begin_keep
#ifdef _WIN32
#include <ws2def.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif
// IWYU pragma: end_keep
// clang-format on

#include "host/commands/modem_simulator/channel_monitor.h"
#include "host/commands/modem_simulator/device_config.h"
#include "host/commands/modem_simulator/modem_simulator.h"
#include "host/commands/modem_simulator/nvram_config.h"

namespace goldfish::modem_simulator {

using cuttlefish::ChannelMonitor;
using cuttlefish::ModemSimulator;
using cuttlefish::NvramConfig;
using cuttlefish::SharedFD;
using cuttlefish::modem::DeviceConfig;

namespace fs = std::filesystem;

namespace {

/**
 * See `int cuttlefish::modem::DeviceConfig::host_id()`. For AF_INETx
 * sockets this is the server socket port, for AF_UNIX sockets (if we
 * decide to support them), this is the number after the "modem_simulator"
 * in the socket name.
 */
int GetHostId(const SharedFD& server) {
    struct sockaddr_storage addr;
    socklen_t addlen;
    if (!server->Endpoint(&addr, &addlen)) {
        return -1;
    }

    switch (addr.ss_family) {
    case AF_INET:
        return ntohs(reinterpret_cast<const struct sockaddr_in*>(&addr)->sin_port);

    case AF_INET6:
        return ntohs(reinterpret_cast<const struct sockaddr_in6*>(&addr)->sin6_port);
    }

    return -1;
}

struct ModemSimulatorServiceImpl : public ModemSimulatorService {
    ModemSimulatorServiceImpl(SharedFD host_fd, SharedFD guest_fd,
                              std::shared_ptr<ModemSimulator> modem_simulator)
            : host_fd_(std::move(host_fd))
            , guest_fd_(std::move(guest_fd))
            , modem_simulator_(std::move(modem_simulator))
            , host_processor_(&ModemSimulatorServiceImpl::HostProcessingLoop, this)
            , guest_processor_(&ModemSimulatorServiceImpl::GuestProcessingLoop, this) {}

    ~ModemSimulatorServiceImpl() override {
        cuttlefish::SharedFD::SocketClient(host_fd_)->Write("STOP", 4);
        host_processor_.join();
        guest_processor_.join();
    }

    std::string ChardevEndpoint() const override { return guest_fd_->ChardevEndpoint(); }
    int HostId() const override { return GetHostId(host_fd_); }

  private:
    void HostProcessingLoop() {
        while (true) {
            cuttlefish::SharedFDSet read_set;
            read_set.Set(host_fd_);
            const int num_fds = cuttlefish::Select(&read_set, nullptr, nullptr, nullptr);
            if (num_fds <= 0) {
                continue;  // Ignore Select error
            }

            if (read_set.IsSet(host_fd_)) {
                auto conn = cuttlefish::SharedFD::Accept(*host_fd_);
                std::string buf(4, ' ');
                auto read = cuttlefish::ReadExact(conn, &buf);
                if (read <= 0) {
                    conn->Close();
                    continue;
                }

                if (buf.compare("STOP") == 0) {
                    break;
                } else if (buf.compare(0, 3, "REM") == 0) {
                    const int id = buf[3] - '0';
                    if ((id < 0) || (id > 9)) {
                        LOG(WARNING) << "Unexpected request: '" << buf << "'";
                    } else if (id != 0) {
                        LOG(WARNING) << "We support one modem only, id=" << 0;
                    } else {
                        modem_simulator_->SetRemoteClient(conn, true);
                    }
                } else {
                    LOG(WARNING) << "Unexpected request: '" << buf << "'";
                }
            }
        }
    }

    void GuestProcessingLoop() {
        // TODO: process the requests from the API.
        // Since we don't have any API at this point - do nothing.
    }

    const SharedFD host_fd_;   // dunno
    const SharedFD guest_fd_;  // this is where the guest connects to
    const std::shared_ptr<ModemSimulator> modem_simulator_;
    std::thread host_processor_;
    std::thread guest_processor_;
};

void CopyIfMissing(const fs::path& modem_avd_dir, const char* dst_filename,
                   const fs::path& src_path) {
    using android::base::file::copy_if_missing;

    const absl::Status status = copy_if_missing(modem_avd_dir / dst_filename, src_path);
    if (!status.ok()) {
        LOG(WARNING) << "Could not copy '" << dst_filename << "': " << status;
    }
}

/**
 * Make sure the important files exist in `modem_simulator_dir`,
 * otherwise copy them from the system image.
 */
void CheckModemSimulatorAvdDir(const fs::path& modem_avd_dir, const fs::path& si_data_dir) {
    const fs::path modem_simulator_si_dir = si_data_dir / "misc" / "modem_simulator";

    CopyIfMissing(modem_avd_dir, "iccprofile_for_sim0.xml",
                  modem_simulator_si_dir / "iccprofile_for_sim0.xml");

    CopyIfMissing(modem_avd_dir, "iccprofile_for_carrierapitests.xml",
                  modem_simulator_si_dir / "iccprofile_for_carrierapitests.xml");

    CopyIfMissing(modem_avd_dir, "numeric_operator.xml",
                  modem_simulator_si_dir / "etc" / "modem_simulator" / "files" / "numeric_operator.xml");
}

absl::StatusOr<std::shared_ptr<ModemSimulatorService>> CreateImpl(
        const Avd& avd, const std::optional<std::filesystem::path>& icc_profile_override) {
    constexpr size_t kNumModems = 1;
    constexpr int kModemId = 0;

    fs::path modem_simulator_avd_dir = avd.GetContentPath() / "modem_simulator";
    if (!fs::exists(modem_simulator_avd_dir)) {
        std::error_code ec;
        if (!fs::create_directories(modem_simulator_avd_dir, ec)) {
            return absl::InternalError(
                    absl::StrCat("could not create '", modem_simulator_avd_dir.string(),
                                 "': ", ec.message()));
        }
    }

    CheckModemSimulatorAvdDir(modem_simulator_avd_dir, avd.GetSystemImagePaths().data_dir);

    SharedFD host_fd = SharedFD::SocketLocalServer();
    SharedFD guest_fd = SharedFD::SocketLocalServer();
    if (!host_fd || !guest_fd) {
        return absl::UnavailableError("could not open a listening socket");
    }

    const int host_id = GetHostId(host_fd);
    if (host_id <= 0) {
        return absl::InternalError("could not get the modem id");
    }

    DeviceConfig::SetBasePath(std::move(modem_simulator_avd_dir));
    DeviceConfig::SetHostId(host_id);
    DeviceConfig::SetTimezone("America/Los_Angeles");  // TODO: do not hardcode

    NvramConfig::InitNvramConfigService(kNumModems, icc_profile_override);

    auto modem_simulator = std::make_shared<ModemSimulator>(kModemId);

    modem_simulator->Initialize(std::make_unique<ChannelMonitor>(*modem_simulator, guest_fd));
    modem_simulator->SetPhoneNumber("+12345550123");

    return std::make_shared<ModemSimulatorServiceImpl>(std::move(host_fd), std::move(guest_fd),
                                                       std::move(modem_simulator));
}
}  // namespace

std::shared_ptr<ModemSimulatorService> ModemSimulatorService::Create(
        const Avd& avd,
        const std::optional<std::filesystem::path>& icc_profile_override) {
    auto service = CreateImpl(avd, icc_profile_override);
    if (!service.ok()) {
        LOG(ERROR) << "Modem simulator: " << service.status() <<
                      ". Cellular features (e.g. text messages and phone calls) will "
                      "not be available.";
        return {};
    }

    return *std::move(service);
}

}  // namespace goldfish::modem_simulator
