"""Bazel rules for launching and testing Android emulators."""

load("@rules_python//python:defs.bzl", "py_test")

def create_launch_emulator_test(name, target_log_line = None, timeout_seconds = None, repeat = 0, params = None):
    """Tests that the emulator can launch and that the given target_log_line regular expression is logged.

    This macro creates a `py_test` target configured to launch an Android emulator
    and verify its startup behavior. It automatically selects the correct
    architecture (ARM64 for macOS, x86_64 for others) and includes the necessary
    system images.

    Note that tests will use the tag: `exclusive-if-local` this will force the test to be run
    in the "exclusive" mode if it is executed locally, but will run the test in parallel if it's
    executed remotely.

    Args:
        name: A unique name for this test target.
        target_log_line: A regular expression string. The
            test will pass only if this pattern is found in the emulator's logs.
        repeat: An optional integer specifying the number of times to repeat the test.
        timeout_seconds: An optional integer specifying the maximum time in seconds
            to wait for the emulator to launch and the log line (if specified) to appear.
        params: Additional parameters that need to be passed on to the emulator launcher.
    """
    args = ["--target_log_line", target_log_line, "--repeat", str(repeat)]
    if params:
        args += params
    if timeout_seconds:
        args.extend(["--timeout_seconds", str(timeout_seconds)])
    _create_launch_emulator_test(name, args, "@goldfish//emulator/launcher")
    _create_launch_emulator_test(
        name + "_zip",
        args + ["--use_zip"],
        "@goldfish//emulator:release",
    )

def _create_launch_emulator_test(name, args, goldfish_dep):
    py_test(
        name = name,
        size = "medium",
        timeout = "moderate",
        # These tests are marked as manual so that they aren't included when using //...
        # Instead they must be explicitly named (including as part of a test_suite).
        tags = ["manual", "exclusive-if-local", "requires-network"],
        args = select({
            "@platforms//os:macos": [
                "--abi",
                "arm64-v8a",
            ],
            "//conditions:default": [
                "--abi",
                "x86_64",
            ],
        }) + args + [
            "--disable-crash-reporting",
            "-verbose",
            "-read-only",
        ],
        main_module = "launch_emulator",
        deps = ["@goldfish//emulator/launcher:launch_emulator"],
        data = [goldfish_dep],
        imports = ["tools"],
        target_compatible_with = select({
            "@platforms//os:windows": ["@platforms//:incompatible"],
            "//conditions:default": [],
        }),
    )
