"""Creates a rule that runs CTS."""

load("@rules_shell//shell:sh_test.bzl", "sh_test")

def deqp_tests(name, submodules = []):
    """Creates a set of rules that runs CTS deqp submodules.

    Args:
      name: The name of the rule
      submodules: A list of submodules to create rules for of the form
          <name>.<submodule>
    """
    test_specs = [
        (
            smp,
            (
                'args: "cts" ' +
                'args: "-m" args: "CtsDeqpTestCases" ' +
                'args: "--module-arg" ' +
                'args: "CtsDeqpTestCases:include-filter:%s"' % smp
            ),
        )
        for smp in submodules
    ]
    cts_test_specs(name, test_specs)

def cts_tests(name, modules = []):
    """Creates a set of rules that runs CTS modules.

    Args:
      name: The name of the rule
      modules: A list of modules to create rules for of the form
          <name>.<module>
    """
    test_specs = [(m, 'args: "cts" args: "-m" args: "%s"' % m) for m in modules]
    cts_test_specs(name, test_specs)

def cts_plan(name, plan_glob):
    """Creates a set of rules that runs CTS modules.

    Args:
      name: The name of the rule
      plan_glob: A glob pattern of tests to include.  e.g.
          xts_test_plans/presubmit/**
    """
    additional_plan_files = native.glob([plan_glob])
    test_specs = [
        (plan_file.split("/")[-1], 'args: "PWD/$(location %s)"' % plan_file)
        for plan_file in additional_plan_files
    ]
    cts_test_specs(name, test_specs, additional_plan_files)

def cts_test_specs(name, test_specs = [], additional_plan_files = []):
    """Creates a set of rules that runs CTS with a given test specification.

    Args:
      name: The name of the rule
      test_specs: The test spec proto that will be passed to the tradefed agent
      additional_plan_files: Additional plan files to inlcude in the dependency list
    """
    tests = []
    for target, test_spec in test_specs:
        test = name + "." + target
        tests.append(test)
        sh_test(
            name = test,
            srcs = ["run_cts.sh"],
            env = {
                "BUILD_TOOLS_PATH": "$(location @linux-build-tools//:aapt)",
                "CTS_TRADEFED_PATH": "$(location @cts-x86-64//:cts-tradefed)",
                "IMAGE_PATH": "$(location @android_minigbm-x86_64//:systemimg)",
                "PLATFORM_TOOLS_PATH": "$(location @linux-platform-tools//:adb)",
                "TEST_SPEC": test_spec,
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
            ] + native.glob(["local/**"]) + additional_plan_files,
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
