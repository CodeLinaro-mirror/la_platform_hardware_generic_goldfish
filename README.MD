# The Android Emulator

This repository contains a series of plugins and tools to extend QEMU with android emulator functionality. The android emulator is build using bazel.

## Quick Start

You can create a release by running

```sh
bazel build --config release //hardware/generic/goldfish/emulator:release
```

This will produce a zip file for your current architecture. You can unzip this file and launch the emulator. For example on darwin it will produce the following:

```sh
bazel build --config release //hardware/generic/goldfish/emulator:release
INFO: Analyzed target //hardware/generic/goldfish/emulator:release (0 packages loaded, 0 targets configured).
INFO: Found 1 target...
Target //hardware/generic/goldfish/emulator:release up-to-date:
  bazel-bin/hardware/generic/goldfish/emulator/sdk-repo-darwin_aarch64-emulator-developer.zip
INFO: Elapsed time: 0.258s, Critical Path: 0.00s
INFO: 1 process: 1 internal.
INFO: Build completed successfully, 1 total action
```

The launcher executable is called `goldfish`.

### Running a development build

You can also run the emulator directly from within bazel:

```sh
bazel run --config debug //hardware/generic/goldfish/emulator/launcher:goldfish -- --avd V --vnc :8
```

This will launch the goldfish emulator with avd *V* and creates a vnc connection on port 5908. You can now attach your favorite debugger to debug any issues.

Note that on linux you will have to set the following source mapping:

```sh
$ lldb
(lldb) settings set target.source-map /proc/self/cwd /my/bazel/workspace/root
```

Where `/my/bazel/workspace/root` points to your bazel workspace root.

## Build configurations

We have the following build configurations available:

- **debug**: Unstripped debug build with no optimizations.
- **release**: Optimized stripped release build.
- **release-assert**: Optimized stripped release build with asserts enabled (`-UNDEBUG`).
- **asan**: Address sanitizer build. AddressSanitizer is a fast memory error detector. The tool can detect the following types of bugs:

  - Out-of-bounds accesses to heap, stack and globals
  - Use-after-free
  - Use-after-return (runtime)
  Enable with: `ASAN_OPTIONS=detect_stack_use_after_return=1` (already enabled on Linux).
   Disable with: `ASAN_OPTIONS=detect_stack_use_after_return=0`.
  - Use-after-scope
  - Double-free, invalid free
  - Memory leaks (experimental)

Typical slowdown introduced by AddressSanitizer is *2x*.

- **msan**: Memory sanitizer build. MemorySanitizer is a detector of uninitialized reads.  Typical slowdown introduced by MemorySanitizer is **3x**.
- **tsan**: Thread sanitizer build.  ThreadSanitizer is a tool that detects data races. Typical slowdown introduced by ThreadSanitizer is about **5x-15x**. Typical memory overhead introduced by ThreadSanitizer is about **5x-10x**. *Note:* This might need additional effort to get working with QEMU.

You can select the configuration by passing the `--config {my_config}` parameters. For example to create a address sanitizer release you can do:

```sh
bazel build --config asan //hardware/generic/goldfish/emulator:release
```

### Setting global build options

You can add build flags globally by adding the `--copt` parameter, for example

```sh
bazel build --copt=-funsafe-math-optimizations //hardware/generic/goldfish/emulator:release
```

Would globablly allow unsafe floating-point optimizations.

### Configuring ASAN

AddressSanitizer (ASAN) is a memory error detector that helps identify issues like buffer overflows and use-after-free errors. When using ASAN, ensure the following environment variables are set:

```sh
export ASAN_OPTIONS=detect_odr_violation=0
```

Statically linking glib is causing One Definition Rule (ODR) violations in some of the shared libraries we use. ODR violations occur when the same global object is defined differently in multiple parts of a program. Disabling ODR violation detection prevents ASAN from reporting these errors, which are expected in our current configuration.

You'll need to use a symbolizer to decode the stack frames reported by ASAN. A symbolizer translates memory addresses back into human-readable function names and line numbers, making it easier to understand error reports.

```sh
export ASAN_SYMBOLIZER_PATH=/to/clang/llvm-symbolizer
```

The pixman-1 library, due to complex initialization logic, leaks a few bytes. The set of known (or acceptable) leaks is defined in [leak_suppressions.txt](./leak_suppressions.txt)

```sh
export LSAN_OPTIONS=suppressions=$PWD/leak_suppressions.txt
```

Use the [asan_exports.sh](./asan_exports.sh) shell script to load this configuration into your shell:

```bash
source ./asan_exports.sh
```

Next you can run the emulator under asan as:

```bash
bazel run --config asan //hardware/generic/goldfish/emulator/launcher:goldfish  -- --avd V
```

*Note: If you are not seeing memory address translated to stackframes you might have to update the [asan_exports.sh](./asan_exports.sh) shell script to point to the proper symbolizer.*

## Using Visual Studio Code

Visual Studio Code is a versatile code editor developed by Microsoft, designed to streamline coding for various languages and frameworks. It offers features that enhance productivity and efficiency.

You can obtain Visual Studio Code here:

- Within Google: [go/vscode/install](http://go/vscode/install), make sure to follow all the instructions. Make sure you also install `sudo apt install google-lldb-vscode`, so you have the lldb debug adapter installed.
- Directly from Microsoft: [https://code.visualstudio.com/](https://code.visualstudio.com/).

Once you have installed Visual Studio Code, open the following workspace: [development/emu-dev.code-workspace](development/emu-dev.code-workspace). Install the set of recommended extensions to improve your development experience.

Additional steps:

- For MacOs:
  - Update the lldb-dap settings: `lldb-dap.executable-path` (Use the key combo `⌘,` and type `lldb-dap.executable-path`) to point to `/usr/local/bin/lldb-debug-adapter`.
- For Google Linux (glinux):
  - Install the google lldb vscode extension: `sudo apt install google-lldb-vscode`.

### Enable IntelliSense

In order for IntelliSense to work you will need to create a JSON compilation database. This can be done by running the `aemu: Update compile commands database` task.  Issue the following command: `ctrl-P Tasks: Run Task` ( `⌘P Tasks: Run Task` on macOS). Next select `aemu: Update compile commands database`. This will create the compilation database that is used by IntelliSense.  Once you have run this, you should have code completion, code navigation, etc., available to you.

*Note: You will have to re-run the `aemu: Update compile commands database` task for each change to a Bazel BUILD file*.

### Building targets

You can build targets in various ways:

- By running `ctrl-P Bazel: Build Target` ( `⌘P Bazel: Build Target` on macOS) command.
- You can build the asan version of the emulator by running the `aemu: Build emulator with asan` task.  `ctrl-P Tasks: Run Task` ( `⌘P Tasks: Run Task` on macOS), after which you can select it. Or use the quick shortcut `ctrl-B` (`⌘B` on macOS).
- By opening the BUILD file of interrest, and clicking the highlighted build minilink provided by the bazel plugin.

*Note: You can create custom tasks by modifying [tasks.json](development/.vscode/tasks.json), learn more about that [here](https://code.visualstudio.com/docs/editor/tasks)*.

### Debugging the Emulator

There is a default task to compile the emulator, which can be run with `ctrl-B` (`⌘B` on macOS). After that, you can debug the emulator from the debug menu `ctrl-D` (`⌘D` on macOS). You can select `Debug Goldfish` from the menu.  You can create additional launch targets by modifying [launch.json](development/.vscode/launch.json).

*Note: Breakpoints in dependencies, like abseil, or grpc, are not yet working*.

### Running unit tests

You have the following options when running tests:

- Run the whole test suite.  Issue the following command: `ctrl-P Tasks: Run Test Task` ( `⌘P Tasks: Run Test Task` on macOS).
- Run individual tests *(Only works for tests under //hardware/..., and is very slow!)*.
  - You can open op the unittest.cpp file of interrest. You should see a run button next to the test definition to run the test. See [ColorLogSink_unittest.cpp](android/logging/test/android/base/logging/ColorLogSink_unittest.cpp) for example.
  - You should have a test runner icon (looks like a flask, on hover it should say testing) in the [Activity Bar](https://code.visualstudio.com/docs/getstarted/userinterface). Click on that, which should change the Primary Side bar to the Testing panel, which shows all individual tests.

*Note: Debugging individual tests is not yet supported, you will have to create a custom launch configuration yourself*.

## Creating a JSON Compilation Database with Bazel

A JSON Compilation Database is a special file that captures the exact commands used to compile your C++ project. It stores information about include paths, compiler flags, and the location of source files. This data acts as a roadmap, guiding code analysis tools and editors like VSCode and Vim to understand your project's structure, enabling advanced features like:

- **Accurate Code Navigation:**  Jump to definitions, find all references, and navigate complex codebases with confidence.
- **Intelligent Code Completion:** Receive context-aware suggestions for variables, functions, and classes as you type.
- **On-the-Fly Diagnostics:** Catch errors and potential issues early, even before you compile your code.
- **Refactoring Support:** Rename symbols, extract methods, and perform other refactorings safely and efficiently.

Various editors and IDE's can consume this file to provide you with an improved developer experience:

- **VSCode:** The standard intellisense extension seamlessly integrates with a JSON Compilation Database, enhancing the built-in C++ support.
- **Vim:** Plugins like `YouCompleteMe` or `coc.nvim` can leverage `clangd`'s capabilities for powerful code completion and navigation.
- **Other Editors:** Many modern code editors offer similar integration possibilities, making JSON Compilation Databases a universal tool for C++ development.

In your Bazel workspace, you can generate a JSON Compilation Database using the following command:

```sh

bazel run //hardware/generic/goldfish/development/tools/bcc:extract-cc -- //hardware/generic/goldfish/...

```

This command runs the extract-cc tool located in the specified Bazel package. The `-- //hardware/generic/goldfish/...` part tells Bazel to generate compilation commands for all C/C++ targets under the `//hardware/generic/goldfish` directory, including their transitive dependencies. The generated `compile_commands.json` file will be placed at the root of your Bazel workspace.

**Important Considerations**

- **Generated Source Files:** Source files generated during the build process, such as protobuf files, gRPC service definitions, and QEMU trace headers, will not be indexed.
- **Build File Modifications:** Any changes to `BUILD` or `BUILD.bazel` files require re-running the tool to update the database.
- **Editor/IDE Configuration:** Some editors or IDEs might require additional configuration to utilize the generated `compile_commands.json` file. Consult your editor's documentation for specific instructions.

## Adding Additional Functionality

The following targets might be of interrest if you wish to add new functionality:

- `//hardware/generic/goldfish/emulator:emulator_unit_tests`: This target contains all the test suites that will be run as part of the build. Make sure your tests are added to this suite.
- `//hardware/generic/goldfish/emulator:release`: The release target. This constructs the zip file with everything that will be distributed. You will need to modify this if you wish to distribute new files.
- `//hardware/generic/goldfish/emulator/launcher:goldfish`: The QEMU launcher. This binary is responsible for configuring the AVD and invoking the qemu emulator with the proper configuration. This is where you configure the drivers, setup the qcow2 images, verify that your system meets the requirements etc..

See [emulator/plugin/README.MD](emulator/plugin/README.MD) if you wish to add additional plugins that need to be loaded into QEMU.
