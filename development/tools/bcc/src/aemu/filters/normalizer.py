# -*- coding: utf-8 -*-
# Copyright 2024 - The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the',  help='License');
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an',  help='AS IS' BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
import os
from functools import lru_cache
from pathlib import Path
from typing import Optional

from aemu.filters.argument_filter import Operation


class FastPathResolver(object):
    """
    A resolver designed for efficiently checking if relative file or directory
    paths exist within a specified root location.

    Attributes:
        root (Path): The root directory against which relative paths are checked.
        l1 (set):  A set containing the names of subdirectories directly within
                the root directory. This is used for quick membership checks.

    Methods:
        resolve(path: str) -> Optional[str]:
            * Takes a relative path as input.
            * Performs efficient checks to determine if the path potentially exists
              under the root.
            * If the path is determined to exist, returns the absolute resolved path.
            * If the path is unlikely or doesn't exist, returns None.

        __str__() -> str:
            Provides a string representation of the resolver, listing the root path and
            its direct subdirectories.
    """

    def __init__(self, root: Path):
        """
        Initializes the FastPathResolver object.

        Args:
            root (Path): The base directory for performing path resolutions.
        """
        self.root = root.resolve()  # Ensure absolute path for consistency
        self.l1 = set([entry.name for entry in root.iterdir() if entry.is_dir()])

    @lru_cache(maxsize=131072)
    def resolve(self, path: str) -> Optional[str]:
        """
        Checks if a relative path exists within the root directory and returns
        the absolute path if found.

        Args:
            path (str): The relative path to resolve.

        Returns:
            Optional[str]: The absolute resolved path if found, otherwise None.
        """

        if path[0] == "-":  # This is likely a command argument, skip it.
            return None

        entries = path.split(os.sep)

        # Optimization: Check for 'escape' from root with ".." and
        # validate the subdirectory
        if len(path) > 3 and (path.startswith("..") or path.startswith("external")):
            if len(entries) < 2 or entries[1] not in self.l1:
                return None
            possible = self.root.joinpath(*entries[1:])
        else:
            # Default resolution attempt
            if len(entries) < 1 or entries[0] not in self.l1:
                return None
            possible = self.root / path

        if possible.exists():
            return str(possible.resolve())  # Return absolute canonical path

        return None

    def __str__(self):
        """Provides a string representation of the resolver."""
        return f"{self.root}: {','.join(self.l1)}"


class NormalizeFile(Operation):
    """
    An operation responsible for normalizing file paths, primarily by
    attempting to convert relative paths to absolute paths within
    the context of a Bazel workspace.

    Core Functionality:
        * Translates paths within a Bazel sandbox to
          their corresponding workspace locations.
        * Employs a series of `FastPathResolver` objects to efficiently
        locate files within various project directories.

    Attributes:
        bazel_info (dict):   Information about the Bazel build environment,
                 including the execution root and workspace path.
        roots (list): A list of `FastPathResolver` instances targeting
                 different potential file locations
                 (e.g., 'external' directory, workspace root, 'bazel-out').

    Methods:
        __init__(self, bazel_info):
            * Initializes the `NormalizeFile` object.
            * Extracts execution root and workspace paths from `bazel_info`.
            * Creates `FastPathResolver` instances for key project directories.

        needs_params(self):
            * Indicates that this operation expects one parameter (the file path).

        apply(self, arg: str) -> str:
            * Attempts to normalize a relative file path to its absolute form.
            * Iterates through the `roots` trying to resolve the path.
            * If the path is resolved, returns the absolute path.
            * If the path cannot be resolved, returns the original input.
            * Uses an `lru_cache` to cache previous resolutions.
    """

    def __init__(self, bazel_info):
        """
        Initializes the NormalizeFile operation.

        Args:
            bazel_info (dict): A dictionary containing Bazel execution
                environment details.
        """
        exe = Path(bazel_info["execution_root"])
        ws = Path(bazel_info["workspace"])
        self.roots = [
            FastPathResolver(exe.parent.parent / "external"),
            FastPathResolver(ws),
            FastPathResolver(exe),
            FastPathResolver(ws / "bazel-out"),
        ]

    def needs_params(self):
        """
        Indicates that this operation requires one parameter.

        Returns:
            int: The number of parameters (1).
        """
        return 1

    @lru_cache(maxsize=131072)
    def apply(self, arg: str) -> str:
        """
        Normalizes a file path (attempts conversion to absolute form).

        Args:
            arg (str):  The file path (potentially relative).

        Returns:
            str: The absolute file path if successfully resolved,
                seotherwise the original argument.
        """
        for resolver in self.roots:
            path = resolver.resolve(arg)
            if path:
                return path

        return arg
