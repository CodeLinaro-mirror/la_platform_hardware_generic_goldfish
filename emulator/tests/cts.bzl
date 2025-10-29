"""Creates a rule that runs CTS."""

load("@rules_shell//shell:sh_test.bzl", "sh_test")

def cts_tests(name, modules = []):
    """Creates a set of rules that runs CTS modules.

    Args:
      name: The name of the rule
      modules: A list of modules to create rules for of the form
          <name>.<module>
    """
    tests = []
    for m in modules:
        test = name + "." + m
        tests.append(test)
        sh_test(
            name = test,
            srcs = ["run_cts.sh"],
            env = {
                "BUILD_TOOLS_PATH": "$(location @linux-build-tools//:aapt)",
                "CTS_TRADEFED_PATH": "$(location @cts-x86-64//:cts-tradefed)",
                "IMAGE_PATH": "$(location @android_minigbm-x86_64//:systemimg)",
                "PLATFORM_TOOLS_PATH": "$(location @linux-platform-tools//:adb)",
                "TEST_SPEC": 'args: "-m" args: "%s"' % m,
                "TEST_SEQ_PATH": "$(location @test_seq_linux//:test_seq)",
            },
            size = "enormous",
            data = [
                "@android_minigbm-x86_64//:system_image",
                "@android_minigbm-x86_64//:systemimg",
                "@cts-x86-64//:cts",
                "@cts-x86-64//:cts-tradefed",
                "//hardware/generic/goldfish/emulator:release",
                "@linux-build-tools//:aapt",
                "@linux-build-tools//:build-tools",
                "@linux-platform-tools//:adb",
                "@linux-platform-tools//:platform-tools",
                "sequence.txtpb",
                "@test_seq_linux//:test_seq_files",
                "@test_seq_linux//:test_seq",
            ],
            target_compatible_with = select({
                "@platforms//os:macos": ["@platforms//:incompatible"],
                "@platforms//os:windows": ["@platforms//:incompatible"],
                "//conditions:default": [],
            }),
            # Note: mac platforms may need requires-network for GRPC to work
            tags = ["manual"],
        )

    native.test_suite(
        name = name,
        tests = tests,
        tags = ["manual"],
    )
