#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "client/crash_report_database.h"
#include "client/crashpad_client.h"

void functionC(int x) {
    std::cout << "In function C: " << x << std::endl;
    // Volatile pointer dereference to trigger crash safely across compiler optimizations
    volatile int* p = nullptr;
    *p = x;
}

void functionB(int x) {
    std::cout << "In function B, calling C" << std::endl;
    functionC(x + 10);
}

void functionA(int x) {
    std::cout << "In function A, calling B" << std::endl;
    functionB(x + 5);
}

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <path_to_crashpad_handler> <database_dir>"
                  << std::endl;
        return 1;
    }

    fs::path handler_path = argv[1];
    fs::path db_dir = argv[2];

    std::cout << "Initializing Crashpad with handler: " << handler_path.string()
              << " and db: " << db_dir.string() << std::endl;

    crashpad::CrashpadClient client;
    std::map<std::string, std::string> annotations;
    annotations["prod"] = "CrashGenerator";
    annotations["ver"] = "1.0";

    std::vector<std::string> arguments;
    arguments.push_back("--no-rate-limit");

    bool success = client.StartHandler(base::FilePath(handler_path.native()),
                                       base::FilePath(db_dir.native()),
                                       base::FilePath(),  // metrics_path
                                       "",                // url
                                       annotations, arguments,
                                       true,  // restartable
                                       false  // asynchronous_start
    );

    if (!success) {
        std::cerr << "Failed to start Crashpad handler" << std::endl;
        return 2;
    }

    std::cout << "Crashpad handler started successfully. Crashing now..." << std::endl;
    functionA(42);

    return 0;
}
