# Setting Up Visual Studio Code for Emulator Development

This guide outlines how to configure Visual Studio Code (VS Code) for efficient emulator development, covering essential extensions and workspace settings.

## Requirements

* **Visual Studio Code:**
  * Visual Studio Code is a versatile code editor developed by Microsoft, designed to streamline coding for various languages and frameworks. It offers features that enhance productivity and efficiency.
  * You can obtain Visual Studio Code here:
    * Within Google: [go/vscode/install](http://go/vscode/install), make sure to follow all the instructions. Make sure you also install `sudo apt install google-lldb-vscode`, so you have the lldb debug adapter installed.
    * Directly from Microsoft: [https://code.visualstudio.com/](https://code.visualstudio.com/).
  * Once you have installed Visual Studio Code, open the following workspace: `development/emu-dev.code-workspace`. Install the set of recommended extensions to improve your development experience.
  * Additional steps:
    * For MacOs:
      * Update the lldb-dap settings: `lldb-dap.executable-path` (Use the key combo `⌘,` and type `lldb-dap.executable-path`) to point to `/usr/local/bin/lldb-debug-adapter`.
    * For Google Linux (glinux):
      * Install the google lldb vscode extension: `sudo apt install google-lldb-vscode`.
* **Bazel:**
  * A fast, scalable, multi-language build system.
  * Download and installation instructions: [https://bazel.build/install](https://bazel.build/install)

## Workspace Configuration

This directory includes pre-configured VS Code settings designed to optimize your development environment. These settings are managed through:

* `.vscode/extensions.json`: Defines a curated list of recommended extensions for enhanced productivity.
* `emu-dev.code-workspace`: Provides optimized settings for code formatting, linting, and advanced code analysis.

## Recommended Extensions

The `extensions.json` file automatically manages the installation of these recommended extensions. Here are key categories and examples of beneficial tools:

* **C/C++ Development:**
  * `ms-vscode.cpptools`: Essential for IntelliSense, debugging, and code navigation.
  * `llvm-vs-code-extensions.vscode-clangd`: Language server for advanced code analysis, completion, formatting, and refactoring.
  * `jeff-hykin.better-cpp-syntax`: Enhanced C++ syntax highlighting for improved code readability.
  * `llvm-vs-code-extensions.lldb-dap`: LLDB debugger integration for debugging C/C++ code.
  * `ms-vscode.cpptools-themes`: Provides enhanced themes for the C/C++ extension.
* **Version Control:**
  * `eamodio.gitlens`: Enhances Git integration, providing detailed code authorship and change insights.
* **Build System Integration:**
  * `bazelbuild.vscode-bazel`: Improves Bazel integration for efficient build and dependency management.
  * `bjob.vsc-bazel-tools`: Additional Bazel tools for enhanced productivity.
* **Testing:**
  * `hbenl.vscode-test-explorer`: Test Explorer integration for running and debugging tests.
* **Markdown:**
  * `DavidAnson.vscode-markdownlint`: Markdown linter for enforcing consistent style and catching errors.
  * `mervin.markdown-formatter`: Markdown formatter for automatic formatting of markdown files.
* **Protocol Buffers:**
  * `zxh404.vscode-proto3`: Protocol Buffers (proto3) language support.
* **Cloud Development:**
  * `googlecloudtools.cloudcode`: Cloud Code for VS Code, providing tools for developing cloud-native applications.
* **Remote Development:**
  * `ms-vscode-remote.vscode-remote-extensionpack`: Extension pack for remote development scenarios.

## Configuration and Toolchain Management

The recommended extensions and workspace settings are designed to work out-of-the-box. You typically only need to adjust the workspace configuration when upgrading the compiler toolchain.

* **Toolchain Version:** The toolchain version is defined in `${workspaceFolder}/build/bazel/toolchains/tool_versions.json`.

## Enable IntelliSense

IntelliSense, which provides code completion, navigation, and other helpful features, relies on a JSON compilation database (`compile_commands.json`) located in your workspace root. This file is crucial for accurate code analysis and navigation.

**Generating `compile_commands.json`:**

You can generate this file using one of the following methods:

1. **VS Code Task:**
    * Open the command palette: `Ctrl+P` (Windows/Linux) or `Cmd+P` (macOS).
    * Type "Tasks: Run Task" and select it.
    * Choose "aemu: Update compile commands database" from the list.

    This task will create the `compile_commands.json` file, enabling IntelliSense features.

    *Note: You must re-run this task whenever you modify a Bazel `BUILD` file to ensure IntelliSense remains up-to-date.*

2. **Bazel Command (Command Line):**
    * Alternatively, you can generate the file from your terminal using the following Bazel command:

        ```bash
        bazel run //hardware/generic/goldfish/development/tools/bcc:extract-cc -- //hardware/generic/goldfish/...
        ```

## Building targets

You can build targets in various ways:

* By running `ctrl-P Bazel: Build Target` ( `⌘P Bazel: Build Target` on macOS) command.
* You can build the asan version of the emulator by running the `aemu: Build emulator with asan` task. `ctrl-P Tasks: Run Task` ( `⌘P Tasks: Run Task` on macOS), after which you can select it. Or use the quick shortcut `ctrl-B` (`⌘B` on macOS).
* By opening the BUILD file of interrest, and clicking the highlighted build minilink provided by the bazel plugin.

*Note: You can create custom tasks by modifying [tasks.json](development/.vscode/tasks.json), learn more about that [here](https://code.visualstudio.com/docs/editor/tasks)*.

## Debugging the Emulator

There is a default task to compile the emulator, which can be run with `ctrl-B` (`⌘B` on macOS). After that, you can debug the emulator from the debug menu `ctrl-D` (`⌘D` on macOS). You can select `Debug Goldfish` from the menu. You can create additional launch targets by modifying [launch.json](development/.vscode/launch.json).

## Running unit tests

You have the following options when running tests:

* Run the whole test suite. Issue the following command: `ctrl-P Tasks: Run Test Task` ( `⌘P Tasks: Run Test Task` on macOS).
* Run individual tests *(Only works for tests under //hardware/..., and is very slow!)*.
  * You can open op the unittest.cpp file of interrest. You should see a run button next to the test definition to run the test. See [ColorLogSink_unittest.cpp](android/logging/test/android/base/logging/ColorLogSink_unittest.cpp) for example.
  * You should have a test runner icon (looks like a flask, on hover it should say testing) in the [Activity Bar](https://code.visualstudio.com/docs/getstarted/userinterface). Click on that, which should change the Primary Side bar to the Testing panel, which shows all individual tests.

## Known Issues

* ***Linux***:
  * You might have to remove the  `prebuilts/clang/host/linux-x86/BUILD.bazel` file. This bazel file has some issues, causing failures when trying to discover all available tests by [vsc-bazel-tools](https://marketplace.visualstudio.com/items?itemName=bjob.vsc-bazel-tools) plugin.
* ***MacOs***
  * The [vsc-bazel-tools](https://marketplace.visualstudio.com/items?itemName=bjob.vsc-bazel-tools) plugin will not debug individual tests.
* **Bazel plugin:** The bazel plugin currently uses the location of the WORKSPACE file to derive targets, like `//hardware/generic/goldfish/...`, without a WORKSPACE file they will be incorrectly derived. We no longer use WORKSPACE files as we have switched to bazel modules. As a workaround you can create an empty one yourself in $AOSP.