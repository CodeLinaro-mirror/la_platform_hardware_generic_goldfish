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
#pragma once

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#endif  // _WIN32

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"

namespace goldfish::memory {

/**
 * @brief A class to share memory between 2 processes using a memory-mapped
 * file.
 *
 * This class provides a platform-independent way to create and share a region
 * of memory between processes. It is implemented by memory-mapping a file on
 * the filesystem.
 *
 * The path to the backing file can be provided as a regular path or as a
 * file URI (e.g., `file:///path/to/file`). On Windows, file URIs are
 * converted to native paths. On other platforms, the path is used directly.
 *
 * Usage examples:
 *
 * @code
 * // Proc1: The owner/creator
 * const std::string message = "Hello world!";
 * // Creates a file named "my_shared_mem" in the system's temp directory.
 * std::string path = (std::filesystem::temp_directory_path() / "my_shared_mem").string();
 * SharedMemory writer(path, 4096);
 * auto status = writer.Create(std::filesystem::perms::owner_read |
 *                             std::filesystem::perms::owner_write);
 * if (status.ok()) {
 *   memcpy(*writer, message.c_str(), message.size());
 * }
 *
 * // Proc2: The observer
 * // Path must be the same.
 * SharedMemory reader(path, 4096);
 * status = reader.Open(SharedMemory::AccessMode::kReadOnly);
 * if (status.ok()) {
 *     const char* read = (const char*) *reader;
 *     // ... use the memory ...
 * }
 * @endcode
 *
 * @note The size of the shared memory region must be specified in the
 * constructor by all parties. It is not automatically discovered.
 *
 * @warning Behavior differs between platforms regarding file handling and
 * lifetime.
 *
 * **General:**
 *  - The object that calls `Create()` is considered the "owner".
 *  - By default (`DestructionPolicy::kAuto`), the owner will delete the
 *    backing file upon destruction. This is not guaranteed if the process
 *    crashes.
 *  - Other objects that `Open()` the region will not delete the file.
 *  - `Create()` is exclusive; it will fail if the file already exists.
 *
 * **Posix (Linux/macOS):**
 *  - File permissions are controlled by the `mode` parameter to `Create()`.
 *
 * **Win32:**
 *  - If `Create()` is called with a size larger than the backing file on disk,
 *    the file will be grown to that size.
 *  - File permissions are inherited from the security attributes of the creating
 *    process. The `mode` parameter to `Create()` is ignored.
 */
class SharedMemory {
  public:
    using memory_type = void*;

#ifdef _WIN32
    using handle_type = HANDLE;
    static constexpr handle_type kInvalidHandle = nullptr;
#else
    using handle_type = int;
    static constexpr handle_type kInvalidHandle = -1;
#endif
    /**
     * @brief Access modes for the shared memory region.
     */
    enum class AccessMode : std::uint8_t {
        kReadOnly,  ///< Read-only access.
        kReadWrite  ///< Read-write access.
    };

    /// @brief Policy for handling backing file cleanup on destruction.
    enum class DestructionPolicy : std::uint8_t {
        kAuto,     ///< Delete the backing file on destruction if this object created it (default).
        kDestroy,  ///< Always delete the backing file on destruction.
        kKeep      ///< Never delete the backing file on destruction.
    };

    /**
     * @brief Constructs a SharedMemory object.
     *
     * @param path_or_uri The file path or URI of the shared memory region.
     * @param size The size of the shared memory region.
     * @param policy The destruction policy (default: kAuto).
     */
    SharedMemory(std::string_view path_or_uri, size_t size,
                 DestructionPolicy policy = DestructionPolicy::kAuto);
    ~SharedMemory() { Close(); }

    /**
     * @brief Move constructor.
     * @param other The object to move from.
     */
    SharedMemory(SharedMemory&& other) noexcept : SharedMemory("", 0) { swap(other); }

    /**
     * @brief Move assignment operator.
     * @param other The object to move from.
     * @return Reference to this object.
     */
    SharedMemory& operator=(SharedMemory&& other) noexcept {
        SharedMemory(std::move(other)).swap(*this);
        return *this;
    }

    /**
     * @brief Swaps the contents of this object with another.
     * @param other The object to swap with.
     */
    void swap(SharedMemory& other) noexcept {  // NOLINT
        using std::swap;
        swap(backing_file_, other.backing_file_);
        swap(size_, other.size_);
        swap(address_, other.address_);
        swap(fd_, other.fd_);
        swap(file_, other.file_);
        swap(create_, other.create_);
    }

    /**
     * @brief Non-member swap function for Argument Dependent Lookup (ADL).
     * @param lhs First object.
     * @param rhs Second object.
     */
    friend void swap(SharedMemory& lhs, SharedMemory& rhs) noexcept {  // NOLINT
        lhs.swap(rhs);
    }

    // Prevent double unlinking by forbidding copy construction and assignment.
    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    /**
     * @brief Creates a new shared memory region.
     *
     * @param mode The access permissions (e.g., owner_read | owner_write).
     * @return absl::Status Ok status on success, or an error status on failure.
     */
    absl::Status Create(std::filesystem::perms mode);

    /**
     * @brief Creates a new shared memory region without mapping it.
     *
     * @param mode The access permissions.
     * @return absl::Status Ok status on success, or an error status on failure.
     */
    absl::Status CreateNoMapping(std::filesystem::perms mode);

    /**
     * @brief Opens an existing shared memory region.
     *
     * @param mode The access mode (ReadOnly or ReadWrite).
     * @return absl::Status Ok status on success, or an error status on failure.
     */
    absl::Status Open(AccessMode access);

    /**
     * @brief Checks if the shared memory object is currently open.
     * @return True if open, false otherwise.
     */
    bool IsOpen() const;

    /**
     * @brief Closes the shared memory object.
     *
     * Unmaps memory and closes the file descriptor.
     */
    void Close();

    /**
     * @brief Gets the size of the shared memory region.
     * @return Size in bytes.
     */
    size_t Size() const { return size_; }

    /**
     * @brief Gets the backing file of the shared memory region.
     * @return The memory mapped file.
     */
    std::filesystem::path BackingFile() const { return backing_file_; }

    /**
     * @brief Gets a pointer to the mapped memory.
     * @return Pointer to start of mapped memory, or nullptr if not mapped.
     */
    memory_type Get() const { return address_; }

    /**
     * @brief Dereference operator to get the mapped memory pointer.
     * @return Pointer to start of mapped memory.
     */
    memory_type operator*() const { return Get(); }

    /**
     * @brief Gets the underlying file descriptor/handle.
     * @return File descriptor or handle.
     */
    handle_type GetFd() const { return fd_; }

    /**
     * @brief Checks if the memory is currently mapped.
     * @return True if mapped, false otherwise.
     */
    bool IsMapped() const { return address_ != nullptr; }

  private:
#ifdef _WIN32
    absl::Status OpenInternal(AccessMode access, bool create, bool do_mapping);
#else
    absl::Status OpenInternal(int oflag, int mode, bool do_mapping = true);
#endif

    handle_type fd_ = kInvalidHandle;
    handle_type file_ = kInvalidHandle;

    std::filesystem::path backing_file_;
    size_t size_ = 0;
    void* address_{nullptr};
    DestructionPolicy destruction_policy_{DestructionPolicy::kAuto};
    bool create_{false};
};
}  // namespace goldfish::memory
