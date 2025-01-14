# Getting Started with Android Emulator Development on Windows

This guide will help you set up your Windows machine to build and contribute to the Android Emulator project. We'll be using a PowerShell script to automate most of the setup process.

## Prerequisites

*   **Windows 10 or 11 (64-bit):** Make sure you are running a 64-bit version of Windows.
*   **Administrator Privileges:** You'll need to run the setup script as an administrator.

## Setup Steps

1.  **Obtain the Script:** Get the [`setup_dev_machine.ps1`](./setup_dev_machine.ps1) PowerShell script.

2.  **Open PowerShell as Administrator:**
    *   Search for "PowerShell" in the Windows Start Menu.
    *   Right-click on "Windows PowerShell" and select "Run as administrator."

3.  **Run the Script:**
    *   Navigate to the directory where you saved `setup_dev_machine.ps1` using the `cd` command. For example:

        ```powershell
        cd C:\Path\To\Where\You\Saved\The\Script
        ```

    *   Execute the script:

        ```powershell
        .\setup_dev_machine.ps1
        ```

    *   **Note:** You might be prompted about the PowerShell execution policy. If so, you can usually type `Y` and press Enter to temporarily allow the script to run.

4.  **Wait for Completion:** The script will automatically download and install all the necessary tools and dependencies, including:
    *   Visual Studio 2022 Build Tools with components for C++ development
    *   Git for Windows
    *   Python 3
    *   Bazel (the build system)
    *   Other utilities like Chocolatey (a package manager), and Process Explorer.
    *   The Android Emulator source code.

    This process might take a while, depending on your internet speed and system performance.

5.  **Build the Emulator:** Once the script finishes, it will print a command to the console. This is the command to build the emulator as executed by the build bots. Copy this command. It should look something like this:

    ```
        .\prebuilts\bazel\windows-x86_64\bazel.exe  build //hardware/generic/goldfish/emulator:release
    ```

    Open a new elevated powershell or command prompt, navigate to `C:\src\emu-dev` and paste the command. Hit Enter to start the build process. You should end up with a distribution zip file.


## That's It!

You've now set up your Windows machine for Android Emulator development.  You're ready to start exploring the codebase, making changes, and contributing to the project.

**Important Notes:**

*   The script will set up the source code in the `C:\src\emu-dev` directory.
*   Refer to the [Android Emulator](../../README.MD) project's internal documentation for more advanced topics, coding guidelines, and contribution workflows.

Happy developing!