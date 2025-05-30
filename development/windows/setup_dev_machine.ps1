# Sets up a new Windows Machine to build emulator
#
# This script will install all the dependencies needed to
# build the emulator. Once this is completed you should be
# able to run
# C:\src\main-emu-dev-next\tools\buildSrc\servers\build.py --dist \tmp --build-id P123 --target windows_x64
#
# You will have to run this script using an elevated prompt


## Stop on action error.
$ErrorActionPreference = 'Stop'
$ConfirmPreference = 'None'

$repo_root = 'c:\src'

## Use only the global PATH, not any user-specific bits.
$env:PATH = [Environment]::GetEnvironmentVariable('PATH', 'Machine')

## Load PowerShell support for ZIP files.
Add-Type -AssemblyName 'System.IO.Compression.FileSystem'

## Create C:\temp
Write-Host 'Creating temporary folder C:\temp...'
if (-not (Test-Path 'c:\temp')) {
    New-Item 'c:\temp' -ItemType 'directory'
}
[Environment]::SetEnvironmentVariable('TEMP', 'C:\temp', 'Machine')
[Environment]::SetEnvironmentVariable('TMP', 'C:\temp', 'Machine')
[Environment]::SetEnvironmentVariable('TEMP', 'C:\temp', 'User')
[Environment]::SetEnvironmentVariable('TMP', 'C:\temp', 'User')
$env:TEMP = [Environment]::GetEnvironmentVariable('TEMP', 'Machine')
$env:TMP = [Environment]::GetEnvironmentVariable('TMP', 'Machine')


## Exclude \src from Windows Defender.
# Removing Defender entirely seems to cause GCESysprep at the end of the script to get stuck.
Set-MpPreference -DisableRealtimeMonitoring $true
Add-MpPreference -ExclusionPath $repo_root

## Enable developer mode.
Write-Host 'Enabling developer mode...'
& reg add 'HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock' /t REG_DWORD /f /v 'AllowDevelopmentWithoutDevLicense' /d '1'

## Enable long paths.
Write-Host 'Enabling long paths...'
& reg add 'HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\FileSystem' /t REG_DWORD /f /v 'LongPathsEnabled' /d '1'

# Set PSGallery to Trusted to avoid prompt on 'Install-Module PowerShellGet'
# See: https://github.com/PowerShell/PowerShellGetv2/issues/461#issuecomment-658669546
Set-PSRepository -Name 'PSGallery' -InstallationPolicy Trusted
# PowerShell Gallery only supports TLS 1.2+
# See: https://devblogs.microsoft.com/powershell/powershell-gallery-tls-support/
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

## Install support for managing NTFS ACLs in PowerShell.
Write-Host 'Installing NTFSSecurity module...'
Install-Module -Name NTFSSecurity
# Done installing PowerShell modules, set PSGallery back to Untrusted.
Set-PSRepository -Name 'PSGallery' -InstallationPolicy Untrusted

# Note: This will fail on non google machines.
& googet -noconfirm install git-google

## Install Chocolatey
Write-Host 'Installing Chocolatey...'
# Chocolatey adds 'C:\ProgramData\chocolatey\bin' to global PATH.
$wc = New-Object System.Net.WebClient
Invoke-Expression ($wc.DownloadString('https://chocolatey.org/install.ps1'))
& choco feature enable -n allowGlobalConfirmation
$env:PATH = [Environment]::GetEnvironmentVariable('PATH', 'Machine')

## Install Git for Windows.
Write-Host 'Installing Git for Windows...'
# FYI: choco adds 'C:\Program Files\Git\cmd' to global PATH.
& choco install git --params "'/GitOnlyOnPath'"  > $null
$env:PATH = [Environment]::GetEnvironmentVariable('PATH', 'Machine')
# Don't convert the line endings when cloning the repository because that could break some tests
& git config --system core.autocrlf false
# Make sure global symlink option is on
& git config --system core.symlinks true
& git config --system core.longpaths true
# Do not detect credential providers (github, bitbucket, etc), avoids slowing down git fetches.
& git config --system credential.provider generic
# Disable interactive credential prompt, otherwise git will hang when git-cookie-authdaemon fails.
& git config --system credential.interactive false

## Build prebuilts with MSVC 2022
Write-Host 'Installing VS 2022 build tools...'
$vs_packages = '--add Microsoft.VisualStudio.Workload.VCTools ' `
  + '--add Microsoft.VisualStudio.Component.VC.ATLMFC ' `
  + '--add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ' `
  + '--add Microsoft.VisualStudio.Component.VC.Llvm.Clang ' `
  + '--add Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset ' `
  + '--add Microsoft.VisualStudio.Component.Windows11SDK.22621 ' `
  + '--add Microsoft.VisualStudio.Component.Windows10SDK.19041 '
& choco install visualstudio2022buildtools --package-parameters $vs_packages > $null
[Environment]::SetEnvironmentVariable('VS2022_INSTALL_PATH', 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools', 'Machine')
$env:VS2022_INSTALL_PATH = [Environment]::GetEnvironmentVariable('VS2022_INSTALL_PATH', 'Machine')

## Requirements for building Emulator with MSVC
Write-Host 'Installing Visual Studio...'
& choco install visualstudio2022community visualstudio2022-workload-nativedesktop  > $null
[Environment]::SetEnvironmentVariable('BAZEL_VC', 'C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC', 'Machine')
$env:BAZEL_VC = [Environment]::GetEnvironmentVariable('BAZEL_VC', 'Machine')
& choco install windowsdriverkit10  > $null


## Install Python3
Write-Host 'Installing Python 3...'
& choco install python311 > $null
$env:PATH = [Environment]::GetEnvironmentVariable('PATH', 'Machine')
& py -m pip install --upgrade certifi pip html5lib

## Process Explorer (helpful for debugging buildbot/bazel processes).
Write-Host 'Installing Process Explorer...'
& choco install procexp  > $null
## Next we are going to check out our repository
Write-Host 'Installing curl & repo..'
& choco install curl

if (-not (Test-Path "$Env:USERPROFILE\bin")) {
  Write-Verbose "Creating directory $Env:USERPROFILE\bin"
  & mkdir "$Env:USERPROFILE\bin" | Out-Null  # Suppress mkdir output
} else {
  Write-Verbose "Directory $Env:USERPROFILE\bin already exists."
}


& cd "$Env:USERPROFILE\bin"
& curl -o repo http://storage.googleapis.com/git-repo-downloads/repo
Set-Content -Path "$Env:USERPROFILE\bin\repo.cmd" -Value "@call python ""$Env:USERPROFILE\bin\repo"" %*" -Encoding ascii

# Add %USERPROFILE%\bin to the permanent PATH
$userBinPath = "$Env:USERPROFILE\bin"
if (-not ($Env:Path -like "*$userBinPath*")) {
    $newPath = "$userBinPath;$Env:Path"
    [Environment]::SetEnvironmentVariable("PATH", $newPath, "Machine")  # Or "User" for user-specific
    $Env:Path = [Environment]::GetEnvironmentVariable("PATH", "Machine") # Or "User"
    Write-Host "Added $userBinPath to PATH"
} else {
    Write-Host "$userBinPath already in PATH"
}

Write-Host 'Syncing the repository'
# Create the source directory if it doesn't exist.
if (-not (Test-Path $repo_root)) {
  New-Item -ItemType Directory -Path $repo_root
}

# Change to the source directory and create/change to main-emu-dev-next
cd $repo_root
if (-not (Test-Path main-emu-dev-next)) {
  New-Item -ItemType Directory -Path main-emu-dev-next
}
cd main-emu-dev-next

# Initialize and sync the repo.
# Change this line if you need to use a different repository.
repo init -c -u https://android.googlesource.com/platform/manifest -b main-emu-next-dev
repo sync


$newPath = "C:\src\main-emu-dev-next\prebuilts\bazel\windows-x86_64\;$Env:Path"
[Environment]::SetEnvironmentVariable("PATH", $newPath, "Machine")  # Or "User" for user-specific
$Env:Path = [Environment]::GetEnvironmentVariable("PATH", "Machine") # Or "User"
Write-Host "Added C:\src\main-emu-dev-next\prebuilts\bazel\windows-x86_64\ to PATH"

Write-Host 'All done, you might have to launch visual studio once to complete.'
Write-Host 'bazel build //hardware/generic/goldfish/emulator:release'