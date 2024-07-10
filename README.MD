# The Android Emulator

This repository contains a series of plugins and tools to extend QEMU with android emulator functionality. The android emulator is build using bazel.

## Quick Start

You can create a release by running

```sh
bazel build //hardware/generic/goldfish/emulator:release
```

This will produce a zip file for your current architecture. You can unzip this file and launch the emulator. For example on darwin it will produce the following:

```sh
bazel build //hardware/generic/goldfish/emulator:release
INFO: Analyzed target //hardware/generic/goldfish/emulator:release (0 packages loaded, 0 targets configured).
INFO: Found 1 target...
Target //hardware/generic/goldfish/emulator:release up-to-date:
  bazel-bin/hardware/generic/goldfish/emulator/sdk-repo-darwin_aarch64-emulator-developer.zip
INFO: Elapsed time: 0.258s, Critical Path: 0.00s
INFO: 1 process: 1 internal.
INFO: Build completed successfully, 1 total action
```

The launcher executable is called `goldfish`.

### Running a development build

You can also run the emulator directly from within bazel:

```sh
bazel run //hardware/generic/goldfish/emulator/launcher:goldfish -- --avd V --vnc :8
```

This will launch the goldfish emulator with avd *V* and creates a vnc connection on port 5908. You can now attach your favorite debugger to debug any issues.

Note that on linux you will have to set the following source mapping:

```sh
$ lldb
(lldb) settings set target.source-map /proc/self/cwd /my/bazel/workspace/root
```

Where `/my/bazel/workspace/root` points to your bazel workspace root.

## Adding Additional Functionality

The following targets might be of interrest if you wish to add new functionality:

- `//hardware/generic/goldfish/emulator:emulator_unit_tests`: This target contains all the test suites that will be run as part of the build. Make sure your tests are added to this suite.
- `//hardware/generic/goldfish/emulator:release`: The release target. This constructs the zip file with everything that will be distributed. You will need to modify this if you wish to distribute new files.
- `//hardware/generic/goldfish/emulator/launcher:goldfish`: The QEMU launcher. This binary is responsible for configuring the AVD and invoking the qemu emulator with the proper configuration. This is where you configure the drivers, setup the qcow2 images, verify that your system meets the requirements etc..

See [emulator/plugin/README.MD](emulator/plugin/README.MD) if you wish to add additional plugins that need to be loaded into QEMU.

