/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.android.tools.testlib.tradefed;

import com.android.tradefed.build.IBuildInfo;
import com.android.tradefed.result.FileSystemLogSaver;
import com.android.tradefed.config.Option;
import java.io.File;
import java.io.IOException;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;

/**
 * Implementation of the {@link FileSystemLogSaver} which allows an xts style output dir.
 */
public class XtsFileSystemLogSaver extends FileSystemLogSaver {
    @Option(name = "logs-dir", description = "base directory to output logs")
    private File mLogsDir = null;

    /** Returns the logs directory that should be used to store logs. */
    @Override
    protected File generateLogReportDir(IBuildInfo buildInfo, File reportDir, String moduleName)
            throws IOException {
        if (mLogsDir != null) {
          String nowStr = LocalDateTime.now().format(DateTimeFormatter.ofPattern("yyyy.MM.dd_HH.mm.ss"));
          File outDir = new File(mLogsDir, nowStr);
          outDir.mkdirs();
          return outDir;
        }
        // Base directory unspecified, just use the default.
        return super.generateLogReportDir(buildInfo, reportDir, moduleName);
    }

}


