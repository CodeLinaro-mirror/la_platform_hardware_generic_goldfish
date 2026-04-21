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

class UnixPipe : public IUnixPipe, public std::enable_shared_from_this<UnixPipe> {
  public:
    explicit UnixPipe(EventLoop* client_loop) : client_loop_(client_loop) {}

    void Init(const std::string_view path) {
        auto un_addr = UnEndpoint::Create(std::string(path));
        if (!un_addr.ok()) {
            LOG(WARNING) << "Cannot create an AF_UNIX endpoint at '" << path << ": "
                         << un_addr.status();
            return;
        }

        client_loop_
                ->Post([self = shared_from_this(), un_addr = *std::move(un_addr)]() {
                    self->InitOnEventLoop(un_addr);
                })
                .IgnoreError();
    }

    void OnConnect() override {}

    void OnClose() override { Close(); }

    void OnReceive(std::string_view data) override {
        client_loop_
                ->Post([self = shared_from_this(), data = std::string(data)]() {
                    self->OnReceiveOnEventLoop(data);
                })
                .IgnoreError();
    }

    void Close() {
        client_loop_->Post([self = shared_from_this()]() { self->CloseOnEventLoop(); })
                .IgnoreError();
    }

  private:
    void InitOnEventLoop(const UnEndpoint& un_addr) {
        std::shared_ptr<async::AsyncSocket> un_socket =
                socket_factory_.CreateSocket(client_loop_, un_addr);

        std::weak_ptr<UnixPipe> weak_self = shared_from_this();
        un_socket->SetOnConnectedCallback(
                [weak_self](async::AsyncSocket&, const absl::Status& connect_status) {
                    if (auto self = weak_self.lock()) {
                        if (connect_status.ok()) {
                            self->OnReceiveImpl(self->queued_);
                            self->queued_.clear();
                            self->connected_ = true;
                        } else {
                            LOG(WARNING) << "`Connect` failed: " << connect_status;
                            self->CloseImpl();
                        }
                    }
                });

        un_socket->SetOnReadCallbackNoFlowControl(
                [weak_self](std::string_view data, const absl::Status& err) {
                    if (auto self = weak_self.lock()) {
                        if (err.ok()) {
                            self->Socket()->Send(std::string(data));
                        }
                    }
                });

        un_socket->SetOnCloseCallback([weak_self]() {
            if (auto self = weak_self.lock()) {
                self->Socket()->Send({});
                self->CloseImpl();
            }
        });

        const absl::Status connect_status = un_socket->Connect();
        if (connect_status.ok()) {
            un_socket_ = std::move(un_socket);
        } else {
            LOG(WARNING) << "Could not connect to " << ToString(un_addr) << ": " << connect_status;
        }
    }

    void OnReceiveOnEventLoop(const std::string_view data) {
        if (un_socket_) {
            if (connected_) {
                OnReceiveImpl(data);
            } else {
                queued_.append(data);
            }
        }
    }

    void CloseOnEventLoop() { CloseImpl(); }

    void OnReceiveImpl(const std::string_view data) {
        if (!un_socket_) {
            return;
        }
        const absl::Status s = un_socket_->Send(data.data(), data.size());
        if (!s.ok()) {
            CloseImpl();
            LOG(WARNING) << "Send failed with " << s;
        }
    }

    void CloseImpl() {
        if (un_socket_) {
            un_socket_->Close();
            un_socket_.reset();
        }
    }

    EventLoop* const client_loop_;
    async::LibuvAsyncSocketFactory socket_factory_;
    std::shared_ptr<async::AsyncSocket> un_socket_;
    std::string queued_;
    bool connected_ = false;
};

void IUnixPipe::RegisterDevice(IConnectorRegistry* registry, EventLoop* client_loop,
                               EventLoop* qemu_loop) {
    registry->RegisterHalDevice(std::string(UnixPipe::kServiceName), client_loop, qemu_loop,
                                [client_loop](const std::string_view path) {
                                    auto dev = std::make_shared<UnixPipe>(client_loop);
                                    dev->Init(path);
                                    return dev;
                                });
}

}  // namespace goldfish::devices::unix_pipe
