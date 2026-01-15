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

#include "absl/log/check.h"
#include "absl/log/log.h"

#include "goldfish/async/libuv_socket_factory.h"
#include "goldfish/network/un_endpoint.h"

namespace goldfish::devices::unix_pipe {

using goldfish::network::UnEndpoint;

class UnixPipe : public IUnixPipe {
  public:
    UnixPipe(EventLoop* client_loop, const std::string_view path) {
        auto un_addr = UnEndpoint::Create(std::string(path));
        if (!un_addr.ok()) {
            LOG(WARNING) << "Cannot create an AF_UNIX endpoint at '" << path << ": "
                         << un_addr.status();
            return;
        }

        std::shared_ptr<async::AsyncSocket> un_socket =
                socket_factory_.CreateSocket(client_loop, *un_addr);

        un_socket->SetOnConnectedCallback(
                [this](async::AsyncSocket&, const absl::Status& connect_status) {
                    DCHECK(un_socket_);
                    if (connect_status.ok()) {
                        OnReceiveImpl(queued_);
                        queued_.clear();
                        connected_ = true;
                    } else {
                        LOG(WARNING) << "`Connect` failed: " << connect_status;
                        un_socket_.reset();
                    }
                });

        un_socket->SetOnReadCallbackNoFlowControl(
                [this](std::string_view data, const absl::Status& err) {
                    if (err.ok()) {
                        Socket()->Send(std::string(data));
                    }
                });

        un_socket->SetOnCloseCallback([this]() {
            Socket()->Send({});
            Close();
        });

        const absl::Status connect_status = un_socket->Connect();
        if (connect_status.ok()) {
            un_socket_ = std::move(un_socket);
        } else {
            LOG(WARNING) << "Could not connect to " << ToString(*un_addr) << ": " << connect_status;
        }
    }

    void OnConnect() override {}

    void OnClose() override { Close(); }

    void OnReceive(std::string_view data) override {
        if (un_socket_) {
            if (connected_) {
                OnReceiveImpl(data);
            } else {
                queued_.append(data);
            }
        } else {
            LOG(WARNING) << "The host side is disconnected, " << data.size() << " bytes are lost";
        }
    }

    void OnReceiveImpl(std::string_view data) {
        const absl::Status s = un_socket_->Send(data.data(), data.size());
        if (!s.ok()) {
            CloseImpl();
            LOG(WARNING) << "Send failed with " << s;
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
    std::string queued_;
    bool connected_ = false;
};

void IUnixPipe::RegisterDevice(IConnectorRegistry* registry, EventLoop* client_loop,
                               EventLoop* qemu_loop) {
    registry->RegisterHalDevice(std::string(UnixPipe::kServiceName), client_loop, qemu_loop,
                                [client_loop](const std::string_view path) {
                                    return std::make_shared<UnixPipe>(client_loop, path);
                                });
}

}  // namespace goldfish::devices::unix_pipe
