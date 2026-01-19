# Component: Development Tools

**Role:** Provides configuration and scripts for the developer workflow, primarily focused on Visual Studio Code and `mise` integration.
**Location:** `hardware/generic/goldfish/development`
**Namespace:** N/A

## Integration Guide
| File | Description |
| :--- | :--- |
| `emu-dev.code-workspace` | VS Code workspace settings for C++ formatting, Bazel integration, and debugging. |
| `mise.toml` | Task runner configuration (build, test, tidy, submit). |
| `README.MD` | Setup instructions for VS Code and Bazel. |

## Critical Infrastructure
* **VS Code:** Pre-configured tasks (`.vscode/tasks.json`) and launch configurations (`.vscode/launch.json`) simplify building, running tests, and debugging the emulator.
* **Mise:** Used as a task runner to standardize common developer actions (`build`, `test`, `submit`) across platforms.
* **Compilation Database:** Tools to generate `compile_commands.json` for IntelliSense (clangd).

## Dependencies
* **External:** VS Code, Bazel, `mise`, `repo`.

## Threading Model
* **N/A:** Configuration files and scripts.
