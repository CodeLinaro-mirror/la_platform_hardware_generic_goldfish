"""Bazel rules for launching and testing Android emulators."""

load("@rules_python//python:defs.bzl", "py_test")

def create_launch_emulator_test(name, target_log_line = None, timeout_seconds = None, repeat = 0):
    """Tests that the emulator can launch and that the given target_log_line regular expression is logged.

    This macro creates a `py_test` target configured to launch an Android emulator
    and verify its startup behavior. It automatically selects the correct
    architecture (ARM64 for macOS, x86_64 for others) and includes the necessary
    system images.

    Args:
        name: A unique name for this test target.
        target_log_line: A regular expression string. The
            test will pass only if this pattern is found in the emulator's logs.
        repeat: An optional integer specifying the number of times to repeat the test.
        timeout_seconds: An optional integer specifying the maximum time in seconds
            to wait for the emulator to launch and the log line (if specified) to appear.
    """
    args = ["--target_log_line", target_log_line, "--repeat", str(repeat)]
    if timeout_seconds:
        args.extend(["--timeout_seconds", str(timeout_seconds)])
    _create_launch_emulator_test(name, args, ":goldfish")
    _create_launch_emulator_test(
        name + "_zip",
        args + ["--use_zip"],
        "//hardware/generic/goldfish/emulator:release",
    )

def _create_launch_emulator_test(name, args, goldfish_dep):
    py_test(
        name = name,
        size = "medium",
        timeout = "moderate",
        srcs = ["src/launch_kernel.py"],
        args = select({
            "@platforms//os:macos": [
                "--abi",
                "arm64-v8a",
            ],
            "//conditions:default": [
                "--abi",
                "x86_64",
            ],
        }) + args,
        data = [goldfish_dep] + select({
            "@platforms//os:macos": [
                "//hardware/generic/goldfish/emulator/sdk/system_images/minigbm-arm64-v8a:minigbm",
                "@android_minigbm-arm64-v8a//:system_image",
            ],
            "//conditions:default": [
                "//hardware/generic/goldfish/emulator/sdk/system_images/minigbm-x86_64:minigbm",
                "@android_minigbm-x86_64//:system_image",
            ],
        }) + [
            "//hardware/generic/goldfish/emulator/sdk:sdk-marker-files",
        ],
        main = "src/launch_kernel.py",
        deps = ["@rules_python//python/runfiles"],
        target_compatible_with = [
            # Currently launch and boot tests fail on Mac.
            # TODO(b/435653752): Fix and re-enable for other platforms.
            "@platforms//os:linux",
        ],
    )
