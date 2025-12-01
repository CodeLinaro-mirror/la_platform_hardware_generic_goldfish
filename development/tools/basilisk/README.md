# Basilisk: C++ Repository Standardization Tool

Basilisk is a tool for standardizing the layout of a C++ repository that uses Bazel. It refactors the file structure based on a set of predefined rules, updating file paths and references within source files and `BUILD.bazel` files.

## Usage

The tool is run from the command line:

```bash
python3 -m development.tools.basilisk.basilisk --repo_root /path/to/your/repo [options]
```

### Arguments

* `--repo_root` (required): The absolute path to the root directory of the repository you want to refactor.
* `--execute` (optional): By default, Basilisk runs in "dry run" mode and only prints the changes it would make. Use this flag to apply the changes to the file system.
* `-v`, `--verbose` (optional): Enable verbose (DEBUG level) logging to see more details about the refactoring process.

## Refactoring Rules

Basilisk applies the following rules to C++ source and header files within a Bazel module (a directory containing a `BUILD` or `BUILD.bazel` file):

### File Naming

* All C++ and C file names (stems) are converted to `snake_case`. For example, `MyClass.cpp` becomes `my_class.cc`.

### C++ Files (`.cpp`, `.cc`)

* **Extension:** `.cpp` files are renamed to `.cc`.
* **Location:** All C++ source files are moved (flattened) into the `src/` directory of their module. For example, `foo/bar/MyClass.cpp` in module `foo` would be moved to `foo/src/my_class.cc`.

### Header Files (`.h`, `.hpp`)

The tool distinguishes between three types of headers:

1. **C-Style Headers:**
    * **Detection:** Any header file (`.h` or `.hpp`) that does **not** contain a `namespace` declaration is considered a C-style header.
    * **Action:** These headers are renamed to `snake_case.h` but **remain in their original directory**. They are not moved.

2. **Private C++ Headers:**
    * **Detection:** Any header file located within a `src/` directory.
    * **Action:** These headers are renamed to `snake_case.h` and flattened into the `src/` directory of their module.

3. **Public C++ Headers:**
    * **Detection:** Any header file containing a `namespace` declaration that is **not** in a `src/` directory (typically in an `include/` directory).
    * **Action:** These headers are renamed to `snake_case.h` and moved into a subdirectory of `include/` that matches their primary namespace. For example, a header with `namespace a::b;` would be moved to `include/a/b/`.

### C Source Files (`.c`)

* **Extension:** The `.c` extension is preserved.
* **Location:** All C source files are moved (flattened) into the `src/` directory of their module, similar to C++ source files.

### Reference Updates

* The tool will automatically update `#include` paths in C++ and C files to point to the new locations.
* It will also update file paths in `BUILD` and `BUILD.bazel` files.
