#pragma once

#include <memory>
#include <vector>

namespace grpc {
class Service;
}

namespace goldfish::grpc {

// Registers the QEMU device TYPE_WEBRTC
void webrtc_register_types();

// Returns all registered WebRTC services (v1 and v2) if realized, else empty.
std::vector<std::shared_ptr<::grpc::Service>> WebrtcGetServices();

}  // namespace goldfish::grpc
