# QEMU plugins

Here you will find the set of qemu plugins that can be loaded into QEMU. Plugins are shared libraries that can contain qemu devices. These devices can be loaded from the qemu command line. For example from the distribution you can load the sample device as follows:

```sh
./bin/qemu-system-x86_64 -device sample,message=hello
```

This informs qemu to instantiate the device `sample`, with the parameter `message` set to the value `hello`.

## Device Registration

In order for QEMU to know about a plugin you must update [qemu-plugin-registration.c](registration/src/android/goldfish/qemu-plugin-registration.c). This contains information about which plugins are available to qemu. If you do not add your module to this file it will not be available to QEMU as it will not know that the plugin exists. See a short sample snippet here:

```c
    {
        .name = LIB_PREFIX "sample-base",
        .objs = ((const char *[]){"sample-base", NULL}),
    },
    {
        .name = LIB_PREFIX "sample",
        .opts = ((const char *[]){"device", NULL}),
        .objs = ((const char *[]){"sample", NULL}),
        .deps = ((const char *[]){LIB_PREFIX "sample-base", NULL}),
    },

```

The snippet above informs QEMU that there is a module called `sample` available that offersw the objects `sample` that is a `device` object. The `.arch` field can be used to restrict the device to a specific architecture. Whenever QEMU encounters the `-device` flag it will resolve the shared library sample, load it into QEMU and perform the standard device registration. The `qemu-plugin-registration.c` contains all the device that we register, and contains some examples of dependencies as well.

Note that the sample device depends on the sample-base device. QEMU will make sure that if you use the `sample` device that the `sample-base` module will be loaded as well.

## Device Registration Order

Devices are initialized in the order that they appear on the command line.  For example:

```sh
./bin/qemu-system-x86_64 -device sample-base,base=foo -device sample,message=hello
```

Will initialize create the `sample-base` device and initialize it before the `sample` device is registered and initialized.

## Adding New Plugins

When you add a new plugin you must take care of the following things:

- Register your plugin in [qemu-plugin-registration.c](registration/src/android/goldfish/qemu-plugin-registration.c).
- Distribute your plugin by adding it to `//hardware/generic/goldfish/emulator:plugins`
- Make it available to `bazel run` by adding it to `//hardware/generic/goldfish/emulator/launcher:plugins`

## CPU dependent devices

If your drivers are architecture specific you will need to add the proper definitions for the target of interest.
For example if your device is arm64 only you will need to add the following set of defines:

```python
   defines = [
        "NEED_CPU_H",
        'CONFIG_TARGET=\\"aarch64-softmmu-config-target.h\\"',
        'CONFIG_DEVICES=\\"aarch64-softmmu-config-devices.h\\"',
    ]
```
