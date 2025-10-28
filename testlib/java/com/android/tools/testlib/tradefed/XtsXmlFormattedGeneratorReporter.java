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

import com.android.tradefed.result.suite.XmlFormattedGeneratorReporter;
import com.android.tradefed.config.Option;
import java.io.File;
import java.io.IOException;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;

/**
 * Implementation of the {@link XmlFormattedGeneratorReporter} which allows specifying the output
 * directory.
 */
public class XtsXmlFormattedGeneratorReporter extends XmlFormattedGeneratorReporter {
    @Option(name = "results-dir", description = "base directory to output test_result.xml.")
    private File mResultDir = null;

    /** Returns the result directory that should be used to store results. */
    @Override
    public File createResultDir() throws IOException {
        if (mResultDir != null) {
          String nowStr = LocalDateTime.now().format(DateTimeFormatter.ofPattern("yyyy.MM.dd_HH.mm.ss"));
          File outDir = new File(mResultDir, nowStr);
          outDir.mkdirs();
          return outDir;
        }
        // Base directory unspecified, just use the default.
        return super.createResultDir();
    }

}

