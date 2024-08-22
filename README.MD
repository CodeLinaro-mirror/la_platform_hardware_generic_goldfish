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
- **tsan**: Thread sanitizer build.  ThreadSanitizer is a tool that detects data races. Typical slowdown introduced by ThreadSanitizer is about **5x-15x**. Typical memory overhead introduced by ThreadSanitizer is about **5x-10x**.

You can select the configuration by passing the `--config {my_config}` parameters. For example to create a thread sanitizer release you can do:

```sh
bazel build --config tsan //hardware/generic/goldfish/emulator:release
```

### Setting global build options

You can add build flags globally by adding the `--copt` parameter, for example

```sh
bazel build --copt=-funsafe-math-optimizations //hardware/generic/goldfish/emulator:release
```

Would globablly allow unsafe floating-point optimizations.

### Creating a JSON Compilation Database with Bazel

A JSON Compilation Database is a special file that captures the exact commands used to compile your C++ project. It stores information about include paths, compiler flags, and the location of source files. This data acts as a roadmap, guiding code analysis tools and editors like VSCode and Vim to understand your project's structure, enabling advanced features like:

* **Accurate Code Navigation:**  Jump to definitions, find all references, and navigate complex codebases with confidence.
* **Intelligent Code Completion:** Receive context-aware suggestions for variables, functions, and classes as you type.
* **On-the-Fly Diagnostics:** Catch errors and potential issues early, even before you compile your code.
* **Refactoring Support:** Rename symbols, extract methods, and perform other refactorings safely and efficiently.

Various editors and IDE's can consume this file to provide you with an improved developer experience:

* **VSCode:** The `clangd` extension seamlessly integrates with a JSON Compilation Database, enhancing the built-in C++ support.
* **Vim:** Plugins like `YouCompleteMe` or `coc.nvim` can leverage `clangd`'s capabilities for powerful code completion and navigation.
* **Other Editors:** Many modern code editors offer similar integration possibilities, making JSON Compilation Databases a universal tool for C++ development.

In your Bazel workspace, you can generate a JSON Compilation Database using the following command:

```sh

bazel run //hardware/generic/goldfish/development/tools/bcc:extract-cc -- //hardware/generic/goldfish/...

```

This command runs the extract-cc tool located in the specified Bazel package. The `-- //hardware/generic/goldfish/...` part tells Bazel to generate compilation commands for all C/C++ targets under the `//hardware/generic/goldfish` directory, including their transitive dependencies. The generated `compile_commands.json` file will be placed at the root of your Bazel workspace.

**Important Considerations**

* **Generated Source Files:** Source files generated during the build process, such as protobuf files, gRPC service definitions, and QEMU trace headers, will not be indexed.
* **Build File Modifications:** Any changes to `BUILD` or `BUILD.bazel` files require re-running the tool to update the database.
* **Editor/IDE Configuration:** Some editors or IDEs might require additional configuration to utilize the generated `compile_commands.json` file. Consult your editor's documentation for specific instructions.

## Adding Additional Functionality

The following targets might be of interrest if you wish to add new functionality:

- `//hardware/generic/goldfish/emulator:emulator_unit_tests`: This target contains all the test suites that will be run as part of the build. Make sure your tests are added to this suite.
- `//hardware/generic/goldfish/emulator:release`: The release target. This constructs the zip file with everything that will be distributed. You will need to modify this if you wish to distribute new files.
- `//hardware/generic/goldfish/emulator/launcher:goldfish`: The QEMU launcher. This binary is responsible for configuring the AVD and invoking the qemu emulator with the proper configuration. This is where you configure the drivers, setup the qcow2 images, verify that your system meets the requirements etc..

See [emulator/plugin/README.MD](emulator/plugin/README.MD) if you wish to add additional plugins that need to be loaded into QEMU.
