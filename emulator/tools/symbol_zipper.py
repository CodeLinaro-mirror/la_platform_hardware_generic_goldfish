# Copyright 2025 - The Android Open Source Project
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
import argparse
import datetime
import logging
import sys
import zipfile
from collections import namedtuple
from pathlib import Path

ZIP_EPOCH = datetime.datetime(1980, 1, 1, 0, 0, 0).timestamp()
MODULE_KEYWORD = "MODULE"
Module = namedtuple("MODULE", "operatingsystem architecture id name")


def parse_breakpad_line(line: str) -> Module | None:
    """Parses a breakpad symbol line and returns a concrete object contained in a line.

    See: https://chromium.googlesource.com/breakpad/breakpad/+/refs/heads/main/docs/symbol_files.md

    Args:
        line (str): Line from the breakpad symbol file

    Returns:
        Module | None: A parsed module object, or None if the line is not a MODULE record.

    Example:
        >>> parse_breakpad_line("MODULE Linux x86_64 A1B2C3D4E5F678901234567890ABCDEF0 mymodule.so")
        Module(operatingsystem='Linux', architecture='x86_64', id='A1B2C3D4E5F678901234567890ABCDEF0', name='mymodule.so')
    """
    value = [x.strip() for x in line.split(" ")]
    if value[0] == MODULE_KEYWORD:
        return Module(*value[1:])
    return None


def extract_module_info(symbol_file):
    """Extracts the module information from a Breakpad symbol file.

    Reads the first line of the symbol file, which should contain the
    MODULE record, and parses it to extract the operating system,
    architecture, module ID, and module name.

    Args:
        symbol_file (Path): The path to the Breakpad symbol file.

    Returns:
        Module: A namedtuple containing the module information, or None if parsing fails.
    """
    try:
        with open(symbol_file, "r", encoding="utf-8") as symbol:
            module = parse_breakpad_line(symbol.readline())
            return module
    except Exception as exc:
        raise Exception(f"Error extracting module info from {symbol_file}", exc)


def symbol_destination(symbol_file):
    """Calculates the destination path for a symbol file within the zip archive.

    This function determines where a given symbol file should be placed within
    the zip archive, adhering to the standard Breakpad symbol file layout.

    The Breakpad symbol file layout is structured as follows:

        <module_name>/<debug_id>/<module_name>.sym

    Where:
        - <module_name>: The base name of the module (e.g., 'libexample.so', 'myprogram.exe').
        - <debug_id>: The unique debug identifier for the module (e.g., 'A1B2C3D4E5F678901234567890ABCDEF0').
        - <module_name>.sym: The symbol file itself, with the '.sym' extension appended.

    For example, a symbol file for 'libexample.so' with debug ID 'A1B2C3D4E5F678901234567890ABCDEF0'
    would be placed at:

        libexample.so/A1B2C3D4E5F678901234567890ABCDEF0/libexample.so.sym

    Args:
        symbol_file (Path): The path to the Breakpad symbol file.

    Returns:
        Path: The destination path for the symbol file within the zip archive,
              constructed according to the Breakpad layout.
    """
    module = extract_module_info(symbol_file)

    name = Path(module.name)
    return (name / module.id / name).with_suffix(name.suffix + ".sym")


def configure_logging(logging_level):
    """Configures the logging system to log at the given level

    Args:
        logging_level (_type_): A logging level, or number.
    """
    logging_handler_out = logging.StreamHandler(sys.stdout)
    logging.root.setLevel(logging_level)
    logging.root.addHandler(logging_handler_out)


def is_file(param):
    """Checks to see if this parameter is a file

    Args:
        param (str): The parameter to be validated.

    Raises:
        argparse.ArgumentTypeError: Not a directory or symbol file.

    Returns:
        Path: A path that can be used.
    """
    sym = Path(param)
    if not sym.exists():
        raise argparse.ArgumentTypeError(param + " does not exist.")

    if not sym.is_file():
        raise argparse.ArgumentTypeError(param + " is not a file.")

    return sym


def parse_date(ts):
    ts = datetime.datetime.fromtimestamp(ts, tz=datetime.timezone.utc)
    return (ts.year, ts.month, ts.day, ts.hour, ts.minute, ts.second)


def main():
    parser = argparse.ArgumentParser(
        prog="symbol_zipper",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description="""
        Zip a list of Breakpad symbol files into a structured directory layout
        suitable for consumption by crash reporting tools.

        The directory layout is as follows:

            <module_name>/<debug_id>/<module_name>.sym

        Where:
            - <module_name>: The name of the executable or library (e.g., libhw-display-virtio-gpu-pci-rutabaga.dylib).
            - <debug_id>: The unique debug identifier for the module (e.g., 4C4C444655553144A1925AF7C65861290).
            - <module_name>.sym: The symbol file itself, with the '.sym' extension.

        Example:

            libhw-display-virtio-gpu-pci-rutabaga.dylib/
                4C4C444655553144A1925AF7C65861290/
                libhw-display-virtio-gpu-pci-rutabaga.dylib.sym

            libhw-display-virtio-gpu-rutabaga.dylib/
                4C4C44B555553144A1544053FFE8D5430/
                libhw-display-virtio-gpu-rutabaga.dylib.sym
        """,
    )
    parser.add_argument(
        "symbol_file",
        metavar="symbol",
        type=is_file,
        nargs="*",
        help="One or more Breakpad symbol files to process.",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        dest="verbose",
        default=False,
        action="store_true",
        help="Verbose logging",
    )
    parser.add_argument(
        "-o",
        "--out",
        default=Path("symbols.zip"),
        type=Path,
        help="Path to the output zip file where the symbol files will be stored.",
    )
    parser.add_argument(
        "-t",
        "--timestamp",
        type=int,
        default=ZIP_EPOCH,
        help="The unix time to use for files added into the zip. values prior to"
        " Jan 1, 1980 are ignored.",
    )

    args = parser.parse_args()
    lvl = logging.DEBUG if args.verbose else logging.INFO
    configure_logging(lvl)

    unix_ts = parse_date(max(ZIP_EPOCH, args.timestamp))
    with zipfile.ZipFile(args.out, "w", zipfile.ZIP_DEFLATED, allowZip64=True) as zipf:
        seen_entry = set()
        for path in args.symbol_file:
            arcname = str(symbol_destination(Path(path)))
            if arcname in seen_entry:
                logging.debug("Already exists: %s -> %s", path, arcname)
                continue
            seen_entry.add(arcname)
            logging.debug("Writing %s -> %s", path, arcname)

            zip_info = zipfile.ZipInfo.from_file(path, arcname)
            zip_info.compress_type = zipfile.ZIP_DEFLATED
            zip_info.external_attr = 0o644 << 16
            zip_info.date_time = unix_ts
            with open(path, "rb") as f:
                zipf.writestr(zip_info, f.read())


if __name__ == "__main__":
    main()
