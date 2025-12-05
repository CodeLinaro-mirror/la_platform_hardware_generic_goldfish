from pathlib import Path
from typing import Set, Dict


class Config:
    """Holds all configuration for the Basilisk refactoring tool.

    Attributes:
        REPO_ROOT: The absolute path to the root of the repository to be refactored.
        IGNORE_DIRS: A set of directory names to ignore during the scan.
        READ_ONLY_DIRS: A set of directory names that should not be modified.
        MANUAL_OVERRIDES: A dictionary mapping original paths to their new, manually-specified paths.
        STD_LIB_HEADERS: A set of standard C/C++ library headers to ignore when resolving includes.
    """

    def __init__(self, root: Path):
        """Initializes the Config object.

        Args:
            root: The root directory of the repository to refactor.
        """
        self.REPO_ROOT: Path = root.resolve()
        self.IGNORE_DIRS: Set[str] = {
            ".git",
            "bazel-bin",
            "bazel-out",
            "bazel-testlogs",
            "external",
            ".idea",
            ".vscode",
        }
        self.READ_ONLY_DIRS: Set[str] = {"third_party"}
        self.MANUAL_OVERRIDES: Dict[str, str] = {
            "emulator/hal/camera/QemuMultidisplay/multi_display.h": "emulator/hal/camera/include/goldfish/multi_display.h",
            "emulator/config/src/android/goldfish/config/keys.h": "emulator/config/keys.h",
        }
        self.STD_LIB_HEADERS: Set[str] = {
            "misc.h",
            "android/utils/misc.h",
            "assert.h",
            "complex.h",
            "ctype.h",
            "errno.h",
            "fenv.h",
            "float.h",
            "inttypes.h",
            "iso646.h",
            "limits.h",
            "locale.h",
            "math.h",
            "setjmp.h",
            "signal.h",
            "stdalign.h",
            "stdarg.h",
            "stdatomic.h",
            "stdbool.h",
            "stddef.h",
            "stdint.h",
            "stdio.h",
            "stdlib.h",
            "stdnoreturn.h",
            "string.h",
            "tgmath.h",
            "threads.h",
            "time.h",
            "uchar.h",
            "wchar.h",
            "wctype.h",
            "algorithm",
            "any",
            "array",
            "atomic",
            "bitset",
            "charconv",
            "chrono",
            "codecvt",
            "complex",
            "condition_variable",
            "deque",
            "exception",
            "execution",
            "filesystem",
            "forward_list",
            "fstream",
            "functional",
            "future",
            "initializer_list",
            "iomanip",
            "ios",
            "iosfwd",
            "iostream",
            "istream",
            "iterator",
            "limits",
            "list",
            "locale",
            "map",
            "memory",
            "memory_resource",
            "mutex",
            "new",
            "numeric",
            "optional",
            "ostream",
            "queue",
            "random",
            "ratio",
            "regex",
            "scoped_allocator",
            "set",
            "shared_mutex",
            "sstream",
            "stack",
            "stdexcept",
            "streambuf",
            "string",
            "string_view",
            "strstream",
            "system_error",
            "thread",
            "tuple",
            "type_traits",
            "typeindex",
            "typeinfo",
            "unordered_map",
            "unordered_set",
            "utility",
            "valarray",
            "variant",
            "vector",
            "unistd.h",
            "fcntl.h",
            "sys/types.h",
            "sys/stat.h",
            "sys/time.h",
            "pthread.h",
            "semaphore.h",
            "dlfcn.h",
            "dirent.h",
        }
