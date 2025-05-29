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
#pragma once
#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include <chrono>
#include <memory>

#include "hardware/generic/goldfish/emulator/grpc/services/emulator_controller/proto/emulator_controller.grpc.pb.h"

namespace android::emulation::control {

using ::grpc::ServerContext;
using ::grpc::Status;
using ::grpc::StatusCode;

/**
 * @brief Base class for gRPC service tests.
 *
 * This class provides a convenient way to set up and tear down a gRPC server
 * and client for testing services that implement the
 * `EmulatorController::Service` interface.
 *
 * It handles the common boilerplate needed for creating a server, a client
 * channel, and a stub, allowing derived test classes to focus on the
 * specific logic of their service.
 *
 * **How to Use:**
 *
 * 1.  **Derive from `GrcpServiceTest`:** Create a new test fixture class that
 *     inherits from `GrcpServiceTest`.
 *
 * 2.  **Implement `getService()`:** Override the `getService()` method to return
 *     a pointer to your service instance, which implements
 *     `EmulatorController::Service`.
 *
 * 3.  **Use `mStub`:** The `mStub` member variable is an instance of
 *     `EmulatorController::Stub` that you can use to make gRPC calls to the
 *     server in your test.
 *
 * 4. **Use getContextWithTimeout:** Use the helper method `getContextWithTimeout()` to get a
 * client context with a default timeout, or provide your own timeout duration.
 *
 * 5. **Write tests:** Write standard googletest using the created client and server.
 *
 * **Example:**
 *
 * ```cpp
 * class MyServiceTest : public GrcpServiceTest {
 *  protected:
 *   void SetUp() override {
 *       // Add custom setup logic here (if needed).
 *       // ...
 *       GrcpServiceTest::SetUp(); // call the parent setup.
 *   }
 *
 *   EmulatorController::Service* getService() override {
 *     return &myService; // Return your service instance here.
 *   }
 *
 *   // Add your member variables (e.g., service instances, etc)
 *   MyServiceImpl myService;
 * };
 *
 * TEST_F(MyServiceTest, MyMethodTest) {
 *   // Use mStub to call your service.
 *   MyRequest request;
 *   MyResponse response;
 *   auto context = getContextWithTimeout();
 *   grpc::Status status = mStub->MyMethod(context.get(), request, &response);
 *   ASSERT_TRUE(status.ok());
 *   // Add your assertions
 *   // ...
 * }
 * ```
 */
class GrcpServiceTest : public ::testing::Test {
  protected:
    /**
     * @brief Sets up the gRPC server and client for each test.
     *
     * This method:
     * - Creates a gRPC server builder.
     * - Registers the service returned by `getService()` with the builder.
     * - Starts a server listening on an ephemeral port ("localhost:0").
     * - Creates a gRPC channel connected to the server.
     * - Creates a client stub (`EmulatorController::Stub`) using the channel.
     * - Stores the ephemeral port in the `mPort` member variable.
     */
    void SetUp() override {
        grpc::ServerBuilder builder;
        builder.RegisterService(getService());
        builder.AddListeningPort("localhost:0", grpc::InsecureServerCredentials(), &mPort);

        mServer = builder.BuildAndStart();
        mChannel = grpc::CreateChannel("localhost:" + std::to_string(mPort),
                                       grpc::InsecureChannelCredentials());
        mStub = EmulatorController::NewStub(mChannel);
    }

    /**
     * @brief Tears down the gRPC server and client after each test.
     *
     * This method:
     * - Shuts down the server.
     * - Waits for the server to finish.
     */
    void TearDown() override {
        // Note: We only give the server 50ms to shutdown to make sure we do not have
        // to block and wait for server shutdown. We should be safely to do so as we do
        // not expect there to be any ongoing requests.
        auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(50);
        mServer->Shutdown(deadline);
        mServer->Wait();
    }

    /**
     * @brief Returns a pointer to the service instance under test.
     *
     * Derived test classes must implement this method to return the
     * specific service instance they want to test.
     *
     * @return A pointer to the service instance.
     */
    virtual EmulatorController::Service* getService() = 0;

  protected:
    /**
     * @brief Gets a gRPC client context with a timeout.
     *
     * This is a helper method that creates a `grpc::ClientContext` and sets
     * a deadline for the operation. The default timeout is 500ms, but it
     * can be overridden.
     *
     * @param timeout The timeout duration for the gRPC operation.
     * @return A unique pointer to a `grpc::ClientContext`.
     */
    std::unique_ptr<grpc::ClientContext> getContextWithTimeout(
            std::chrono::milliseconds timeout = std::chrono::seconds(10)) {
        auto context = std::make_unique<grpc::ClientContext>();
        std::chrono::system_clock::time_point deadline = std::chrono::system_clock::now() + timeout;
        context->set_deadline(deadline);
        return context;
    }

    /// The gRPC server.
    std::unique_ptr<grpc::Server> mServer;
    /// The gRPC channel.
    std::shared_ptr<grpc::Channel> mChannel;
    /// The gRPC client stub.
    std::unique_ptr<EmulatorController::Stub> mStub;
    /// The port the server is listening on.
    int mPort;
};

/**
 * @brief Asserts that a gRPC status is OK, providing detailed error information if not.
 *
 * This macro checks if the given gRPC status is OK. If the status is not OK,
 * it fails the test with a detailed error message including the error code and
 * error message from the gRPC status.
 *
 * @param grpcStatus The gRPC status to check.
 */
#define ASSERT_GRPC_STATUS(grpcStatus)                                                   \
    do {                                                                                 \
        const ::grpc::Status& status = (grpcStatus);                                     \
        GTEST_TEST_BOOLEAN_(status.ok(), #grpcStatus, false, true, GTEST_FATAL_FAILURE_) \
                << "gRPC status failed: " << status.error_code()                         \
                << ", message: " << status.error_message();                              \
    } while (0)

}  // namespace android::emulation::control
