"""Bazel Transitions for Emulator Release Variants.

In Bazel, 'transitions' allow a target to change the build configuration (flags)
for itself and its entire dependency graph. This is useful for creating
variants of a target (like an internal vs standard release) without needing
to duplicate all the intermediate targets or pass flags on the command line.
"""

def _internal_fishtank_transition_impl(_settings, _attr):
    """Implementation of the transition that flips the internal_fishtank flag."""

    # This dictionary defines the flags we want to change.
    # We are setting //emulator:internal_fishtank to True for this build branch.
    return {"//emulator:internal_fishtank": True}

# Define the transition.
# 'inputs' are flags the transition needs to read (none here).
# 'outputs' are flags the transition will modify.
internal_fishtank_transition = transition(
    implementation = _internal_fishtank_transition_impl,
    inputs = [],
    outputs = ["//emulator:internal_fishtank"],
)

def _internal_release_impl(ctx):
    """Rule implementation that forwards files from the transitioned target."""

    # Because we applied the transition to the 'actual' attribute,
    # the files in ctx.files.actual are built with internal_fishtank = True.
    # We simply wrap those files and return them as the output of this rule.
    return [
        DefaultInfo(files = depset(ctx.files.actual)),
    ]

internal_release = rule(
    implementation = _internal_release_impl,
    attrs = {
        # The 'actual' attribute points to the target we want to build (e.g., :release).
        # By setting 'cfg', we tell Bazel to apply our 'flag-flipping' transition
        # to that target and everything it depends on.
        "actual": attr.label(cfg = internal_fishtank_transition),

        # This is a mandatory Bazel boilerplate attribute required to use
        # Starlark transitions in a rule.
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
    },
)
