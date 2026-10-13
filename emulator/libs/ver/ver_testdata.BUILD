filegroup(
    name = "virtualscene_default",
    srcs = glob(["virtualscene/default/*"]),
    visibility = ["//visibility:public"],
)

filegroup(
    name = "testdata_flat",
    srcs = glob(
        [
            "testdata/textureutils/*",
            "testdata/ver/*",
            "testdata/ver/gltf/*",
            "testdata/ver/gltf/Duck/*",
        ],
        exclude_directories = 1,
    ),
    visibility = ["//visibility:public"],
)

filegroup(
    name = "testdata_exif_rotation",
    srcs = glob(["testdata/ver/exif_rotation/*"]),
    visibility = ["//visibility:public"],
)
