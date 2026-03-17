// Copyright (C) 2014 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#pragma once

#include <cstdlib>
#include <string>
#include <utility>

#include "android/cpu/version.h"

namespace android {

// don't change these numbers
// Android Studio depends on them
using AndroidCpuAcceleration = enum {
    ANDROID_CPU_ACCELERATION_READY = 0,                 // Acceleration is available
    ANDROID_CPU_ACCELERATION_NESTED_NOT_SUPPORTED = 1,  // HAXM doesn't support nested VM
    ANDROID_CPU_ACCELERATION_INTEL_REQUIRED = 2,        // HAXM requires GeniuneIntel processor
    ANDROID_CPU_ACCELERATION_NO_CPU_SUPPORT =
            3,  // CPU doesn't support required features (VT-x or SVM)
    ANDROID_CPU_ACCELERATION_NO_CPU_VTX_SUPPORT = 4,   // CPU doesn't support VT-x
    ANDROID_CPU_ACCELERATION_NO_CPU_NX_SUPPORT = 5,    // CPU doesn't support NX
    ANDROID_CPU_ACCELERATION_ACCEL_NOT_INSTALLED = 6,  // KVM/HAXM package not installed
    ANDROID_CPU_ACCELERATION_ACCEL_OBSOLETE = 7,       // HAXM package is obsolete
    ANDROID_CPU_ACCELERATION_DEV_NOT_FOUND = 8,     // /dev/kvm is not found: VT disabled in BIOS or
                                                    // KVM kernel module not loaded
    ANDROID_CPU_ACCELERATION_VT_DISABLED = 9,       // HAXM is installed but VT disabled in BIOS
    ANDROID_CPU_ACCELERATION_NX_DISABLED = 10,      // HAXM is installed but NX disabled in BIOS
    ANDROID_CPU_ACCELERATION_DEV_PERMISSION = 11,   // /dev/kvm or HAXM device: permission denied
    ANDROID_CPU_ACCELERATION_DEV_OPEN_FAILED = 12,  // /dev/kvm or HAXM device: open failed
    ANDROID_CPU_ACCELERATION_DEV_IOCTL_FAILED = 13,  // /dev/kvm or HAXM device: ioctl failed
    ANDROID_CPU_ACCELERATION_DEV_OBSOLETE = 14,      // KVM or HAXM supported API is too old
    ANDROID_CPU_ACCELERATION_HYPERV_ENABLED =
            15,                            // HyperV must be disabled, unless WHPX is available
    ANDROID_CPU_ACCELERATION_ERROR = 138,  // Some other error occurred
};

// ditto
using AndroidHyperVStatus = enum {
    ANDROID_HYPERV_ABSENT = 0,     // No hyper-V found
    ANDROID_HYPERV_INSTALLED = 1,  // Hyper-V is installed but not running
    ANDROID_HYPERV_RUNNING = 2,    // Hyper-V is up and running
    ANDROID_HYPERV_ERROR = 100,    // Failed to detect status
};

// For any possible Android Studio Hypervisor.framework detection
using AndroidHVFStatus = enum {
    ANDROID_HVF_UNSUPPORTED = 0,  // No Hypervisor.framework support on host system
    ANDROID_HVF_SUPPORTED = 1,    // Hypervisor.framework is supported
    ANDROID_HVF_ERROR = 100,      // Failed to detect status
};

// +1, cpu info
using AndroidCpuInfoFlags = enum {
    ANDROID_CPU_INFO_FAILED = 0,  // this is the value to return if something
                                  // went wrong

    ANDROID_CPU_INFO_AMD = 1 << 0,             // AMD CPU
    ANDROID_CPU_INFO_INTEL = 1 << 1,           // Intel CPU
    ANDROID_CPU_INFO_OTHER = 1 << 2,           // Other CPU manufacturer
    ANDROID_CPU_INFO_VM = 1 << 3,              // Running in a VM
    ANDROID_CPU_INFO_VIRT_SUPPORTED = 1 << 4,  // CPU supports
                                               // virtualization technologies

    ANDROID_CPU_INFO_32_BIT = 1 << 5,            // 32-bit CPU, really!
    ANDROID_CPU_INFO_64_BIT_32_BIT_OS = 1 << 6,  // 32-bit OS on a 64-bit CPU
    ANDROID_CPU_INFO_64_BIT = 1 << 7,            // 64-bit CPU
    ANDROID_CPU_INFO_APPLE = 1 << 8,             //  Apple CPU (such as M1, M2, ...)

};

using AndroidCpuAccelerator = enum {
    ANDROID_CPU_ACCELERATOR_NONE = 0,
    ANDROID_CPU_ACCELERATOR_KVM,
    ANDROID_CPU_ACCELERATOR_HAX,
    ANDROID_CPU_ACCELERATOR_HVF,
    ANDROID_CPU_ACCELERATOR_WHPX,
    ANDROID_CPU_ACCELERATOR_AEHD,
    ANDROID_CPU_ACCELERATOR_MAX,
};

// The list of CPU emulation acceleration technologies supported by the
// Android emulator.
//  CPU_ACCELERATOR_NONE means no acceleration is supported on this machine.
//
//  CPU_ACCELERATOR_KVM means Linux KVM, which requires a specific driver
//  to be installed and that /dev/kvm is properly accessible by the current
//  user.
//
//  CPU_ACCELERATOR_HAX means Intel's Hardware Accelerated eXecution,
//  which can be installed on Windows and OS X machines running on an
//  Intel processor.
//
//  CPU_ACCELERATOR_HVF means Apple's Hypervisor.framework, which
//  requires an Intel Mac running OS X 10.10+.
//
//  CPU_ACCELERATOR_WHPX means Windows Hypervisor Platform.
//
enum CpuAccelerator {
    CPU_ACCELERATOR_NONE = 0,
    CPU_ACCELERATOR_KVM,
    CPU_ACCELERATOR_HAX,
    CPU_ACCELERATOR_HVF,
    CPU_ACCELERATOR_WHPX,
    CPU_ACCELERATOR_AEHD,
    CPU_ACCELERATOR_MAX,
};

// Returns whether or not the CPU supports all modern x86
// virtualization features, so that we don't have to
// use SMP = 1.
bool hasModernX86VirtualizationFeatures();

// Return the CPU accelerator technology usable on the current machine.
// This only returns a non-CPU_ACCELERATOR_NONE if corresponding accelerator
// can be used properly. Otherwise it will return CPU_ACCELERATOR_NONE.
CpuAccelerator GetCurrentCpuAccelerator();
void ResetCurrentCpuAccelerator(CpuAccelerator accel);

// Returns whether or not the accelerator |type| is suppored
// on the current system.
bool GetCurrentAcceleratorSupport(CpuAccelerator type);

// Return an ASCII string describing the state of the current CPU
// acceleration on this machine. If GetCurrentCpuAccelerator() returns
// CPU_ACCELERATOR_NONE this will contain a small explanation why
// the accelerator cannot be used.
std::string GetCurrentCpuAcceleratorStatus();

// Return the version for the CPU accelerator technology usable
// on the current machine.
android::base::Version GetCurrentCpuAcceleratorVersion();

// Convert CpuAccelerator to string type
std::string CpuAcceleratorToString(CpuAccelerator type);

// Return an status code describing the state of the current CPU
// acceleration on this machine. If GetCurrentCpuAccelerator() returns
// CPU_ACCELERATOR_NONE this will contain a small explanation why
// the accelerator cannot be used.
AndroidCpuAcceleration GetCurrentCpuAcceleratorStatusCode();

// For unit testing/debugging purpose only, must be called before
// GetCurrentCpuAccelerator().
void SetCurrentCpuAcceleratorForTesting(CpuAccelerator accel, AndroidCpuAcceleration status_code,
                                        const char* status);

// Returns the Hyper-V configuration of the current system
// and a short message describing it.
std::pair<AndroidHyperVStatus, std::string> GetHyperVStatus();

// Returns a set of AndroidCpuInfoFlags describing the CPU capabilities
// (and a text explanation as well)
std::pair<AndroidCpuInfoFlags, std::string> GetCpuInfo();

// For testing
base::Version parseMacOSVersionString(const std::string& str, std::string* status);

#ifdef __x86_64__
struct X86Cpuid {
    uint32_t cpuid_stepping;
    uint32_t cpuid_model;
    uint32_t cpuid_family;
    uint32_t cpuid_type;
    uint32_t cpuid_extmodel;
    uint32_t cpuid_extfamily;
};

X86Cpuid GetX86Cpuid();
#endif

}  // namespace android
