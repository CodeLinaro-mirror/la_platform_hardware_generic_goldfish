"""Defines a module extension to create repos for local rust crates.

These are dependencies of crosvm and netsim.

Crate Collections:
- _ANDROID_CRATES: Standard crates mirroring `third_party/rust/android-crates-io/crates`.
- _GOLDFISH_UNVERSIONED_CRATES: Legacy unversioned crates in Goldfish (e.g. `winapi`).
- _GOLDFISH_VERSIONED_CRATES: Versioned crates in Goldfish (e.g. `aes-0.8.4`).
- _CUSTOM_PATH_CRATES: Crates with non-standard paths (e.g. `rustutils` in `system`, `pica`).
- _MANUAL_BUILD_CRATES: Legacy crates using build files in Goldfish root (`nix`, `bitflags`, etc.).
"""

load("@bazel_tools//tools/build_defs/repo:local.bzl", "new_local_repository")
load("//:repository_rules.bzl", "patched_new_local_repository")

# Crate locations
_ANDROID_CRATES_IO = "third_party/rust/android-crates-io/crates"
_GOLDFISH_CRATES = "hardware/generic/goldfish/third_party/rust/crates"

def _make_android_crate(name):
    return struct(
        name = name,
        build_file = "@goldfish_crates//netsim_build:{}.BUILD.bazel".format(name),
        path = "{}/{}".format(_ANDROID_CRATES_IO, name),
    )

def _make_versioned_goldfish_crate(name, version):
    return struct(
        name = name,
        build_file = "@goldfish_crates//netsim_build:{}.BUILD.bazel".format(name),
        path = "{}/{}-{}".format(_GOLDFISH_CRATES, name, version),
    )

def _lrc_impl(module_ctx):
    """Implementation of the local_rust_crates module extension."""

    # 1. Goldfish Crates (Unversioned)
    _GOLDFISH_UNVERSIONED_CRATES = [
        "winapi",
        "winapi-x86_64-pc-windows-gnu",
    ]

    for crate in _GOLDFISH_UNVERSIONED_CRATES:
        new_local_repository(
            name = crate,
            build_file = "@goldfish_crates//:BUILD.{}".format(crate),
            path = "{}/{}".format(_GOLDFISH_CRATES, crate),
        )

    # 2. Custom Path Crates
    _CUSTOM_PATH_CRATES = [
        struct(name = "pica", path = "third_party/rust/crates/pica"),
        struct(name = "protobuf-rust", path = "{}/protobuf".format(_ANDROID_CRATES_IO)),
        struct(name = "rustutils", path = "system/librustutils/rustutils"),
    ]

    for crate in _CUSTOM_PATH_CRATES:
        new_local_repository(
            name = crate.name,
            build_file = "@goldfish_crates//netsim_build:{}.BUILD.bazel".format(crate.name),
            path = crate.path,
        )

    # 3. Manual Build Crates (Legacy)
    # These use build files in the root of goldfish_crates.
    _MANUAL_BUILD_CRATES = [
        "bitflags",
        "memoffset",
        "nix",
        "remain",
    ]

    for crate in _MANUAL_BUILD_CRATES:
        new_local_repository(
            name = crate,
            build_file = "@goldfish_crates//:BUILD.{}".format(crate),
            path = "{}/{}".format(_ANDROID_CRATES_IO, crate),
        )

    # 4. Patched Dependencies
    # Patch tempfile to fix API mismatches with newer rustix/errno versions and enforce deterministic RNG.
    patched_new_local_repository(
        name = "tempfile",
        build_file = "@goldfish_crates//netsim_build:tempfile.BUILD.bazel",
        path = "{}/tempfile".format(_ANDROID_CRATES_IO),
        patches = ["@goldfish_crates//netsim_build:tempfile.patch"],
    )

    # 4. Standard Android Crates
    _ANDROID_CRATES = [
        "aho-corasick",
        "android_log-sys",
        "anstyle",
        "anyhow",
        "argh",
        "argh_derive",
        "argh_shared",
        "arrayvec",
        "async-trait",
        "base64",
        "byteorder",
        "bytes",
        "cfg-if",
        "chrono",
        "clap",
        "clap_builder",
        "clap_derive",
        "clap_lex",
        "codespan-reporting",
        "crc32fast",
        "crossbeam-utils",
        "data-encoding",
        "downcast",
        "either",
        "env_logger",
        "equivalent",
        "errno",
        "etherparse",
        "fastrand",
        "flate2",
        "fnv",
        "foldhash",
        "fragile",
        "futures",
        "futures-channel",
        "futures-core",
        "futures-executor",
        "futures-io",
        "futures-macro",
        "futures-sink",
        "futures-task",
        "futures-util",
        "getrandom",
        "gherkin",
        "glam",
        "grpcio",
        "grpcio-sys",
        "hashbrown",
        "heck",
        "hex",
        "http",
        "httparse",
        "indexmap",
        "itoa",
        "lazy_static",
        "libc",
        "libz-sys",
        "libz-rs-sys",
        "lock_api",
        "log",
        "matchers",
        "memchr",
        "minimal-lexical",
        "mio",
        "mockall",
        "mockall_derive",
        "named-lock",
        "nom",
        "num_cpus",
        "num-bigint",
        "num-derive",
        "num-integer",
        "num-traits",
        "once_cell",
        "parking_lot",
        "parking_lot_core",
        "pdl-compiler",
        "pdl-runtime",
        "peg",
        "peg-macros",
        "peg-runtime",
        "pest",
        "pest_derive",
        "pest_generator",
        "pest_meta",
        "pin-project-lite",
        "pin-utils",
        "ppv-lite86",
        "predicates",
        "predicates-core",
        "predicates-tree",
        "prettyplease",
        "proc-macro2",
        "protobuf-codegen",
        "protobuf-json-mapping",
        "protobuf-parse",
        "protobuf-support",
        "quote",
        "rand",
        "rand_chacha",
        "rand_core",
        "regex",
        "regex-automata",
        "regex-syntax",
        "rustix",
        "ryu",
        "scopeguard",
        "serde",
        "serde_core",
        "serde_derive",
        "serde_json",
        "signal-hook-registry",
        "sharded-slab",
        "slab",
        "smallvec",
        "socket2",
        "strsim",
        "syn",
        "termcolor",
        "termtree",
        "textwrap",
        "thiserror",
        "thiserror-impl",
        "thread_local",
        "tokio",
        "tokio-macros",
        "tokio-stream",
        "tokio-util",
        "tracing",
        "tracing-attributes",
        "tracing-core",
        "tracing-log",
        "tracing-subscriber",
        "tungstenite",
        "typed-builder",
        "typed-builder-macro",
        "ucd-trie",
        "unicode-ident",
        "unicode-width",
        "utf-8",
        "which",
        "windows_aarch64_gnullvm",
        "windows_i686_gnullvm",
        "windows_x86_64_gnullvm",
        "windows-link",
        "windows-sys",
        "windows-targets",
        "zerocopy",
        "zerocopy-derive",
        "zip",
        "zlib-rs",
    ]

    for crate_name in _ANDROID_CRATES:
        crate = _make_android_crate(crate_name)
        new_local_repository(
            name = crate.name,
            build_file = crate.build_file,
            path = crate.path,
        )

    # 5. Versioned Goldfish Crates
    _GOLDFISH_VERSIONED_CRATES = [
        ("aead", "0.5.2"),
        ("aes", "0.8.4"),
        ("ccm", "0.5.0"),
        ("cipher", "0.4.4"),
        ("cpufeatures", "0.2.16"),
        ("crypto-common", "0.1.6"),
        ("ctr", "0.9.2"),
        ("generic-array", "0.14.7"),
        ("inout", "0.1.3"),
        ("subtle", "2.6.1"),
        ("typenum", "1.17.0"),
        ("winapi-util", "0.1.9"),
    ]

    for name, version in _GOLDFISH_VERSIONED_CRATES:
        crate = _make_versioned_goldfish_crate(name, version)
        new_local_repository(
            name = crate.name,
            build_file = crate.build_file,
            path = crate.path,
        )

    return module_ctx.extension_metadata(root_module_direct_deps = "all", root_module_direct_dev_deps = [], reproducible = True)

lrc = module_extension(
    implementation = _lrc_impl,
    tag_classes = {},
)
