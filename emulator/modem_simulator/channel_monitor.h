//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

#include "common/libs/fs/shared_fd.h"

class ModemServiceTest;

namespace cuttlefish {

class ModemSimulator;

enum ModemSimulatorExitCodes : int {
  kSuccess = 0,
  kSelectError = 1,
  kServerError = 2,
};

class ClientId {
 public:
  ClientId() : id_(GetNextId()) {}
  bool operator==(const ClientId& rhs) const { return id_ == rhs.id_; }
 private:
  static size_t GetNextId();
  size_t id_;
};

/**
 * Client object managed by ChannelMonitor, contains two types, the RIL client
 * and the remote client of other cuttlefish instance.
 * Due to std::mutex does not implement its copy and operate= constructors, it
 * can't be stored in standard contains (vector, map), so use the point instead.
 */
class Client {
 public:
  enum ClientType { RIL, REMOTE };

  ~Client() = default;
  Client(SharedFD fd);
  Client(SharedFD read, SharedFD write);
  Client(SharedFD fd, ClientType client_type);
  Client(SharedFD read, SharedFD write, ClientType client_type);
  Client(const Client& client) = delete;
  Client(Client&& client) = delete;

  Client& operator=(Client&& other) = delete;

  bool operator==(const Client& rhs) const { return id_ == rhs.id_; }

  void SendCommandResponse(std::string response) const ABSL_LOCKS_EXCLUDED(write_mutex_);
  void SendCommandResponse(const std::vector<std::string>& responses) const ABSL_LOCKS_EXCLUDED(write_mutex_);

  ClientId Id() const { return id_; }
  ClientType Type() const { return type; }
  void Close();

 private:
  friend class ChannelMonitor;
  friend class ::ModemServiceTest;

  const ClientId id_;
  const ClientType type = RIL;
  const SharedFD client_read_fd_;
  const SharedFD client_write_fd_ ABSL_GUARDED_BY(write_mutex_);
  std::string incomplete_command;
  mutable absl::Mutex write_mutex_;
  bool first_read_command_;  // Only used when ClientType::REMOTE
  bool is_valid = true;
};

class ChannelMonitor {
 public:
  ChannelMonitor(ModemSimulator& modem, cuttlefish::SharedFD server);
  ~ChannelMonitor();

  ChannelMonitor(const ChannelMonitor&) = delete;
  ChannelMonitor& operator=(const ChannelMonitor&) = delete;

  std::optional<ClientId> SetRemoteClient(SharedFD client, bool is_accepted);
  void SendRemoteCommand(ClientId client, const std::string& response);
  void CloseRemoteConnection(ClientId client) ABSL_LOCKS_EXCLUDED(remote_clients_mutex_);

  // For modem services to send unsolicited commands
  void SendUnsolicitedCommand(const std::string& response);

 private:
  ModemSimulator& modem_;
  std::thread monitor_thread_;
  cuttlefish::SharedFD server_;
  cuttlefish::SharedFD read_pipe_;
  cuttlefish::SharedFD write_pipe_;
  std::vector<std::unique_ptr<Client>> clients_;
  std::vector<std::unique_ptr<Client>> remote_clients_ ABSL_GUARDED_BY(remote_clients_mutex_);
  mutable absl::Mutex remote_clients_mutex_;

  void AcceptIncomingConnection();
  void OnClientSocketClosed(int sock);
  bool ReadCommand(Client& client);

  void MonitorLoop();
};

}  // namespace cuttlefish
