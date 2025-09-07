#include <iostream>

#include "absl/strings/str_format.h"

#include "goldfish/async/async_socket.h"

namespace goldfish::async {
std::ostream& operator<<(std::ostream& os, const AsyncSocket& socket) {
    return os << absl::StreamFormat("%v", socket);
}
}  // namespace goldfish::async
