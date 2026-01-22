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
#include <thread>
#include <cstring>

// clang-format off
// IWYU pragma: begin_keep
#ifdef _WIN32
#include <ws2def.h>
#else
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
    WSAStartup(MAKEWORD(2,2), &wsaData);
  }

  ~WsaInitializer() {
    WSACleanup();
  }
};

int CreateSocketImpl(int d, int t, int p) {
  static WsaInitializer wsa_initializer;
  return ::socket(d, t, p);
}

int BindSocketImpl(int s, const sockaddr *addr, socklen_t addrlen) {
  return ::bind(s, addr, addrlen);
}

int ListenSocketImpl(int s, int backlog) {
  return ::listen(s, backlog);
}

int AcceptSocketImpl(int s, sockaddr *addr, socklen_t* addrlen) {
  return ::accept(s, addr, addrlen);
}

int ConnectSocketImpl(int s, const sockaddr *addr, socklen_t addrlen) {
  return ::connect(s, addr, addrlen);
}

int SendSocketImpl(const int s, const void* buf, int len, int flags) {
  return ::send(s, static_cast<const char*>(buf), len, flags);
}

int RecvSocketImpl(const int s, void* buf, int len, int flags) {
  return ::recv(s, static_cast<char*>(buf), len, flags);
}

int SelectSocketImpl(int nfds, fd_set *readfds,
                     fd_set *writefds,
                     fd_set *exceptfds,
                     struct timeval *timeout) {
  return ::select(nfds, readfds, writefds, exceptfds, timeout);
}

void CloseSocketImpl(const int s) {
  ::closesocket(s);
}

#else  // _WIN32

int CreateSocketImpl(int d, int t, int p) {
  return TEMP_FAILURE_RETRY(::socket(d, t, p));
}

int BindSocketImpl(int s, const sockaddr *addr, socklen_t addrlen) {
  return TEMP_FAILURE_RETRY(::bind(s, addr, addrlen));
}

int ListenSocketImpl(int s, int backlog) {
  return TEMP_FAILURE_RETRY(::listen(s, backlog));
}

int AcceptSocketImpl(int s, sockaddr *addr, socklen_t* addrlen) {
  return TEMP_FAILURE_RETRY(::accept(s, addr, addrlen));
}

int ConnectSocketImpl(int s, const sockaddr *addr, socklen_t addrlen) {
  return TEMP_FAILURE_RETRY(::connect(s, addr, addrlen));
}

int SendSocketImpl(const int s, const void* buf, int len, int /*flags*/) {
  return TEMP_FAILURE_RETRY(::write(s, buf, len));
}

int RecvSocketImpl(const int s, void* buf, int len, int /*flags*/) {
  return TEMP_FAILURE_RETRY(::read(s, buf, len));
}

int SelectSocketImpl(int nfds, fd_set *readfds,
                     fd_set *writefds,
                     fd_set *exceptfds,
                     struct timeval *timeout) {
  return TEMP_FAILURE_RETRY(::select(nfds, readfds, writefds, exceptfds, timeout));
}

void CloseSocketImpl(const int s) {
  ::close(s);
}

#endif  // _WIN32

int CreateLocalServerImpl(const int port, const bool is_ipv6) {
  const int s = CreateSocketImpl(is_ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
  if (s < 0) {
    return s;
  }

  int bind_result;
  if (is_ipv6) {
    struct sockaddr_in6 in6 = {};
    in6.sin6_family = AF_INET6;
    in6.sin6_port = htons(port);
    in6.sin6_addr = in6addr_loopback;
    bind_result = BindSocketImpl(s, reinterpret_cast<const sockaddr *>(&in6), sizeof(in6));
  } else {
    struct sockaddr_in in4 = {};
    in4.sin_family = AF_INET;
    in4.sin_port = htons(port);
    in4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind_result = BindSocketImpl(s, reinterpret_cast<const sockaddr *>(&in4), sizeof(in4));
  }

  if (bind_result < 0) {
    CloseSocketImpl(s);
    return -1;
  }

  if (ListenSocketImpl(s, 3) < 0) {
    CloseSocketImpl(s);
    return -1;
  }

  return s;
}

int CreateLocalServerImpl(const int port) {
  int s = CreateLocalServerImpl(port, true);
  if (s >= 0) {
    return s;
  }

  return CreateLocalServerImpl(port, false);
}

int CreateLocalClientImpl(const int port, const bool is_ipv6) {
  const int s = CreateSocketImpl(is_ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
  if (s < 0) {
    return s;
  }

  int connect_result;
  if (is_ipv6) {
    struct sockaddr_in6 in6 = {};
    in6.sin6_family = AF_INET6;
    in6.sin6_port = htons(port);
    in6.sin6_addr = in6addr_loopback;
    connect_result = ConnectSocketImpl(s, reinterpret_cast<const sockaddr *>(&in6), sizeof(in6));
  } else {
    struct sockaddr_in in4 = {};
    in4.sin_family = AF_INET;
    in4.sin_port = htons(port);
    in4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    connect_result = ConnectSocketImpl(s, reinterpret_cast<const sockaddr *>(&in4), sizeof(in4));
  }

  if (connect_result < 0) {
    CloseSocketImpl(s);
    return -1;
  }

  return s;
}

#ifdef _WIN32
bool MakeSocketPipe(int fds[2]) {
  const int s = CreateLocalServerImpl(0);
  if (s < 0) {
    return false;
  }

  struct sockaddr_storage addr;
  socklen_t addrlen = sizeof(addr);
  if (::getsockname(s, reinterpret_cast<struct sockaddr*>(&addr), &addrlen)) {
    return false;
  }

  int c1 = CreateSocketImpl(addr.ss_family, SOCK_STREAM, 0);
  if (c1 < 0) {
    CloseSocketImpl(s);
    return false;
  }

  if (ConnectSocketImpl(c1, reinterpret_cast<struct sockaddr*>(&addr), addrlen)) {
    CloseSocketImpl(s);
    CloseSocketImpl(c1);
    return false;
  }

  fds[0] = AcceptSocketImpl(s, nullptr, nullptr);
  if (fds[0] < 0) {
    CloseSocketImpl(s);
    CloseSocketImpl(c1);
    return false;
  }

  fds[1] = c1;
  return true;
}
#else  // _WIN32
bool MakeSocketPipe(int fds[2]) {
#ifdef __linux__
  return ::pipe2(fds, O_CLOEXEC) != -1;
#else
  return ::pipe(fds) != -1;
#endif
}
#endif // _WIN32

}  // namespace

SharedFD::SharedFD(std::shared_ptr<FileInstance> val) : value_(std::move(val)) {}

SharedFD::SharedFD(const int fd) : SharedFD(std::make_shared<FileInstance>(fd, FileInstance::Private())) {}

SharedFD::SharedFD() : SharedFD(-1) {}

SharedFD SharedFD::Accept(const FileInstance& listener, struct sockaddr* addr,
                          socklen_t* addrlen) {
  return SharedFD(listener.Accept(addr, addrlen));
}

SharedFD SharedFD::Accept(const FileInstance& listener) {
  return SharedFD(listener.Accept(nullptr, nullptr));
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

SharedFD SharedFD::SocketLocalClient(const std::string& name, bool /*is_abstract*/, int /*in_type*/) {
  using namespace std::literals::string_view_literals;
  constexpr auto kPrefix = "modem_simulator"sv;

  std::string_view namev = name;
  if (!namev.starts_with(kPrefix)) {
    return {};
  }

  namev.remove_prefix(kPrefix.size());
  const auto port = goldfish::parsing::fromChars<int>(namev);
  if (!port) {
    return {};
  }

  return SocketLocalClient(*port);
}

SharedFD SharedFD::SocketLocalServer(const int port) {
  return SharedFD(CreateLocalServerImpl(port));
}

bool SharedFD::Pipe(SharedFD* fd0, SharedFD* fd1) {
  int fds[2];
  if (!MakeSocketPipe(fds)) {
    return false;
  }

  *fd0 = SharedFD(fds[0]);
  *fd1 = SharedFD(fds[1]);
  return true;
}

bool SharedFD::SocketPair(int /*domain*/, int /*type*/, int /*protocol*/,
                          SharedFD* fd0, SharedFD* fd1) {
  return Pipe(fd0, fd1);
}

/**********************************************************************************************/

void FileInstance::Close() {
  if (IsOpen()) {
    CloseSocketImpl(fd_);
    fd_ = -1;
  }
}

int FileInstance::Bind(const struct sockaddr* addr, socklen_t addrlen) {
  return BindSocketImpl(fd_, addr, addrlen);
}

int FileInstance::Listen(int backlog) {
  return ListenSocketImpl(fd_, backlog);
}

std::shared_ptr<FileInstance> FileInstance::Accept(struct sockaddr* addr, socklen_t* addrlen) const {
  return std::make_shared<FileInstance>(AcceptSocketImpl(fd_, addr, addrlen), Private());
}

int FileInstance::Connect(const struct sockaddr* addr, socklen_t addrlen) {
  return ConnectSocketImpl(fd_, addr, addrlen);
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

int Select(SharedFDSet* read_set, SharedFDSet* write_set,
           SharedFDSet* error_set, struct timeval* timeout) {
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
