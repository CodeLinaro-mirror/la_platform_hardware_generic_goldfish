// Copyright (C) 2025 The Android Open Source Project
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
#include <memory>
#include <string>

namespace goldfish::singleton {

/**
 * @class ApplicationSingleton
 * @brief A class that provides a per-user lock to ensure a single instance
 *        of an application is running for a given user.
 *
 * This class uses a native, OS-managed file lock that is tied to the
 * process lifetime. This ensures that the lock is automatically released by the
 * operating system if the process crashes, preventing stale locks.
 *
 * Usage:
 * @code
 * int main(int argc, char* argv[]) {
 *     try {
 *         goldfish::singleton::ApplicationSingleton singleton("com.mycompany.myapp");
 *         if (!singleton.isPrimaryInstance()) {
 *             // Another instance is already running for this user.
 *             return 1;
 *         }
 *         // Proceed with application logic...
 *     } catch (const std::runtime_error& e) {
 *         // Handle locking error...
 *         return 1;
 *     }
 *     return 0;
 * } // singleton is destroyed here, releasing the lock.
 * @endcode
 */
class ApplicationSingleton {
  public:
    /**
     * @brief Attempts to acquire a per-user lock for the application.
     *
     * The constructor creates a lock file in a user-specific directory to
     * acquire a lock. It is critical that the provided identifier is unique to
     * the application.
     *
     * @param appName A unique identifier for the application, used to create the
     *        lock file (e.g., "[appName].lock").
     *
     * @details The lock file is created in the directory provided by
     *          `ConfigDirs::getDiscoveryDirectory()`.
     *
     * @throws std::runtime_error if the lock cannot be created or acquired due
     *         to a system error (e.g., out of resources, permissions issue).
     */
    explicit ApplicationSingleton(const std::string& appName);

    /**
     * @brief Releases the application lock.
     *
     * The destructor releases the lock. This happens automatically when the
     * object goes out of scope. Importantly, if the process crashes, the
     * operating system will automatically release the underlying file handle,
     * preventing a stale lock.
     */
    ~ApplicationSingleton();

    /**
     * @brief Checks if this process is the first and only instance of the
     *        application for the current user.
     *
     * @return true if this process successfully acquired the lock, meaning it is
     *         the primary instance for this user.
     * @return false if another instance already holds the lock.
     */
    bool isPrimaryInstance() const;

    // The ApplicationSingleton is non-copyable and non-movable to ensure that
    // the lock is uniquely owned and its lifetime is tied to the scope of a
    // single instance.
    ApplicationSingleton(const ApplicationSingleton&) = delete;
    ApplicationSingleton& operator=(const ApplicationSingleton&) = delete;
    ApplicationSingleton(ApplicationSingleton&&) = delete;
    ApplicationSingleton& operator=(ApplicationSingleton&&) = delete;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
    bool mIsLocked;
};

}  // namespace goldfish::singleton
