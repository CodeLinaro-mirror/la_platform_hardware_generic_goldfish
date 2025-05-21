# This directory contains a set of bazel build files
# that enable the compilation of packages that are found
# in /third_party/rust/crates/...
#
# Here is the rationale behind the bazel build files living here:
#
# - The Rust team does not explicitly support bazel:
#    - There are no automated tools that convert Cargo -> Bazel (as there is for android.bp)
#    - The Rust team does not have the bandwidth to support bazel directly
# - The emulator team takes ownership of the crate to bazel translation. That is if the crates
#   in aosp are updated, and it breaks the emulator build, the emulator team will update the bazel build
#   to make them work
# - No other team will take dependencies on these build files.
