// Copyright 2020 The Android Open Source Project
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

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"

#include "goldfish/memory/shared_memory.h"

ABSL_FLAG(std::string, name, "", "Shared memory region name");
ABSL_FLAG(size_t, size, 0, "Size of shared memory region");
ABSL_FLAG(std::string, mode, "read", "Mode: read or write");
ABSL_FLAG(std::string, data, "", "Data to write (if mode is write) or expect (if mode is read)");
ABSL_FLAG(int, sleep, 0, "Sleep in ms after operation");

int main(int argc, char** argv) {
    absl::ParseCommandLine(argc, argv);

    std::string name = absl::GetFlag(FLAGS_name);
    size_t size = absl::GetFlag(FLAGS_size);
    std::string mode = absl::GetFlag(FLAGS_mode);
    std::string data = absl::GetFlag(FLAGS_data);
    int sleep_ms = absl::GetFlag(FLAGS_sleep);

    if (name.empty() || size == 0) {
        std::cerr << "Name and size are required." << std::endl;
        return 1;
    }

    using goldfish::memory::SharedMemory;
    std::unique_ptr<SharedMemory> mem_ptr;

    if (mode == "write") {
        mem_ptr = std::make_unique<SharedMemory>(name, size);
        // Try creating first, if exists open
        absl::Status status = mem_ptr->Create(std::filesystem::perms::owner_read |
                                              std::filesystem::perms::owner_write);
        if (!status.ok()) {
            // If create failed, maybe it already exists and we just need to open
            status = mem_ptr->Open(SharedMemory::AccessMode::kReadWrite);
        }

        if (!status.ok()) {
            std::cerr << "Failed to open/create for writing: " << status << std::endl;
            return 1;
        }

        if (data.size() > mem_ptr->Size()) {
            std::cerr << "Data too large for shared memory." << std::endl;
            return 1;
        }

        std::memcpy(**mem_ptr, data.c_str(), data.size());
        // Null terminate if possible
        if (data.size() < mem_ptr->Size()) {
            static_cast<char*>(**mem_ptr)[data.size()] = '\0';
        }
    } else if (mode == "read") {
        mem_ptr = std::make_unique<SharedMemory>(name, size);
        if (!mem_ptr->Open(SharedMemory::AccessMode::kReadOnly).ok()) {
            std::cerr << "Failed to open for reading." << std::endl;
            return 1;
        }

        std::string content(static_cast<const char*>(**mem_ptr));
        // If we expect specific data, check it
        if (!data.empty()) {
            // Use simple substring check or exact match
            if (content.find(data) == std::string::npos) {
                std::cerr << "Read content mismatch. Expected to find '" << data << "', got '"
                          << content << "'" << std::endl;
                return 1;
            }
        }
        std::cout << content << std::endl;
    } else {
        std::cerr << "Unknown mode: " << mode << std::endl;
        return 1;
    }

    if (sleep_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }

    return 0;
}
