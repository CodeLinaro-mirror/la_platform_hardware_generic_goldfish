# Fishtank Update Guide
**IMPORTANT**: If you are not a Google developer, you won't be able to update the fishtank zips, and
can stop reading.

This document describes the process for updating the "fishtank" zips used by the emulator.

## Overview

The "fishtank" zips contain pre-built binaries and libraries used by the emulator, particularly for graphics and UI components. When new versions of these components are built, they need to be uploaded to Google Cloud Storage (GCS) and the emulator's build configuration must be updated to use the new versions.

## Prerequisites

- Access to the `emu-next-bazel` GCS bucket.
- `gpkg` tool installed and configured.

## Update Process

### Step 1: Prepare the Environment

Before running any Bazel commands that interact with internal repositories or GCS, you must set up your credentials:

```bash
cd $BAZEL_ROOT
gpkg setup
```

### Step 2: Upload New Fishtank Zips

Use the `upload_fishtank` tool to upload the zips for a specific build ID.

```bash
bazel run @goldfish//emulator/tools/upload_fishtank:upload_fishtank -- -v <BUILD_ID>
```

Replace `<BUILD_ID>` with the actual build ID of the fishtank artifacts you want to upload.

This command will:
1.  Fetch the fishtank zips for Linux, macOS, and Windows for the given build ID from the internal build server.
2.  Upload them to `gs://emu-next-bazel/fishtank/<BUILD_ID>/`.
3.  Print generated Bazel snippets to the console.

### Step 3: Update `MODULE.bazel`

Copy the generated `gcs_file` snippets from the output of the previous step and use them to update the `fishtank` rules in:

`build/bazel/registry/modules/goldfish/0.0.1/MODULE.bazel`

Example snippets:

```starlark
gcs_file(
    name = "fishtank-linux",
    sha256 = "...",
    url = "gs://emu-next-bazel/fishtank/<BUILD_ID>/FISHTANK-sdk-repo-linux-emu-<BUILD_ID>.zip",
)
gcs_file(
    name = "fishtank-mac",
    sha256 = "...",
    url = "gs://emu-next-bazel/fishtank/<BUILD_ID>/FISHTANK-sdk-repo-darwin_aarch64-emu-<BUILD_ID>.zip",
)
gcs_file(
    name = "fishtank-windows",
    sha256 = "...",
    url = "gs://emu-next-bazel/fishtank/<BUILD_ID>/FISHTANK-sdk-repo-windows-emu-<BUILD_ID>.zip",
)
```

### Step 4: Update Test Data (if necessary)

If the internal structure of the fishtank zips has changed (e.g., files moved or renamed), you must update the expected content lists used by the release zip tests.

Update the relevant files in `hardware/generic/goldfish/emulator/tests/testdata/`:

- `release_zip_emulator_linux_x64.txt`
- `release_zip_emulator_mac_aarch64.txt`
- `release_zip_emulator_windows_x64.txt`

You can use `git diff` to verify the changes and ensure they match the new zip structure.

### Step 5: Verify the Changes

Run the release zip tests to ensure everything is correct:

```bash
bazel test @goldfish//emulator/tests:release_zip_test
```
