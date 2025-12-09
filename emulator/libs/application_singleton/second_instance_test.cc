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
#include <string>

#include "goldfish/singleton/application_singleton.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <app_name>" << std::endl;
        return 2;  // Indicate incorrect usage
    }

    std::string appName = argv[1];

    try {
        goldfish::singleton::ApplicationSingleton singleton(appName);
        if (singleton.isPrimaryInstance()) {
            // This process is the primary instance. In the context of the test,
            // this would be unexpected, but we'll return 0 to indicate success.
            return 0;
        } else {
            // This process is a secondary instance, which is the expected
            // outcome for the test helper.
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "An exception occurred: " << e.what() << std::endl;
        return 3;  // Indicate an error
    }

    return 0;
}
