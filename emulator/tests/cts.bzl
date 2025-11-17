"""Creates a rule that runs CTS."""

load("@rules_python//python:defs.bzl", "py_test")

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
            [
                "cts",
                "-m",
                "CtsDeqpTestCases",
                "--module-arg",
                "CtsDeqpTestCases:include-filter:%s" % smp,
            ],
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
    test_specs = [(m, ["cts", "-m", "%s" % m]) for m in modules]
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
        (plan_file.split("/")[-1], ["PWD/$(location %s)" % plan_file])
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
    for target, tradefed_args in test_specs:
        test = name + "." + target
        tests.append(test)
        py_test(
            name = test,
            main = "run_cts.py",
            srcs = ["run_cts.py"],
            args = [
                "--build_tools_aapt_path",
                "$(location @linux-build-tools//:aapt)",
                "--tradefed_exec_path",
                "$(location @cts-x86-64//:cts-tradefed)",
                "--system_img_path",
                "$(location @android_minigbm-x86_64//:systemimg)",
                "--platform_tools_adb_path",
                "$(location @linux-platform-tools//:adb)",
                "--tradefed_args='%s'" % (",".join(["%s" % arg for arg in tradefed_args]),),
                "--test_seq_path",
                "$(location @test_seq_linux//:test_seq)",
            ],
            size = "enormous",
            data = [
                "@android_minigbm-x86_64//:system_image",
                "@android_minigbm-x86_64//:systemimg",
                "@cts-x86-64//:cts",
                "@cts-x86-64//:cts-tradefed",
                "@goldfish//emulator:release",
                "@linux-build-tools//:aapt",
                "@linux-build-tools//:build-tools",
                "@linux-platform-tools//:adb",
                "@linux-platform-tools//:platform-tools",
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
