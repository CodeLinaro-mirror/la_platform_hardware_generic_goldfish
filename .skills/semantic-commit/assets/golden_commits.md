# Golden Commit Examples

## Example 1: Refactoring (Goldfish)

refactor(goldfish): use atomics in RingBuffer

Replaces the global mutex in `RingBuffer::write` with std::atomic
to reduce contention on the RenderThread. This aligns the implementation
with the design in `docs/flows/buffer_sync.md`.

Bug: 12345678
Test: ran `bazel test @goldfish//base:ring_buffer_test`

## Example 2: Feature (GfxStream)

feat(gxstream): add shared memory handle to ScreenService

Updates the `StreamScreenshot` gRPC method to accept an optional
shared memory handle. This allows zero-copy transfer of frames
when the client is on the same host.

Bug: None
Test: verified with `screen_recording_test_client`

## Example 3: Build Fix

build(deps): upgrade libpng to 1.6.37

Fixes a security vulnerability (CVE-2019-7317) by upgrading the
library version. Updated `WORKSPACE` and verified strict aliasing
rules in `BUILD`.

Bug: 87654321
Test: `bazel build //...`
