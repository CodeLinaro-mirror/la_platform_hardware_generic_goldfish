# Update Check Library (`//emulator/libs/update_check`)

Asynchronous "Check For Update" (CFU) support for the Android Emulator launcher.

## Overview
- **`repository_manifest`**: Parses `repository2-3.xml` manifests using `tinyxml2`, filters `<remotePackage>` entries matching a caller-provided package prefix, host `<archive>` (`linux`/`macosx`/`windows` and `x64`/`aarch64`), and update channel stability (`channel <= max_channel`), and returns the newest version ordered by `(major, minor, micro, build_id)`.
- **`update_check`**: Runs an asynchronous update check on a background thread managed by `ScopedUpdateCheck` using `//emulator/libs/network:http_client` (`libcurl`), persists per-version check timestamps in `<user_dir>/emu-update-last-check.ini` (throttled to once every 4 hours), and logs an actionable update notice when a newer build is available.
