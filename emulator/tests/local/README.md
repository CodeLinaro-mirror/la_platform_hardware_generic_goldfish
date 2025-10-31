# Use A Local Image

Say you have a local image at `/tmp/myimage/x86_64/system.img`

You can use it with `bazel test :cts` by creating a symlink in `local/` like this.

    ln -s /tmp/myimage image

To revert to the default image

    rm image

Note this works for both local and remote invocations (your local image will be uploaded
for the second case).

