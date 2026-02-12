// Copyright (C) 2025 The Android Open Source Project
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

#include "emulator/grpc/client/grpc_channel_factory.h"

#include <grpcpp/grpcpp.h>

#include <string>
#include <unordered_map>
#include <utility>

#include "absl/log/log.h"
#include "absl/strings/match.h"

namespace android::emulation::control {

namespace {
// A plugin that inserts a set of headers.
class HeaderInjector : public grpc::MetadataCredentialsPlugin {
  public:
    explicit HeaderInjector(std::unordered_map<std::string, std::string> headers)
            : headers_(std::move(headers)) {}
    ~HeaderInjector() override = default;

    grpc::Status GetMetadata(grpc::string_ref /*service_url*/, grpc::string_ref /*method_name*/,
                             const grpc::AuthContext& /*channel_auth_context*/,
                             std::multimap<grpc::string, grpc::string>* metadata) override {
        for (const auto& v : headers_) {
            metadata->insert(std::make_pair(v.first, v.second));
        }
        return grpc::Status::OK;
    }

  private:
    std::unordered_map<std::string, std::string> headers_;
};

}  // namespace

GrpcChannelFactory::GrpcChannelFactory(Endpoint endpoint, InterceptorFactories interceptors)
        : endpoint_(std::move(endpoint)), interceptors_(std::move(interceptors)) {}

std::shared_ptr<grpc::CallCredentials> GrpcChannelFactory::Credentials() const {
    if (credentials_) {
        return credentials_;
    }

    if (!endpoint_.required_headers().empty()) {
        std::unordered_map<std::string, std::string> map;
        for (const auto& header : endpoint_.required_headers()) {
            map[header.key()] = header.value();
        }
        credentials_ = grpc::MetadataCredentialsFromPlugin(
                std::make_unique<HeaderInjector>(std::move(map)));
    }
    return credentials_;
}

std::shared_ptr<grpc::Channel> GrpcChannelFactory::CreateChannel() {
    auto address = endpoint_.target();
    std::shared_ptr<grpc::ChannelCredentials> channel_creds;

    const std::string ca_pem = endpoint_.tls_credentials().pem_root_certs();
    const std::string key_pem = endpoint_.tls_credentials().pem_private_key();
    const std::string cer_pem = endpoint_.tls_credentials().pem_cert_chain();

    if (!ca_pem.empty() || !key_pem.empty() || !cer_pem.empty()) {
        grpc::SslCredentialsOptions ssl_opts;
        ssl_opts.pem_root_certs = ca_pem;
        ssl_opts.pem_private_key = key_pem;
        ssl_opts.pem_cert_chain = cer_pem;
        channel_creds = grpc::SslCredentials(ssl_opts);
    } else if (absl::StartsWith(address, "127.0.0.1") || absl::StartsWith(address, "localhost")) {
        channel_creds = ::grpc::experimental::LocalCredentials(LOCAL_TCP);
    } else {
        LOG(ERROR) << "Insecure connections are not supported for non-local "
                      "addresses. Please configure TLS credentials.";
        return nullptr;
    }

    auto creds = Credentials();
    if (creds) {
        channel_creds = grpc::CompositeChannelCredentials(channel_creds, creds);
    }

    grpc::ChannelArguments max_size;
    max_size.SetMaxReceiveMessageSize(-1);
    return grpc::experimental::CreateCustomChannelWithInterceptors(address, channel_creds, max_size,
                                                                   std::move(interceptors_));
}

}  // namespace android::emulation::control
