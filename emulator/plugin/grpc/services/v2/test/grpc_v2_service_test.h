// Copyright (C) 2026 The Android Open Source Project
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

#pragma once

#include <grpcpp/grpcpp.h>

#include <vector>

namespace goldfish::grpc::v2::test {

/**
 * @brief In-memory mock reactor that captures written messages without a gRPC server.
 */
template <typename W>
class FakeWriteReactor {
  public:
    virtual ~FakeWriteReactor() = default;
    virtual void OnWriteDone(bool /*ok*/) {}
    virtual void OnDone() {}
    virtual void OnCancel() {}

    void StartWrite(const W* msg) {
        start_write_called = true;
        last_write_msg = msg;
        written_messages.push_back(*msg);
    }

    void Finish(::grpc::Status status) {
        finish_called = true;
        finish_status = status;
    }

    bool start_write_called = false;
    bool finish_called = false;
    ::grpc::Status finish_status = ::grpc::Status::OK;
    const W* last_write_msg = nullptr;
    std::vector<W> written_messages;
};

}  // namespace goldfish::grpc::v2::test
