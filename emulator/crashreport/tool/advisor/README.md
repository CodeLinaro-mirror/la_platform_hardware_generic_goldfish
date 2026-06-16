<!--
Copyright 2026 The Android Open Source Project

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->

# CrashAdvisor (`emulator/crashreport/tool/advisor`)

## Overview

This directory houses **CrashAdvisor**, the automated AI diagnostic pipeline for the Android Emulator (`aemu`).
CrashAdvisor accepts a `go/crash` URL or raw crash ID, retrieves minidump artifacts and symbols, invokes the standalone C++ `crashreport` CLI binary to render thread stack traces and looper breadcrumb Mermaid timelines, and instantly generates an interactive `jetski` AI investigation script for collaborative Root Cause Analysis (RCA).

______________________________________________________________________

## Actionable Usage Workflow

### Step 1: Obtain Your Android Build OAuth2 Token (macOS)

CrashAdvisor exclusively utilizes the Android Build (`go/ab`) API to stream pre-bundled Breakpad symbol archives (`sdk-repo-<platform>-emulator-breakpad-symbols-<build_id>.zip`). This strictly requires an OAuth2 token with `API_ANDROID_BUILD_INTERNAL` scope.

- **On GLinux**: Tokens are retrieved automatically via `oauth2l fetch --sso`.
- **On macOS**: Generate an SSO token on a GLinux workstation:
  ```bash
  ~/go/bin/oauth2l fetch --sso $USER@google.com androidbuild.internal
  ```

### Step 2: Build the Environment & Scrape Assets

Run CrashAdvisor via Bazel, passing the crash ID (or `go/crash` URL) and your token string:

```bash
bazel run @goldfish//emulator/crashreport/tool/advisor -- 6998451fea502b78 --token "ya29.a0AT3oNZ..."
```

**What Happens Automatically:**

1. **Asset Fetching**: Downloads `metadata.json` (pretty-printed in-place) and `minidump.dmp` via `gosso`.
2. **Global Symbol Caching**: Downloads and extracts Breakpad symbols to `/tmp/crashadvisor_symbols_cache_$USER/<build_id>/` (instantly reused across different crash IDs from the same build).
3. **Local Dump Generation**: Executes the C++ `crashreport` binary to render both `crashreport.txt` (human-readable stack trace & looper timelines) and `crashreport.json` (`--format=machine`).
4. **Script Preparation**: Generates `/tmp/crashadvisor_$USER/<crash_id>/investigation_cmd.sh`.

### Step 3: Launch the Interactive AI Investigation

Upon completion, CrashAdvisor outputs the path to your interactive investigation script. Execute it directly in your terminal:

```bash
/tmp/crashadvisor_$USER/6998451fea502b78/investigation_cmd.sh
```

**What Happens in the REPL:**

1. **The `Crash Advisor` Persona**: Jetski loads the co-located [crash_advisor.md](crash_advisor.md) specialized agent persona.
2. **Initial Codebase Research**: Jetski immediately ingests `crashreport.txt`, explores the attached AOSP workspace using search tools (`grep_search`), evaluates the looper timeline, and prints its initial Root Cause Analysis.
3. **Interactive Collaboration**: The REPL (`jetski> `) remains open so you can ask follow-up questions, inspect specific memory addresses, or collaborate on remediation strategies.



