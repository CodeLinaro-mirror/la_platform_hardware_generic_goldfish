"""Defines a module extension to create repos for local rust crates.

These are dependencies of crosvm and netsim.
"""

load("@bazel_tools//tools/build_defs/repo:local.bzl", "new_local_repository")

def _lrc_impl(module_ctx):
    """Implementation of the local_rust_crates module extension."""

    # First the crosvm deps.
    # Commented deps are now provided by netsim rules below.
    for crate in [
        #"anyhow",
        "bitflags",
        #"byteorder",
        #"cfg-if",
        #"equivalent",
        #"foldhash",
        #"hashbrown",
        #"itoa",
        #"libc",
        #"log",
        #"memchr",
        "memoffset",
        "nix",
        #"once_cell",
        #"proc-macro2",
        #"quote",
        "remain",
        #"ryu",
        #"serde_derive",
        #"serde_json",
        #"serde",
        #"syn",
        #"thiserror-impl",
        #"thiserror",
        #"unicode-ident",
        #"zerocopy-derive",
        #"zerocopy",
    ]:
        new_local_repository(
            name = crate,
            build_file = "@goldfish_crates//:BUILD.{}".format(crate),
            path = "third_party/rust/android-crates-io/crates/{}".format(crate),
        )

    new_local_repository(
        name = "winapi",
        build_file = "@goldfish_crates//:BUILD.winapi",
        path = "hardware/generic/goldfish/third_party/rust/crates/winapi",
    )

    new_local_repository(
        name = "winapi-x86_64-pc-windows-gnu",
        build_file = "@goldfish_crates//:BUILD.winapi-x86_64-pc-windows-gnu",
        path = "hardware/generic/goldfish/third_party/rust/crates/winapi-x86_64-pc-windows-gnu",
    )

    # Needed by netsim below:
    new_local_repository(
        name = "protobuf-rust",
        build_file = "@goldfish_crates//:netsim_build/protobuf-rust.BUILD.bazel",
        path = "third_party/rust/android-crates-io/crates/protobuf",
    )

    new_local_repository(
        name = "pica",
        build_file = "@goldfish_crates//:netsim_build/pica.BUILD.bazel",
        path = "third_party/rust/crates/pica",
    )

    new_local_repository(
        name = "rustutils",
        build_file = "@goldfish_crates//:netsim_build/rustutils.BUILD.bazel",
        path = "system/librustutils/rustutils",
    )

    for crate in [
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
        "env_logger",
        "equivalent",
        "etherparse",
        "flate2",
        "fnv",
        "foldhash",
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
        "glam",
        "grpcio",
        "grpcio-sys",
        "hashbrown",
        "heck",
        "hex",
        "http",
        "httparse",
        "itoa",
        "libc",
        "libz-sys",
        "libz-rs-sys",
        "lock_api",
        "log",
        "memchr",
        "minimal-lexical",
        "mio",
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
        "pest",
        "pest_derive",
        "pest_generator",
        "pest_meta",
        "pin-project-lite",
        "pin-utils",
        "ppv-lite86",
        "prettyplease",
        "proc-macro2",
        "protobuf-json-mapping",
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
        "serde_derive",
        "serde_json",
        "signal-hook-registry",
        "slab",
        "smallvec",
        "socket2",
        "strsim",
        "syn",
        "termcolor",
        "thiserror",
        "thiserror-impl",
        "tokio",
        "tokio-macros",
        "tokio-stream",
        "tokio-util",
        "tungstenite",
        "ucd-trie",
        "unicode-ident",
        "unicode-width",
        "utf-8",
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
    ]:
        new_local_repository(
            name = crate,
            build_file = "@goldfish_crates//:netsim_build/{}.BUILD.bazel".format(crate),
            path = "third_party/rust/android-crates-io/crates/{}".format(crate),
        )

    for crate, version in [
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
    ]:
        new_local_repository(
            name = crate,
            build_file = "@goldfish_crates//:netsim_build/{}.BUILD.bazel".format(crate),
            path = "hardware/generic/goldfish/third_party/rust/crates/{}-{}".format(crate, version),
        )

    return module_ctx.extension_metadata(root_module_direct_deps = "all", root_module_direct_dev_deps = [], reproducible = True)

lrc = module_extension(
    implementation = _lrc_impl,
    tag_classes = {},
)
