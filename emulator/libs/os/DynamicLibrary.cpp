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

#include "goldfish/os/DynamicLibrary.h"

#include <utility>

namespace goldfish::os {
namespace {
#if defined(_WIN32)
HMODULE loadLibraryImpl(const std::filesystem::path& path) {
    return ::LoadLibraryA(path.string().c_str());
}

void* getProcAddressImpl(HMODULE lib, const char* func) {
    return reinterpret_cast<void*>(::GetProcAddress(lib, func));
}

#else

void* loadLibraryImpl(const std::filesystem::path& path) {
    return ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
}

void* getProcAddressImpl(void* lib, const char* func) {
    return ::dlsym(lib, func);
}
#endif
}  // namespace

DynamicLibrary::DynamicLibrary(const std::filesystem::path& path)
        : mHandle(loadLibraryImpl(path)) {}

DynamicLibrary::DynamicLibrary(DynamicLibrary&& rhs) : mHandle(std::move(rhs.mHandle)) {}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& rhs) {
    swap(*this, rhs);
    return *this;
}

bool DynamicLibrary::ok() const {
    return mHandle.ok();
}

void* DynamicLibrary::operator[](const char* func) const {
    return getProcAddressImpl(mHandle.get(), func);
}

void swap(DynamicLibrary& lhs, DynamicLibrary& rhs) {
    using std::swap;
    swap(lhs.mHandle, rhs.mHandle);
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
