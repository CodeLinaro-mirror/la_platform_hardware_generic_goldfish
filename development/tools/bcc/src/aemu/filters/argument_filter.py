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
import logging
import re
from abc import ABC, abstractmethod
from pathlib import Path
from typing import List
from functools import lru_cache

from aemu.process.bazel import Bazel


class Operation(ABC):
    """
    Abstract base class for all filtering operations. Subclasses must
    define how many parameters they need and how to apply the operation.
    """

    @abstractmethod
    def needs_params(self):
        """
        Returns the number of parameters the operation requires.

        Raises:
            NotImplementedError: Subclasses must implement this method.
        """
        raise NotImplementedError(
            "Subclasses must define how many parameters they need"
        )

    @abstractmethod
    def apply(self, *args):
        """
        Applies the operation to a set of arguments.

        Args:
            *args: Variable number of arguments for the operation.

        Returns:
            The result of the operation, or None if the item(s) should be removed.
        """
        pass


class MultiArgumentFilter(Operation):
    """
    Operation that handles multiple arguments
    """

    def __init__(self, pattern: str, params: int, replace_fn=lambda *args: None):
        """
        Initializes the MultiArgumentFilter.

        Args:
            args: A regex that will trigger the replace function if the FIRST argument matches.
            replace_fn: Function that will be invoked when a argument
                to be replaced
        """
        self.pattern: re.Pattern = re.compile(pattern)
        self.params = params
        self.replace_fn = replace_fn
        assert params > 0

    def needs_params(self):
        """
        Returns the number of parameters required for this operation (at least 1).
        """
        return self.params

    def apply(self, *args) -> str:
        """
        Applies the operation to multiple arguments.

        Args:
            *args: The arguments to process.

        Returns:
            The result of the replacement operation, or None if filtering is needed.
        """
        if args[0] == "-MF":
            pass
        if self.pattern.match(args[0]):  # Check if pattern matches any argument
            return self.replace_fn(*args)

        return args


class SingleArgumentFilter(MultiArgumentFilter):
    """
    Operation that replaces items it matches the given regex
    """

    def __init__(self, pattern: str, replace_fn=lambda x: None):
        """
        Initializes the SingleArgumentFilter.

        Args:
            args: A regex that will trigger the replace function.
            replace_fn: Function that will be invoked when a argument
                to be replaced
        """
        super().__init__(pattern, 1, replace_fn)


class DropSecondArgumentFilter(MultiArgumentFilter):
    # Rename for clarity?
    # Generalize for n parameters matching a regex?
    """
    Operation that removes items if the first argument is in a provided list.
    Ignores the second argument.
    """

    def __init__(self, pattern: str, replace_fn=lambda x, y: None):
        """
        Initializes the DropSecondArgumentFilter.

        Args:
            pattern: A regex that will trigger removal.
            replace_fn: Function that will be invoked when a argument
                to be replaced
        """
        super().__init__(pattern, 2, replace_fn)


class NormalizeFile(Operation):
    """
    Operation that attempts to normalize a file path to an absolute path.
    """

    def __init__(self, bazel: Bazel):
        """
        Initializes the NormalizeFile operation.

        Args:
            bazel: An object containing Bazel build context information.
        """
        self.bazel = bazel
        self.ws = Path(bazel.info["workspace"]).resolve()
        self.exe = Path(bazel.info["execution_root"]).resolve()
        self.out = self.ws / "bazel-out"

    def needs_params(self):
        """
        Returns the number of parameters required for this operation (1).
        """
        return 1

    @lru_cache
    def apply(self, arg: str) -> str:
        """
        Attempts to convert a relative path to an absolute path.

        Note: This operation is quite expensinve, so we cache this call.
              We are often expanding the same paths anyways..

        Args:
            arg:  The relative file path to normalize.

        Returns:
            The absolute file path if it exists, otherwise the original argument.
        """
        potential_paths = [
            Path(arg),  # Check original argument first
            # self.out / re.sub("^bazel-out", str(self.out), arg),
            self.ws / arg,
            self.exe / arg,
        ]

        for path in potential_paths:
            real = path.resolve()
            if real.exists() and not real.is_relative_to(self.exe):
                return str(real)

        return arg


class SysrootFilter(SingleArgumentFilter):
    def __init__(self, bazel):
        super().__init__(["^--sysroot="], self.fixup)
        self.norm = NormalizeFile(bazel)

    def fixup(self, root):
        return "--sysroot=" + self.norm.apply(root[10:])


class AndroidClangFilter(Operation):
    """
    Operation that replaces clang system headers from the aosp toolchain.

    This is rather specific to the AOSP use case of the emulator team, where
    the clang toolchain has include paths that confuse clangd.
    """

    def needs_params(self):
        """
        Returns the number of parameters required for this operation.
        """
        return 2

    def apply(self, fst: str, snd: str) -> str:
        """
        Filters out -isystem headers specific to the aosp clang compiler.
        We recognize the clang compiler by the "clang-r" token.

        Args:
            fst:  The first item.
            snd:  The second item (ignored).

        Returns:
            The original items if they should be kept, otherwise None.
        """
        if fst == "-isystem" and "clang-r" in snd:
            return None
        return [fst, snd]


class DropDeadIncludes(DropSecondArgumentFilter):
    """Filter out includes that are under execution_root or do not exist.

    Note: This might cause us to drop includes of autogenerated files.
    """

    def __init__(self, bazel):
        super().__init__("")
        self.exe = Path(bazel.info["execution_root"]).resolve()

    def apply(self, fst: str, snd: str) -> str:
        if fst in ["-isystem", "-iquote", "-I"]:
            resolved = Path(snd).resolve()
            if not resolved.exists() or resolved.is_relative_to(self.exe):
                return None
            return [fst, str(resolved)]

        return [fst, snd]


def apply_argument_filter(items: List[str], operations: List[Operation]) -> List[str]:
    """
    Applies a list of filtering operations to a list of items.

    Args:
        items: The original list of items to filters.
        operations: A list of Operation objects.

    Returns:
        A new list containing the filtered items.
    """
    filtered_items = items.copy()
    for operation in operations:
        idx = 0
        while idx < len(filtered_items):
            num_params_needed = operation.needs_params()
            if (len(filtered_items) - idx) < num_params_needed:
                idx += 1
                logging.debug("Not enough parameters to handle this case")
                continue

            operation_args = filtered_items[idx : idx + num_params_needed]
            transformed_item = operation.apply(*operation_args)

            if transformed_item == operation_args:
                idx += 1
                continue

            del filtered_items[idx : idx + num_params_needed]
            if isinstance(transformed_item, list) or isinstance(
                transformed_item, tuple
            ):
                for x in range(len(transformed_item)):
                    filtered_items.insert(idx, transformed_item[x])
                    idx += 1

            elif transformed_item:
                filtered_items.insert(idx, transformed_item)
                idx += 1

    return filtered_items
