#include <google/protobuf/empty.pb.h>

#include <iostream>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"

#include "android/emulation/control/emulator_grpc_client.h"
#include "emulator_controller.grpc.pb.h"
#include "emulator_controller.pb.h"

using android::emulation::control::BlockingEmulatorGrpcClient;
using android::emulation::control::ConnectionState;
using android::emulation::control::EmulatorController;
using android::emulation::control::EmulatorGrpcClientBuilder;
using android::emulation::control::Endpoint;
using android::emulation::control::Notification;
using google::protobuf::Empty;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <port>" << std::endl;
        return 1;
    }

    std::string port = argv[1];
    std::string target = "localhost:" + port;

    Endpoint endpoint;
    endpoint.set_target(target);

    auto client_or_status = EmulatorGrpcClientBuilder().WithEndpoint(endpoint).BuildBlocking();

    if (!client_or_status.ok()) {
        std::cerr << "Failed to build client: " << client_or_status.status() << std::endl;
        return 1;
    }

    auto client = std::move(*client_or_status);
    auto status = client->Connect(absl::Seconds(60));
    if (!status.ok()) {
        std::cerr << "Failed to connect to " << target << ": " << status << std::endl;
        return 1;
    }
    std::cout << "Connected to " << target << std::endl;

    auto stub_or_status = client->Stub<EmulatorController>();
    if (!stub_or_status.ok()) {
        std::cerr << "Failed to get stub: " << stub_or_status.status() << std::endl;
        return 1;
    }

    auto stub = std::move(*stub_or_status);
    auto context_or_status = client->NewContext();
    if (!context_or_status.ok()) {
        std::cerr << "Failed to create context: " << context_or_status.status() << std::endl;
        return 1;
    }

    auto context = std::move(*context_or_status);
    Empty request;
    Notification response;

    std::cout << "Listening for notifications on " << target << "..." << std::endl;
    auto reader = stub->streamNotification(context.get(), request);
    while (reader->Read(&response)) {
        std::cout << "Received notification: " << response.DebugString() << std::endl;
    }

    auto grpc_status = reader->Finish();
    if (!grpc_status.ok()) {
        std::cerr << "Stream finished with error: " << grpc_status.error_message() << std::endl;
        return 1;
    }

    return 0;
}
