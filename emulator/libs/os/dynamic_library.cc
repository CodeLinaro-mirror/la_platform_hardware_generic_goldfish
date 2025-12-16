/* Copyright 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "goldfish/os/dynamic_library.h"

#include <utility>

namespace goldfish::os {
namespace {
#if defined(_WIN32)
HMODULE LoadLibraryImpl(const std::filesystem::path& path) {
    return ::LoadLibraryA(path.string().c_str());
}

void* GetProcAddressImpl(HMODULE lib, const char* func) {
    return reinterpret_cast<void*>(::GetProcAddress(lib, func));
}

#else

void* LoadLibraryImpl(const std::filesystem::path& path) {
    return ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
}

void* GetProcAddressImpl(void* lib, const char* func) {
    return ::dlsym(lib, func);
}
#endif
}  // namespace

DynamicLibrary::DynamicLibrary(const std::filesystem::path& path)
        : handle_(LoadLibraryImpl(path)) {}

DynamicLibrary::DynamicLibrary(DynamicLibrary&& rhs) noexcept : handle_(std::move(rhs.handle_)) {}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& rhs) noexcept {
    swap(*this, rhs);
    return *this;
}

bool DynamicLibrary::ok() const {
    return handle_.ok();
}

void* DynamicLibrary::operator[](const char* func) const {
    return GetProcAddressImpl(handle_.get(), func);
}

void swap(DynamicLibrary& lhs, DynamicLibrary& rhs) noexcept {
    using std::swap;
    swap(lhs.handle_, rhs.handle_);
}

#if defined(_WIN32)
void DynamicLibrary::HandleDeleter::operator()(HMODULE lib) const {
    ::FreeLibrary(lib);
}
#else
void DynamicLibrary::HandleDeleter::operator()(void* lib) const {
    ::dlclose(lib);
}
#endif

}  // namespace goldfish::os
