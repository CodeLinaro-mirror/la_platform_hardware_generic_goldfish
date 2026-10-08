# Component: Development Tools & `mise` Task Runner (`development`)

**Role:** Workspace Task Runner (`mise`), VS Code Integration, Compilation Database Generator, and Clang-Tidy Diff Runner
**Location:** `development/` (with `mise.toml` symlinked to `<workspace_root>/mise.toml`)
**Build Targets:** `@goldfish//development/tools:generate_bcc`

## Integration Guide & Key Files
| File | Description |
| :--- | :--- |
| `mise.toml` | Workspace task runner configuration (symlinked at `<workspace_root>/mise.toml`). Configures `BAZEL_ROOT`, `ANDROID_HOME`, prebuilt `bazel`/`clang` `PATH`, and `mise` tasks. |
| `tools/clang_tidy.py` | Runs `clang-tidy` against C/C++ lines modified in a git commit (`--commit HEAD` or `--commit <sha>`). Invoked by `mise tidy`. |
| `tools/generate_bcc.py` | Bazel binary (`@goldfish//development/tools:generate_bcc`) that generates `compile_commands.json` for `clangd`. Invoked by `mise bcc`. |
| `fasttest_exclusions.txt` | Cached list of expensive symbol/zip targets (`@breakpad//:dump_syms`, `@rules_pkg//pkg/private/zip:build_zip` rdeps) excluded by `mise fasttest`. |
| `GLOBAL-PREUPLOAD.cfg` | Repo preupload hooks configuration (`clang_format`, `bazel_lockfile`, `buildifier`, `xmllint`). |
| `emu-dev.code-workspace` | VS Code workspace settings for C++ formatting, Bazel integration, and debugging. |
| `README.MD` | Setup instructions for VS Code, `mise`, and Bazel. |

## `mise` Tasks Reference (`mise.toml`)

### 1. Per-Repository Git & Gerrit Tasks (`dir = "{{ cwd }}"`)
> **Important:** These tasks execute in the caller's current working directory (`{{ cwd }}`). Always set `Cwd` to the specific git repository you are modifying or uploading (e.g., `hardware/generic/goldfish` or `hardware/google/aemu`), **not** the superproject root.

- **`mise presubmit`**: Sets `git config push.negotiate false` and uploads the current branch to Gerrit with `Presubmit-Ready+1` (`repo upload . --cbr -y --label 'Presubmit-Ready+1'`).
- **`mise submit`**: Sets `git config push.negotiate false` and uploads the current branch to Gerrit for review without triggering presubmit (`repo upload . --cbr -y`).
- **`mise gum`**: Stages modified tracked files and amends `HEAD` (`git add --update . && git commit --amend`) after verifying `$USER` owns the `HEAD` commit.

### 2. Workspace Build, Test & Lint Tasks (run from `<workspace_root>`)
- **`mise fastbuild`**: Fast C++-only verification build (`bazel build --noshow_progress -- @goldfish//...` excluding non-`cc_*` targets).
- **`mise fasttest`**: Runs `@goldfish//...` unit tests while excluding slow zip/symbol packaging targets listed in `fasttest_exclusions.txt` (regenerated via `mise create-fasttest-exclusions`).
- **`mise build`**: Builds the full emulator release package (`bazel build @goldfish//emulator:release`).
- **`mise test`**: Runs all emulator tests (`bazel test @goldfish//...`).
- **`mise tidy`**: Runs `hardware/generic/goldfish/development/tools/clang_tidy.py --commit HEAD`.
- **`mise tidy_fix`**: Applies clang-tidy fixes (`bazel run @goldfish//:clang_tidy_report`).
- **`mise bcc`**: Generates `compile_commands.json` (`bazel run @goldfish//development/tools:generate_bcc -- --workspace $BAZEL_ROOT`).

