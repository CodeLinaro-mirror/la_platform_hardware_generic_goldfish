// Copyright 2026 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

/**
 * @file address_space_low_level.h
 * @brief Public QOM registration interface for the `goldfish_address_space` PCI device shim.
 *
 * Included by `emulator/plugin/plugin.cc` to register the legacy Goldfish Address Space PCI device
 * (`607D:F153`) into QEMU's object model at plugin load time.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registers the `goldfish_address_space` PCI device and BAR 1 subregion QOM types with QEMU.
 *
 * Safe to call strictly from QEMU's `GF_REGISTER_TYPES_FUNC` initialization hook before machine
 * realization.
 */
void goldfish_address_space_register_types(void);

#ifdef __cplusplus
}
#endif
