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

#include "common/libs/fs/shared_fd.h"

#include <chrono>
#include <cstring>
#include <thread>

#include "absl/strings/str_format.h"

// clang-format off
// IWYU pragma: begin_keep
#ifdef _WIN32
#include <ws2def.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#endif
// IWYU pragma: end_keep
// clang-format on

#include "common/libs/fs/shared_select.h"
#include "goldfish/os/temp_failure_retry.h"
#include "goldfish/parsing/from_chars.h"

namespace cuttlefish {

namespace impl {
void MarkAll(const SharedFDSet& input, fd_set* dest, int* max_index) {
    for (SharedFDSet::const_iterator it = input.begin(); it != input.end(); ++it) {
        (*it)->Set(dest, max_index);
    }
}

void CheckMarked(fd_set* in_out_mask, SharedFDSet* in_out_set) {
    if (!in_out_set) {
        return;
    }

    SharedFDSet save;
    save.swap(in_out_set);
    for (SharedFDSet::iterator it = save.begin(); it != save.end(); ++it) {
        if ((*it)->IsSet(in_out_mask)) {
            in_out_set->Set(*it);
        }
    }
}
}  // namespace impl

namespace {
#ifdef _WIN32

struct WsaInitializer {
    WsaInitializer() {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }

    ~WsaInitializer() { WSACleanup(); }
};

int CreateSocketImpl(int d, int t, int p) {
    static WsaInitializer wsa_initializer;
    return ::socket(d, t, p);
}

int BindSocketImpl(int s, const sockaddr* addr, socklen_t addrlen) {
    return ::bind(s, addr, addrlen);
}

int ListenSocketImpl(int s, int backlog) {
    return ::listen(s, backlog);
}

int AcceptSocketImpl(int s, sockaddr* addr, socklen_t* addrlen) {
    return ::accept(s, addr, addrlen);
}

int ConnectSocketImpl(int s, const sockaddr* addr, socklen_t addrlen) {
    return ::connect(s, addr, addrlen);
}

int SendSocketImpl(const int s, const void* buf, int len, int flags) {
    return ::send(s, static_cast<const char*>(buf), len, flags);
}

int RecvSocketImpl(const int s, void* buf, int len, int flags) {
    return ::recv(s, static_cast<char*>(buf), len, flags);
}

int SelectSocketImpl(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds,
                     struct timeval* timeout) {
    return ::select(nfds, readfds, writefds, exceptfds, timeout);
}

void CloseSocketImpl(const int s) {
    ::closesocket(s);
}

#else  // _WIN32

int CreateSocketImpl(int d, int t, int p) {
    return TEMP_FAILURE_RETRY(::socket(d, t, p));
}

int BindSocketImpl(int s, const sockaddr* addr, socklen_t addrlen) {
    return TEMP_FAILURE_RETRY(::bind(s, addr, addrlen));
}

int ListenSocketImpl(int s, int backlog) {
    return TEMP_FAILURE_RETRY(::listen(s, backlog));
}

int AcceptSocketImpl(int s, sockaddr* addr, socklen_t* addrlen) {
    return TEMP_FAILURE_RETRY(::accept(s, addr, addrlen));
}

int ConnectSocketImpl(int s, const sockaddr* addr, socklen_t addrlen) {
    return TEMP_FAILURE_RETRY(::connect(s, addr, addrlen));
}

int SendSocketImpl(const int s, const void* buf, int len, int /*flags*/) {
    return TEMP_FAILURE_RETRY(::write(s, buf, len));
}

int RecvSocketImpl(const int s, void* buf, int len, int /*flags*/) {
    return TEMP_FAILURE_RETRY(::read(s, buf, len));
}

int SelectSocketImpl(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds,
                     struct timeval* timeout) {
    return TEMP_FAILURE_RETRY(::select(nfds, readfds, writefds, exceptfds, timeout));
}

void CloseSocketImpl(const int s) {
    ::close(s);
}

#endif  // _WIN32

int CreateServerImpl(const struct sockaddr* addr, const socklen_t addrlen, const int backlog) {
    const int s = CreateSocketImpl(addr->sa_family, SOCK_STREAM, 0);
    if (s < 0) {
        return s;
    }

    if (BindSocketImpl(s, addr, addrlen) < 0) {
        CloseSocketImpl(s);
        return -1;
    }

    if (ListenSocketImpl(s, backlog) < 0) {
        CloseSocketImpl(s);
        return -1;
    }

    return s;
}

int CreateLocalServerImpl(const int port, const bool is_ipv6, const int backlog) {
    if (is_ipv6) {
        struct sockaddr_in6 in6 = {};
        in6.sin6_family = AF_INET6;
        in6.sin6_port = htons(port);
        in6.sin6_addr = in6addr_loopback;

        return CreateServerImpl(reinterpret_cast<const sockaddr*>(&in6), sizeof(in6), backlog);
    } else {
        struct sockaddr_in in4 = {};
        in4.sin_family = AF_INET;
        in4.sin_port = htons(port);
        in4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        return CreateServerImpl(reinterpret_cast<const sockaddr*>(&in4), sizeof(in4), backlog);
    }
}

int CreateLocalServerImpl(const int port, const int backlog) {
    int s = CreateLocalServerImpl(port, /*is_ipv6=*/true, backlog);
    if (s >= 0) {
        return s;
    }

    return CreateLocalServerImpl(port, /*is_ipv6=*/false, backlog);
}

int CreateClientImpl(const struct sockaddr* addr, const socklen_t addrlen) {
    const int s = CreateSocketImpl(addr->sa_family, SOCK_STREAM, 0);
    if (s < 0) {
        return s;
    }

    if (ConnectSocketImpl(s, addr, addrlen) < 0) {
        CloseSocketImpl(s);
        return -1;
    }

    return s;
}

int CreateLocalClientImpl(const int port, const bool is_ipv6) {
    if (is_ipv6) {
        struct sockaddr_in6 in6 = {};
        in6.sin6_family = AF_INET6;
        in6.sin6_port = htons(port);
        in6.sin6_addr = in6addr_loopback;

        return CreateClientImpl(reinterpret_cast<const sockaddr*>(&in6), sizeof(in6));
    } else {
        struct sockaddr_in in4 = {};
        in4.sin_family = AF_INET;
        in4.sin_port = htons(port);
        in4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        return CreateClientImpl(reinterpret_cast<const sockaddr*>(&in4), sizeof(in4));
    }
}
}  // namespace

SharedFD::SharedFD(std::shared_ptr<FileInstance> val) : value_(std::move(val)) {}

SharedFD::SharedFD(const int fd)
        : SharedFD(std::make_shared<FileInstance>(fd, FileInstance::Private())) {}

SharedFD::SharedFD() : SharedFD(-1) {}

SharedFD SharedFD::Accept(const FileInstance& listener, struct sockaddr* addr, socklen_t* addrlen) {
    return SharedFD(listener.Accept(addr, addrlen));
}

SharedFD SharedFD::Accept(const FileInstance& listener) {
    return SharedFD(listener.Accept(nullptr, nullptr));
}

SharedFD SharedFD::SocketClient(const SharedFD& server) {
    struct sockaddr_storage addr;
    socklen_t addrlen;

    if (!server->Endpoint(&addr, &addrlen)) {
        return {};
    }

    return SocketClient(reinterpret_cast<const struct sockaddr*>(&addr), addrlen);
}

SharedFD SharedFD::SocketClient(const struct sockaddr* addr, socklen_t addrlen) {
    return SharedFD(CreateClientImpl(addr, addrlen));
}

SharedFD SharedFD::SocketLocalClient(const int port) {
    int c = CreateLocalClientImpl(port, true);
    if (c >= 0) {
        return SharedFD(c);
    }

    c = CreateLocalClientImpl(port, false);
    if (c >= 0) {
        return SharedFD(c);
    }

    return {};
}

SharedFD SharedFD::SocketLocalClient(const std::string& name, bool /*is_abstract*/,
                                     int /*in_type*/) {
    using namespace std::literals::string_view_literals;
    constexpr auto kPrefix = "modem_simulator"sv;

    std::string_view namev = name;
    if (!namev.starts_with(kPrefix)) {
        return {};
    }

    namev.remove_prefix(kPrefix.size());
    const auto port = goldfish::parsing::FromChars<int>(namev);
    if (!port) {
        return {};
    }

    return SocketLocalClient(*port);
}

SharedFD SharedFD::SocketLocalServer(const int port) {
    constexpr int kServerBacklog = 3;
    return SharedFD(CreateLocalServerImpl(port, kServerBacklog));
}

SharedFD SharedFD::SocketLocalServer() {
    return SocketLocalServer(0);
}

bool SharedFD::Pipe(SharedFD* consumer, SharedFD* producer) {
#ifdef _WIN32
    return SocketPair(0, 0, 0, consumer, producer);
#else  // _WIN32

    int ret;
    int fds[2];
#ifdef __linux__
    ret = ::pipe2(fds, O_CLOEXEC);
#else
    ret = ::pipe(fds);
#endif

    if (ret) {
        return false;
    }

    *consumer = SharedFD(fds[0]);
    *producer = SharedFD(fds[1]);
    return true;
#endif  // _WIN32
}

bool SharedFD::SocketPair(const int domain, const int type, const int protocol, SharedFD* fd0,
                          SharedFD* fd1) {
#ifdef _WIN32
    (void)domain;
    (void)type;
    (void)protocol;

    const SharedFD server = SocketLocalServer();
    if (!server) {
        return false;
    }
    SharedFD client = SocketClient(server);
    if (!client) {
        return false;
    }
    SharedFD conn = SharedFD::Accept(*server);
    if (!conn) {
        return false;
    }

    *fd0 = std::move(client);
    *fd1 = std::move(conn);
    return true;
#else
    int fds[2];
    if (::socketpair(domain, type, protocol, fds)) {
        return false;
    }

    *fd0 = SharedFD(fds[0]);
    *fd1 = SharedFD(fds[1]);
    return true;
#endif
}

/**********************************************************************************************/

void FileInstance::Close() {
    if (IsOpen()) {
        CloseSocketImpl(fd_);
        fd_ = -1;
    }
}

std::shared_ptr<FileInstance> FileInstance::Accept(struct sockaddr* addr,
                                                   socklen_t* addrlen) const {
    return std::make_shared<FileInstance>(AcceptSocketImpl(fd_, addr, addrlen), Private());
}

void FileInstance::Set(fd_set* dest, int* max_index) const {
    if (IsOpen()) {
        if (fd_ >= *max_index) {
            *max_index = fd_ + 1;
        }

        FD_SET(fd_, dest);
    }
}

bool FileInstance::IsSet(fd_set* in) const {
    return IsOpen() && FD_ISSET(fd_, in);
}

ssize_t FileInstance::Write(const void* buf, size_t count) {
    return SendSocketImpl(fd_, buf, count, 0);
}

ssize_t FileInstance::Read(void* buf, size_t count) {
    return RecvSocketImpl(fd_, buf, count, 0);
}

bool FileInstance::Endpoint(struct sockaddr_storage* addr, socklen_t* addrlen) const {
    *addrlen = sizeof(*addr);
    return ::getsockname(fd_, reinterpret_cast<struct sockaddr*>(addr), addrlen) == 0;
}

std::string FileInstance::ChardevEndpoint() const {
    struct sockaddr_storage addr;
    socklen_t addrlen;

    if (!Endpoint(&addr, &addrlen)) {
        return {};
    }

    switch (addr.ss_family) {
    case AF_INET: {
        const struct sockaddr_in* addr4 = reinterpret_cast<const struct sockaddr_in*>(&addr);

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr4->sin_addr, ip_str, sizeof(ip_str));
        return absl::StrFormat("port=%d,host=%s,ipv4=on", ntohs(addr4->sin_port), ip_str);
    }

    case AF_INET6: {
        const struct sockaddr_in6* addr6 = reinterpret_cast<const struct sockaddr_in6*>(&addr);

        char ip_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &addr6->sin6_addr, ip_str, sizeof(ip_str));

        return absl::StrFormat("port=%d,host=%s,ipv6=on", ntohs(addr6->sin6_port), ip_str);
    }
    }

    return {};
}

int Select(SharedFDSet* read_set, SharedFDSet* write_set, SharedFDSet* error_set,
           struct timeval* timeout) {
    int max_index = 0;

    fd_set readfds;
    FD_ZERO(&readfds);
    if (read_set) {
        impl::MarkAll(*read_set, &readfds, &max_index);
    }

    fd_set writefds;
    FD_ZERO(&writefds);
    if (write_set) {
        impl::MarkAll(*write_set, &writefds, &max_index);
    }

    fd_set errorfds;
    FD_ZERO(&errorfds);
    if (error_set) {
        impl::MarkAll(*error_set, &errorfds, &max_index);
    }

    int rval = SelectSocketImpl(max_index, &readfds, &writefds, &errorfds, timeout);
    if (rval < 0) {
        return rval;
    }

    impl::CheckMarked(&readfds, read_set);
    impl::CheckMarked(&writefds, write_set);
    impl::CheckMarked(&errorfds, error_set);
    return rval;
}

}  // namespace cuttlefish
