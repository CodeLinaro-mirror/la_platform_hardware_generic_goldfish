// Copyright 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "goldfish/devices/unix_pipe/unix_pipe.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "absl/log/log.h"

#include "goldfish/async/libuv_socket_factory.h"
#include "goldfish/network/un_endpoint.h"

namespace goldfish::devices::unix_pipe {

using goldfish::network::UnEndpoint;

class UnixPipe : public IUnixPipe {
  public:
    UnixPipe(EventLoop* clientLoop, const std::string_view path) {
        auto un_addr = UnEndpoint::Create(std::string(path));
        if (!un_addr.ok()) {
            LOG(WARNING) << "Cannot create an AF_UNIX endpoint at '" << path << ": "
                         << un_addr.status();
            return;
        }

        un_socket_ = socket_factory_.CreateSocket(clientLoop, *std::move(un_addr));
        un_socket_->SetOnReadCallbackNoFlowControl([this](std::string_view data, absl::Status err) {
            if (err.ok()) {
                socket()->send(std::string(data));
            }
        });

        un_socket_->SetOnCloseCallback([this]() { Close(); });
    }

    void onConnect() override {}

    void onClose() override { Close(); }

    void onReceive(std::string_view data) override {
        if (un_socket_) {
            const absl::Status s = un_socket_->Send(data.data(), data.size());
            if (!s.ok()) {
                CloseImpl();
                LOG(WARNING) << "Send failed with " << s;
            }
        } else {
            LOG(WARNING) << "The host side is disconnected, " << data.size() << " bytes are lost";
        }
    }

    void Close() {
        if (un_socket_) {
            CloseImpl();
        }
    }

  private:
    void CloseImpl() {
        un_socket_->Close();
        un_socket_.reset();
    }

    async::LibuvAsyncSocketFactory socket_factory_;
    std::shared_ptr<async::AsyncSocket> un_socket_;
};

void IUnixPipe::registerDevice(IConnectorRegistry* registry, EventLoop* clientLoop,
                               EventLoop* qemuLoop) {
    registry->registerHalDevice(std::string(UnixPipe::serviceName), clientLoop, qemuLoop,
                                [clientLoop](const std::string_view path) {
                                    return std::make_shared<UnixPipe>(clientLoop, path);
                                });
}

}  // namespace goldfish::devices::unix_pipe
