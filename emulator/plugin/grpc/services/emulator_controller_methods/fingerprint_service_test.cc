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
#include "fingerprint_service.h"

#include <memory>

#include "emulator_controller.grpc.pb.h"
#include "test/grpc_service_test.h"

namespace android::emulation::control {

using ::goldfish::avd_universe::fingerprint::ObservableFingerprintSensor;
using ::google::protobuf::Empty;
using ::grpc::ServerContext;
using ::grpc::Status;

class FingerprintServiceWrapper : public EmulatorController::Service {
  public:
    explicit FingerprintServiceWrapper(FingerprintServiceImpl& fingerprint_service)
            : fingerprint_service_(fingerprint_service) {}

    Status sendFingerprint(ServerContext* /*context*/, const Fingerprint* request,
                           Empty* /*reply*/) override {
        return fingerprint_service_.sendFingerprint(*request);
    }

  private:
    FingerprintServiceImpl& fingerprint_service_;
};

class FingerprintServiceTest : public GrcpServiceTest {
  protected:
    void SetUp() override {
        fingerprint_service_ = std::make_unique<FingerprintServiceImpl>(sensor_);
        service_wrapper_ = std::make_unique<FingerprintServiceWrapper>(*fingerprint_service_);

        GrcpServiceTest::SetUp();
    }

    EmulatorController::Service* getService() override { return service_wrapper_.get(); }

  protected:
    ObservableFingerprintSensor sensor_;
    std::unique_ptr<FingerprintServiceImpl> fingerprint_service_;
    std::unique_ptr<FingerprintServiceWrapper> service_wrapper_;
};

TEST_F(FingerprintServiceTest, SendFingerprintTouch) {
    Fingerprint request;
    request.set_istouching(true);
    request.set_touchid(123);

    int64_t received_value = 0;
    auto cb = android::base::eventing::MakeScopedCallback(
            sensor_, [&received_value](int64_t val) { received_value = val; });

    Empty reply;
    auto context = getContextWithTimeout();
    ASSERT_GRPC_STATUS(mStub->sendFingerprint(context.get(), request, &reply));

    EXPECT_EQ(received_value, 123);
    EXPECT_EQ(sensor_.GetValue(), 123);
}

TEST_F(FingerprintServiceTest, SendFingerprintRemove) {
    Fingerprint request;
    request.set_istouching(false);

    int64_t received_value = 0;
    auto cb = android::base::eventing::MakeScopedCallback(
            sensor_, [&received_value](int64_t val) { received_value = val; });

    Empty reply;
    auto context = getContextWithTimeout();
    ASSERT_GRPC_STATUS(mStub->sendFingerprint(context.get(), request, &reply));

    EXPECT_EQ(received_value, goldfish::avd_universe::fingerprint::kReleaseEvent);
    EXPECT_EQ(sensor_.GetValue(), goldfish::avd_universe::fingerprint::kReleaseEvent);
}

}  // namespace android::emulation::control
