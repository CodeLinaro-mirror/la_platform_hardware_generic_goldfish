/* Copyright (C) 2012 The Android Open Source Project
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/

#pragma once

/* Keys of the properties found in avd/name.ini and config.ini files.
 *
 * These keys must match their counterpart defined in
 * sdk/sdkmanager/libs/sdklib/src/com/android/sdklib/internal/avd/AvdManager.java
 */

/* -- Keys used in avd/name.ini -- */

/* Absolute path of the AVD content directory.
 */
constexpr char kRootAbsPathKey[] = "path";

/* Relative path of the AVD content directory.
 * Path is relative to the bufprint_config_path().
 */
constexpr char kRootRelPathKey[] = "path.rel";

/* -- Keys used in config.ini -- */

/* AVD/config.ini key name representing the abi type of the specific avd
 */
constexpr char kAbiType[] = "abi.type";

/* AVD/config.ini key name representing the CPU architecture of the specific avd
 */
constexpr char kCpuArch[] = "hw.cpu.arch";
/* the prefix of config.ini keys that will be used for search directories
 * of system images.
 */
constexpr char kSearchPrefix[] = "image.sysdir.";

/* the maximum number of search path keys we're going to read from the
 * config.ini file
 */
constexpr int kMaxSearchPaths = 2;

/* the config.ini key that will be used to indicate the full relative
 * path to the skin directory (including the skin name).
 */
constexpr char kSkinPath[] = "skin.path";

/* the config.ini key that will be used to indicate the default skin's name.
 * this is ignored if there is a valid SKIN_PATH entry in the file.
 */
constexpr char kSkinName[] = "skin.name";

/*
 * The pixel_fold default skin and closed skin name, they are inside
 * skins/pixel_fold/
 */
constexpr char kPixelFoldDefaultSkinName[] = "default";
constexpr char kPixelFoldClosedSkinName[] = "closed";

/* default skin name */
constexpr char kSkinDefault[] = "HVGA";

/* the config.ini key that is used to indicate the absolute path
 * to the SD Card image file, if you don't want to place it in
 * the content directory.
 */
constexpr char kSdcardPath[] = "sdcard.path";

/* The config.ini key name representing the second path where the emulator looks
 * for system images. Typically this is the path to the platform system image.
 */
constexpr char kImages2[] = "image.sysdir.2";

/* AVD/config.ini key name representing the presence of the snapshots file.
 */
constexpr char kSnapshotPresent[] = "snapshot.present";

/* AVD/config.ini key name representing the size of the SD card.
 */
constexpr char kSdcardSize[] = "sdcard.size";

/* AVD/config.ini key name representing the tag id of the specific avd
 */
constexpr char kTagId[] = "tag.id";

/* AVD/config.ini value for tag id of Chrome OS.
 */
constexpr char kTagIdChromeos[] = "chromeos";

/* AVD/config.ini key name representing the tag display of the specific avd
 */
constexpr char kTagDisplay[] = "tag.display";
