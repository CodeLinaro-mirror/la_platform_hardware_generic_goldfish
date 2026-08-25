#pragma once

#include <memory>
namespace grpc {
class Service;
}

namespace goldfish::grpc {

// Registers the QEMU device TYPE_WEBRTC
void webrtc_register_types();

// Returns the singleton WebRTC service if the webrtc device has been realized, else nullptr.
std::shared_ptr<::grpc::Service> WebrtcGetService();

}  // namespace goldfish::grpc
