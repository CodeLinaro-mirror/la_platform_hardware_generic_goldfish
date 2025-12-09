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
#include <iostream>
#include <stdexcept>
#include <string>

#include "goldfish/singleton/application_singleton.h"

#ifndef _WIN32
#include <signal.h>
#endif

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <app_name>" << std::endl;
        return 1;  // Indicate incorrect usage
    }

    std::string appName = argv[1];

    try {
        goldfish::singleton::ApplicationSingleton singleton(appName);
        if (singleton.isPrimaryInstance()) {
            // We are the primary instance, now crash.
            volatile int* volatile ptr = nullptr;
            *ptr = 1313;  // die

#ifndef _WIN32
            raise(SIGABRT);
#endif
            abort();
        } else {
            // This shouldn't happen in the test.
            return 2;
        }
    } catch (const std::exception& e) {
        std::cerr << "An exception occurred: " << e.what() << std::endl;
        return 3;  // Indicate an error
    }

    return 0;  // Should not be reached
}
