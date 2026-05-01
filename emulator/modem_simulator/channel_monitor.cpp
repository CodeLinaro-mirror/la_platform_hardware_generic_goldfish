
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

#include "host/commands/modem_simulator/channel_monitor.h"

#include <algorithm>
#include <atomic>

#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/strings/str_replace.h"

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_select.h"
#include "host/commands/modem_simulator/modem_simulator.h"

namespace cuttlefish {

constexpr int32_t kMaxCommandLength = 4096;

size_t ClientId::GetNextId() {
  static std::atomic<size_t> next_id;
  return next_id.fetch_add(1) + 1U;
}

Client::Client(SharedFD fd) : client_read_fd_(fd), client_write_fd_(fd) {}

Client::Client(SharedFD read, SharedFD write)
    : client_read_fd_(std::move(read)), client_write_fd_(std::move(write)) {}

Client::Client(SharedFD fd, ClientType client_type)
    : type(client_type), client_read_fd_(fd), client_write_fd_(fd) {}

Client::Client(SharedFD read, SharedFD write, ClientType client_type)
    : type(client_type),
      client_read_fd_(std::move(read)),
      client_write_fd_(std::move(write)) {}

void Client::SendCommandResponse(std::string response) const {
  if (response.empty()) {
    VLOG(1) << "Invalid response, ignore!";
    return;
  }

  if (response.back() != '\r') {
    response += '\r';
  }
  VLOG(2) << " AT< " << response;

  const absl::MutexLock lock(write_mutex_);
  WriteAll(client_write_fd_, response);
}

void Client::SendCommandResponse(
    const std::vector<std::string>& responses) const {
  for (auto& response : responses) {
    SendCommandResponse(response);
  }
}

void Client::Close() {
  const absl::MutexLock lock(write_mutex_);
  client_read_fd_->Close();
  client_write_fd_->Close();
  is_valid = false;
}

ChannelMonitor::ChannelMonitor(ModemSimulator& modem, SharedFD server)
    : modem_(modem), server_(std::move(server)) {
  if (!SharedFD::Pipe(&read_pipe_, &write_pipe_)) {
    LOG(ERROR) << "Unable to create pipe, ignore";
  }

  if (server_->IsOpen()) {
    monitor_thread_ = std::thread([this]() { MonitorLoop(); });
  }
}

std::optional<ClientId> ChannelMonitor::SetRemoteClient(SharedFD client, bool is_accepted) {
  auto remote_client = std::make_unique<Client>(client, Client::REMOTE);

  if (is_accepted) {
    // There may be new data from remote client before select.
    remote_client->first_read_command_ = true;
    if (!ReadCommand(*remote_client)) {
      return std::nullopt;
    }
  }

  auto id = remote_client->Id();
  remote_client->first_read_command_ = false;

  {
    const absl::MutexLock lock(remote_clients_mutex_);
    remote_clients_.push_back(std::move(remote_client));
    VLOG(1) << "added one remote client";
  }

  // Trigger monitor loop
  if (write_pipe_->IsOpen()) {
    write_pipe_->Write("OK", sizeof("OK"));
  } else {
    LOG(ERROR) << "Pipe created fail, can't trigger monitor loop";
  }

  return id;
}

void ChannelMonitor::AcceptIncomingConnection() {
  auto client_fd = SharedFD::Accept(*server_);
  if (!client_fd->IsOpen()) {
    LOG(ERROR) << "Error accepting connection on socket: " << client_fd->StrError();
  } else {
    auto client = std::make_unique<Client>(client_fd);
    VLOG(1) << "added one RIL client";

    clients_.push_back(std::move(client));
    if (clients_.size() == 1) {
      // The first connected client default to be the unsolicited commands channel
      modem_.OnFirstClientConnected();
    }
  }
}

bool ChannelMonitor::ReadCommand(Client& client) {
  std::vector<char> buffer(kMaxCommandLength);
  auto bytes_read = client.client_read_fd_->Read(buffer.data(), buffer.size());
  if (bytes_read <= 0) {
    if (errno == EAGAIN && client.type == Client::REMOTE &&
        client.first_read_command_) {
      LOG(ERROR) << "After read 'REM' from remote client, and before select "
          "no new data come.";
      return true;
    }

    VLOG(1) << "Error reading from client fd: " << client.client_read_fd_->StrError();
    return false;  // drop the client
  }

  std::string& incomplete_command = client.incomplete_command;

  // Add the incomplete command from the last read
  auto commands = std::string{incomplete_command.data()};
  commands.append(buffer.data());

  incomplete_command.clear();

  // Replacing '\n' with '\r'
  commands = absl::StrReplaceAll(commands, {{"\n", "\r"}});

  // Split into commands and dispatch
  size_t pos = 0, r_pos = 0;  // '\r' or '\n'
  while (r_pos != std::string::npos) {
    if (modem_.IsWaitingSmsPdu()) {
      r_pos = commands.find('\032', pos);  // In sms, find ctrl-z
    } else {
      r_pos = commands.find('\r', pos);
    }
    if (r_pos != std::string::npos) {
      auto command = commands.substr(pos, r_pos - pos);
      if (command.size() > 0) {  // "\r\r" ?
        VLOG(2) << "AT> " << command;
        modem_.DispatchCommand(client, command);
      }
      pos = r_pos + 1;  // Skip '\r'
    } else if (pos < commands.length()) {  // Incomplete command
      incomplete_command = commands.substr(pos);
    }
  }

  return true;
}

void ChannelMonitor::SendUnsolicitedCommand(const std::string& response) {
  if (!clients_.empty()) {
    // The first accepted client default to be unsolicited command channel?
    clients_.front()->SendCommandResponse(response);
  } else {
    VLOG(1) << "No client connected yet.";
  }
}

void ChannelMonitor::SendRemoteCommand(const ClientId clientId, const std::string& response) {
  const absl::MutexLock lock(remote_clients_mutex_);
  const auto i = std::find_if(
      remote_clients_.begin(), remote_clients_.end(),
      [clientId](const std::unique_ptr<Client>& client) {
          return client->Id() == clientId;
      });

  if (i != remote_clients_.end()) {
    (*i)->SendCommandResponse(response);
  } else {
    VLOG(1) << "Remote client has closed.";
  }
}

void ChannelMonitor::CloseRemoteConnection(const ClientId clientId) {
  {
    const absl::MutexLock lock(remote_clients_mutex_);
    const auto i = std::find_if(
        remote_clients_.begin(), remote_clients_.end(),
        [clientId](const std::unique_ptr<Client>& client) {
            return client->Id() == clientId;
        });

    if (i == remote_clients_.end()) {
      VLOG(1) << "Remote client has been erased.";
      return;
    }

    (*i)->Close();
  }

  // Trigger monitor loop
  if (write_pipe_->IsOpen()) {
    write_pipe_->Write("OK", sizeof("OK"));
    VLOG(1) << "asking to remove clients";
  } else {
    LOG(ERROR) << "Pipe created fail, can't trigger monitor loop";
  }
}

ChannelMonitor::~ChannelMonitor() {
  if (write_pipe_->IsOpen()) {
    write_pipe_->Write("KO", sizeof("KO"));
  }

  if (monitor_thread_.joinable()) {
    VLOG(1) << "waiting for monitor thread to join";
    monitor_thread_.join();
  }
}

void ChannelMonitor::MonitorLoop() {
  do {
    cuttlefish::SharedFDSet read_set;
    read_set.Set(server_);
    read_set.Set(read_pipe_);

    for (auto& client : clients_) {
      if (client->is_valid) {
        read_set.Set(client->client_read_fd_);
      }
    }

    {
      const absl::MutexLock lock(remote_clients_mutex_);
      for (auto& client : remote_clients_) {
        if (client->is_valid) {
          read_set.Set(client->client_read_fd_);
        }
      }
    }

    int num_fds = cuttlefish::Select(&read_set, nullptr, nullptr, nullptr);
    if (num_fds < 0) {
      LOG(ERROR) << "Select call returned error : " << strerror(errno);
      // std::exit(kSelectError);
      break;
    } else if (num_fds > 0) {
      if (read_set.IsSet(server_)) {
        AcceptIncomingConnection();
      }
      if (read_set.IsSet(read_pipe_)) {
        std::string buf(2, ' ');
        read_pipe_->Read(buf.data(), buf.size());  // Empty pipe
        if (buf == std::string("KO")) {
          VLOG(1) << "requested to exit now";
          break;
        }

        const auto is_invalid_client = [](const std::unique_ptr<Client>& client) {
          return !client->is_valid;
        };

        std::erase_if(clients_, is_invalid_client);

        const absl::MutexLock lock(remote_clients_mutex_);
        std::erase_if(remote_clients_, is_invalid_client);
      }

      std::erase_if(clients_, [&read_set, this](const std::unique_ptr<Client>& client) {
        if (read_set.IsSet(client->client_read_fd_)) {
          return !ReadCommand(*client);
        } else {
          return false;  // keep it
        }
      });

      const absl::MutexLock lock(remote_clients_mutex_);
      std::erase_if(remote_clients_, [&read_set, this](const std::unique_ptr<Client>& client) {
        if (read_set.IsSet(client->client_read_fd_)) {
          return !ReadCommand(*client);
        } else {
          return false;  // keep it
        }
      });
    } else {
      // Ignore errors here
      LOG(ERROR) << "Select call returned error : " << strerror(errno);
    }
  } while (true);
}

}  // namespace cuttlefish
