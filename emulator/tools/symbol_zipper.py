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
import contextlib
import datetime
import logging
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import zipfile
from collections import namedtuple

ZIP_EPOCH = datetime.datetime(1980, 1, 1, 0, 0, 0, tzinfo=datetime.timezone.utc).timestamp()
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


def extract_module_info(symbol_file: Path) -> Module:
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


def symbol_destination(symbol_file: Path) -> Path:
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


def configure_logging(logging_level: int) -> None:
    """Configures the logging system to log at the given level

    Args:
        logging_level: A logging level, or number.
    """
    logging_handler_out = logging.StreamHandler(sys.stdout)
    logging.root.setLevel(logging_level)
    logging.root.addHandler(logging_handler_out)


def is_file(param: str) -> Path:
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


def parse_date(ts: float):
    ts_dt = datetime.datetime.fromtimestamp(ts, tz=datetime.timezone.utc)
    return (ts_dt.year, ts_dt.month, ts_dt.day, ts_dt.hour, ts_dt.minute, ts_dt.second)


def _force_rmtree(path: Path) -> None:
    """Removes a directory tree, resetting read-only permissions on failure (Windows)."""
    if not path.exists():
        return

    def _on_error(func, p, _):
        try:
            os.chmod(p, 0o777)
            func(p)
        except OSError:
            pass

    shutil.rmtree(path, onerror=_on_error)


@contextlib.contextmanager
def _staging_environment(output_path: Path):
    """Context manager setting up an isolated staging dir and listfile, ensuring cleanup."""
    staging_dir = output_path.parent / (output_path.name + ".staging")
    listfile_path = output_path.parent / (output_path.name + ".list")

    _force_rmtree(staging_dir)
    staging_dir.mkdir(parents=True, exist_ok=True)
    try:
        yield staging_dir, listfile_path
    finally:
        _force_rmtree(staging_dir)
        if listfile_path.exists():
            try:
                listfile_path.unlink()
            except OSError:
                pass


def _stage_file(src: Path, target: Path, timestamp: int) -> None:
    """Stages a file into target path via hardlink or copy, setting timestamp."""
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists() or target.is_symlink():
        try:
            target.unlink()
        except OSError:
            pass
    try:
        os.link(src, target)
    except OSError:
        shutil.copyfile(src, target)
    try:
        target.chmod(0o644)
    except OSError:
        pass
    try:
        os.utime(target, (timestamp, timestamp), follow_symlinks=False)
    except (OSError, NotImplementedError):
        pass


def _normalize_staging_metadata(staging_dir: Path, timestamp: int) -> None:
    """Normalizes directory permissions and deterministic timestamps bottom-up."""
    for root, dirs, _ in os.walk(staging_dir, topdown=False):
        for d in dirs:
            p = Path(root) / d
            if not p.is_symlink():
                try:
                    p.chmod(0o755)
                except OSError:
                    pass
            try:
                os.utime(p, (timestamp, timestamp), follow_symlinks=False)
            except (OSError, NotImplementedError):
                pass

    try:
        staging_dir.chmod(0o755)
        os.utime(staging_dir, (timestamp, timestamp))
    except OSError:
        pass


def _generate_sorted_listfile(staging_dir: Path, listfile_path: Path) -> bool:
    """Generates an explicitly sorted listfile for 7-Zip."""
    items_to_archive: list[str] = []
    for p in staging_dir.rglob("*"):
        rel_str = str(p.relative_to(staging_dir)).replace("\\", "/")
        if p.is_dir() and not p.is_symlink():
            items_to_archive.append(rel_str + "/")
        else:
            items_to_archive.append(rel_str)

    if not items_to_archive:
        return False

    items_to_archive.sort()
    listfile_path.write_text(
        "\n".join(items_to_archive) + "\n",
        encoding="utf-8",
    )
    return True


def _run_sevenzip(
    sevenzip_bin: Path,
    output_path: Path,
    listfile_path: Path,
    staging_dir: Path,
    compression_level: int,
) -> None:
    """Invokes 7-Zip to produce the final archive with multi-threading."""
    if output_path.exists():
        output_path.unlink()

    compression_args = (
        ["-mx=0"]
        if compression_level == 0
        else ["-mm=Deflate", f"-mx={compression_level}"]
    )
    cmd = (
        [
            str(sevenzip_bin),
            "a",
            "-tzip",
        ]
        + compression_args
        + [
            "-mmt=on",
            "-bd",
            "-bso0",
            "-bsp0",
            "-snl",
            "-r-",
            "-mtc=off",
            "-scsUTF-8",
            "-mcu=on",
            "-y",
            str(output_path),
            f"@{listfile_path}",
        ]
    )

    env = dict(os.environ)
    env["TZ"] = "UTC"
    env["LANG"] = "en_US.UTF-8"
    env["LC_CTYPE"] = "UTF-8"

    proc = subprocess.run(
        cmd,
        cwd=staging_dir,
        capture_output=True,
        text=True,
        env=env,
    )
    if proc.returncode != 0:
        sys.stderr.write(
            f"7za failed with exit code {proc.returncode}:\n"
            f"{proc.stderr}\n{proc.stdout}\n"
        )
        sys.exit(proc.returncode)


def _zipfile_fallback(
    output_path: Path,
    symbol_mappings: list[tuple[Path, Path]],
    timestamp: int,
    compression_level: int,
) -> None:
    """Fallback using python's zipfile module if 7za is unavailable."""
    if output_path.exists():
        output_path.unlink()

    compress_type = (
        zipfile.ZIP_STORED if compression_level == 0 else zipfile.ZIP_DEFLATED
    )
    compress_level = None if compression_level == 0 else compression_level
    unix_ts = parse_date(timestamp)

    with zipfile.ZipFile(
        output_path,
        "w",
        compression=compress_type,
        compresslevel=compress_level,
        allowZip64=True,
    ) as zipf:
        for src, dest in symbol_mappings:
            arcname = str(dest).replace("\\", "/")
            zip_info = zipfile.ZipInfo.from_file(src, arcname)
            zip_info.compress_type = compress_type
            zip_info.external_attr = 0o644 << 16
            zip_info.date_time = unix_ts
            with open(src, "rb") as f:
                zipf.writestr(
                    zip_info,
                    f.read(),
                    compress_type=compress_type,
                    compresslevel=compress_level,
                )


def main():
    parser = argparse.ArgumentParser(
        prog="symbol_zipper",
        fromfile_prefix_chars="@",
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
        default=int(ZIP_EPOCH),
        help="The unix time to use for files added into the zip. values prior to"
        " Jan 1, 1980 are ignored.",
    )
    parser.add_argument(
        "-c",
        "--compression-level",
        type=int,
        default=1,
        choices=range(0, 10),
        help="The compression level to use (0-9). 0 is uncompressed (stored), 1 is fastest.",
    )
    parser.add_argument(
        "--sevenzip",
        type=Path,
        default=None,
        help="Path to the 7za executable.",
    )

    args = parser.parse_args()
    lvl = logging.DEBUG if args.verbose else logging.INFO
    configure_logging(lvl)

    timestamp = int(max(ZIP_EPOCH, args.timestamp))

    # Collect and deduplicate mappings (src -> dest)
    seen_entry = set()
    symbol_mappings: list[tuple[Path, Path]] = []
    for path in args.symbol_file:
        dest = symbol_destination(Path(path))
        dest_str = str(dest).replace("\\", "/")
        if dest_str in seen_entry:
            logging.debug("Already exists: %s -> %s", path, dest_str)
            continue
        seen_entry.add(dest_str)
        logging.debug("Mapping %s -> %s", path, dest_str)
        symbol_mappings.append((Path(path), dest))

    # Determine 7za binary
    sevenzip_bin = args.sevenzip
    if not sevenzip_bin or not sevenzip_bin.exists():
        found = shutil.which("7za")
        sevenzip_bin = Path(found) if found else None

    output_path = args.out.resolve()
    if sevenzip_bin and sevenzip_bin.exists() and symbol_mappings:
        sevenzip_bin = sevenzip_bin.resolve()
        with _staging_environment(output_path) as (staging_dir, listfile_path):
            for src, dest in symbol_mappings:
                target = staging_dir / dest
                _stage_file(src.resolve(), target, timestamp)
            _normalize_staging_metadata(staging_dir, timestamp)
            if _generate_sorted_listfile(staging_dir, listfile_path):
                _run_sevenzip(
                    sevenzip_bin,
                    output_path,
                    listfile_path.resolve(),
                    staging_dir,
                    args.compression_level,
                )
            else:
                _zipfile_fallback(
                    output_path, symbol_mappings, timestamp, args.compression_level
                )
    else:
        _zipfile_fallback(
            output_path, symbol_mappings, timestamp, args.compression_level
        )


if __name__ == "__main__":
    main()
