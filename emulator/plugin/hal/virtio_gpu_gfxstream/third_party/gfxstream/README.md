# Vendored gfxstream headers

`virtio-gpu-gfxstream-renderer.h` and `virtio-gpu-gfxstream-renderer-unstable.h`
are verbatim copies of `hardware/google/gfxstream/host/include/gfxstream/`
(gfxstream commit `69d5eb409`, "fix(vulkan): Keep reused reverse handle mappings").

They are vendored because `@gfxstream//host:gfxstream_backend_headers` carries
link options (`-Wl,--warn-backrefs -Wl,--fatal-warnings`) that the goldfish
plugin cannot satisfy. Only the type and constant definitions are used; every
`stream_renderer_*` entry point is resolved at runtime by
`gfxstream_backend_loader.c`, so a header drift only matters when a struct
layout or an `enum` value changes. Refresh both files together whenever
gfxstream bumps `STREAM_RENDERER_VERSION_*`.
