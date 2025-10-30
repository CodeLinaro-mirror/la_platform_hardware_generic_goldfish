// Copyright 2025 The Android Open Source Project
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
#include <optional>
#include <memory>
#include <vector>

#include "client/crash_report_database.h"

namespace android {
namespace crashreport {

class CrashReportManager {
  public:
    bool Initialize();
    std::vector<crashpad::CrashReportDatabase::Report> GetAllReports();
    bool DeleteReport(const crashpad::UUID& uuid);
    bool RequestUpload(const crashpad::UUID& uuid);
    std::optional<std::string> GetLatestReportPath();
    void ForEachReport(
            std::function<void(const crashpad::CrashReportDatabase::Report& report)> action);

  private:
    std::unique_ptr<crashpad::CrashReportDatabase> mDb;
};

}  // namespace crashreport
}  // namespace android
