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
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define AF_LOCAL 0
#else
#include <sys/socket.h>
#endif

namespace cuttlefish {

struct FileInstance;

struct SharedFD {
  static SharedFD Accept(const FileInstance& listener, struct sockaddr* addr,
                         socklen_t* addrlen);
  static SharedFD Accept(const FileInstance& listener);

  static SharedFD SocketLocalClient(int port);
  static SharedFD SocketLocalClient(const std::string& name, bool is_abstract, int in_type);
  static SharedFD SocketLocalServer(int port);

  static bool Pipe(SharedFD* fd0, SharedFD* fd1);
  static bool SocketPair(int domain, int type, int protocol, SharedFD* fd0, SharedFD* fd1);

  SharedFD();
  SharedFD(const SharedFD&) = default;
  SharedFD(SharedFD&&) = default;
  SharedFD& operator=(const SharedFD&) = default;
  SharedFD& operator=(SharedFD&&) = default;

  bool operator==(const SharedFD& rhs) const { return value_ == rhs.value_; }
  bool operator<(const SharedFD& rhs) const { return value_ < rhs.value_; }
  FileInstance* operator->() const { return value_.get(); }
  const FileInstance& operator*() const { return *value_; }

 private:
  explicit SharedFD(int fd);
  explicit SharedFD(std::shared_ptr<FileInstance>);

  std::shared_ptr<FileInstance> value_;
};

struct FileInstance {
 private:
  struct Private {};
  friend SharedFD;

 public:
  FileInstance(Private) {}
  FileInstance(int fd, Private) : fd_(fd) {}
  ~FileInstance() { Close(); }

  bool IsOpen() const { return fd_ != -1; }
  void Close();

  int Bind(const struct sockaddr* addr, socklen_t addrlen);
  int Listen(int backlog);
  int Connect(const struct sockaddr* addr, socklen_t addrlen);
  void Set(fd_set* dest, int* max_index) const;
  bool IsSet(fd_set* in) const;
  ssize_t Write(const void* buf, size_t count);
  ssize_t Read(void* buf, size_t count);

  std::string StrError() const { return "error"; };

  FileInstance(const FileInstance&) = delete;
  FileInstance(FileInstance&&) = delete;
  FileInstance& operator =(const FileInstance&) = delete;
  FileInstance& operator =(FileInstance&&) = delete;

 private:
  std::shared_ptr<FileInstance> Accept(struct sockaddr* addr, socklen_t* addrlen) const;

  int fd_ = -1;
};

}  // namespace cuttlefish