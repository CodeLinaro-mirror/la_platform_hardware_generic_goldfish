# Getting Started with Android Emulator Development on Linux

This guide will help you set up your Linux machine to build and contribute to the Android Emulator project.

## Prerequisites

*   **A supported Linux Distribution (e.g., Ubuntu, Debian):**  This guide assumes you are using a Debian-based distribution. You'll need to adapt the package installation commands for other distributions.
*   **Superuser (sudo) Privileges:** You'll need privileges to install system-wide packages.

## Setup Steps

1.  **Install Dependencies:**
    Open a terminal and install the required packages using `apt-get`:

    ```bash
    sudo apt-get install -y git build-essential qemu-kvm
    ```

    This command installs the following:
    *   `git`: Version control system.
    *   `build-essential`: Essential tools for compiling C/C++ code.
    *   `qemu-kvm`: Virtualization software for running the emulator.

2.  **Install and Configure `repo`:**

    `repo` is a tool built on top of Git that makes it easier to work with multiple Git repositories, which is common in Android development.

    *   **Download and Set Permissions:**

        ```bash
        mkdir $HOME/bin
        curl [http://commondatastorage.googleapis.com/git-repo-downloads/repo](http://commondatastorage.googleapis.com/git-repo-downloads/repo) > $HOME/bin/repo
        chmod 700 $HOME/bin/repo
        ```

    *   **Add to PATH:**

        ```bash
        export PATH=$PATH:$HOME/bin
        ```

        To make this change permanent, add the `export PATH=$PATH:$HOME/bin` line to your shell's configuration file (e.g., `~/.bashrc` for Bash, `~/.zshrc` for Zsh). Then, source the file to apply the changes immediately:
        ```bash
        source ~/.bashrc  # Or source ~/.zshrc
        ```

3.  **Get the Source Code:**

    *   **Initialize the Repository:**

        ```bash
        mkdir -p $HOME/src/emu-dev && cd $HOME/src/emu-dev
        repo init -u [https://android.googlesource.com/platform/manifest](https://android.googlesource.com/platform/manifest) -b emu-dev
        ```
        This creates a directory for the emulator source code and initializes a `repo` workspace using the `emu-dev` branch of the Android Emulator manifest.

    *   **Download the Code:**

        ```bash
        repo sync
        ```
        This command downloads the source code from all the required Git repositories.

## Build the Emulator

Once the setup is complete and the source code is downloaded, you can build the emulator. Refer to the Android Emulator project's internal documentation for the exact build command, which might look similar to the following.

```bash
    cd $HOME/src/emu-dev
    ./prebuilts/bazel/linux-x86_64/bazel build //hardware/generic/goldfish/emulator:release
```

## That's It!

You've now set up your Linux machine for Android Emulator development.  You're ready to start exploring the codebase, making changes, and contributing to the project.

**Important Notes:**

*   Refer to the [Android Emulator](../../README.MD) project's internal documentation for more advanced topics, coding guidelines, and contribution workflows.

Happy developing!