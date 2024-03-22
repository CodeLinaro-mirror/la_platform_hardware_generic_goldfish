# Bazel -> CompileCommands.json: A Conversion Tool for Emulator Development

This tool streamlines development workflows within the Android emulator environment by generating a JSON Compilation Database from Bazel targets. It addresses compatibility issues arising from Bazel's sandboxing, making compiler commands usable by external tools like clangd.

**Key Features:**

* **Target Closure Calculation:** Accurately determines the full set of dependencies for specified targets.
* **Action Normalization:** Adapts Bazel-specific compiler commands into a format readily understood by external tools.
* **JSON Compilation Database Generation:**  Creates a structured JSON file that can be consumed by clangd for code intelligence features.

**How It Works:**

1. **Identifies Dependencies:** The tool analyzes Bazel targets to understand the complete scope of the compilation process.
2. **Retrieves Bazel Actions:** It extracts the underlying compiler commands used by Bazel to build the targets.
3. **De-Bazelifies Commands:** The tool removes Bazel-specific paths and constructs, making the commands more compatible with external tools.
4. **Generates JSON Output:** It assembles the normalized commands into a well-formatted JSON Compilation Database.

## Example Usage

From within a Bazel workspace:

  ```bash
  bazel run //hardware/generic/goldfish/development/tools/bcc:extract-cc -- //hardware/..
  ```

If installed via pip

  ```bash
  extract-cc -o ~/src/emu/dev/hardware/google/gfxstream/ @glib//:glib-static //hardware/google/gfxstream/host:gfxstream_backend
  ```

Note: Make sure you output the `compile_commands.json` file to a location where your compiler tools expect them! For example when using vscode you usually want it
to be in the root of the workspace that contains your packages.

## Alternatives

We considered using the Bazel Compile Commands Extractor (<https://github.com/hedronvision/bazel-compile-commands-extractor>). However, we determined the following incompatibilities and concerns with our project:

* *Package Structure*: Our reliance on the //external directory is not supported by the tool.
* *Toolchain Customization*: Our custom clang toolchains require transformations beyond the tool's capabilities.
* *Licensing Ambiguity*: The tool's licensing terms were custom, making us hesitant to adopt it for our project.
