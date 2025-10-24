/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.android.tools.testlib.emu

import java.io.File
import java.io.FileNotFoundException
import java.nio.file.Path
import java.nio.file.Paths
import kotlin.collections.ArrayList
import kotlin.io.path.forEachDirectoryEntry

/**
 * Represents a running emulator's discovery and config.ini files.
 *
 * @property discoveryPath path to the discovery file to parse.
 */
class Discovery(val discoveryPath: Path) {
    val discoveryIni = parseIni(discoveryPath)
    val configIni = parseIni(Paths.get(discoveryIni["avd.dir"] ?: "").resolve("config.ini"))
}

/**
 * Returns the currently running emulators.
 * @return the list of currently running emulators.
 */
fun runningEmulators(env: Map<String, String> = System.getenv()): List<Discovery> {
    return buildList {
        discoveryDirectories(env).forEach {
            it.forEachDirectoryEntry(glob = "pid_*.ini") {
                entry -> add(Discovery(entry))
            }
        }
    }
}

/**
 * Finds a running emulator by the serial number.
 * @return the running emulator or null if not found.
 */
fun findEmulator(serial: String, env: Map<String, String> = System.getenv()): Discovery? {
    runningEmulators(env).forEach {
        if (serial.substring(9, serial.length).equals(it.discoveryIni["port.serial"] ?: "")) {
            return it
        }
    }
    return null
}

private fun discoveryDirectories(env: Map<String, String>): List<Path> {
    var dirs = ArrayList<Path>()
    val osName = System.getProperty("os.name")
    when {
        osName.contains("Windows", ignoreCase = true) -> {
            val localAppData = env["LOCALAPPDATA"]
            if (localAppData != null) {
                dirs.add(Paths.get(localAppData, "Temp"))
            }
        }
        osName.contains("Linux", ignoreCase = true) -> {
            // TODO(kmagic): Figure out how to get uid for backup runtime dir.
            val runtimeDir = env["XDG_RUNTIME_DIR"]
            if (runtimeDir != null) {
                dirs.add(Paths.get(runtimeDir))
            }
        }
        osName.contains("Mac", ignoreCase = true) -> {
            val home = env["HOME"]
            if (home != null) {
                dirs.add(Paths.get(home, "Library", "Caches", "TemporaryItems"))
            }
        }
    }
    for (e in listOf("ANDROID_EMULATOR_HOME", "ANDROID_AVD_HOME")) {
        val v = env[e]
        if (v != null) {
            dirs.add(Paths.get(v))
        }
    }
    val sdkHome = env["ANDROID_SDK_HOME"]
    if (sdkHome != null) {
        dirs.add(Paths.get(sdkHome, ".android"))
    }
    return buildList {
        dirs.forEach {
            add(it.resolve("avd").resolve("running"))
        }

    }
}

private fun parseIni(path: Path): Map<String, String> {
    return buildMap {
        try {
            File(path.toString()).forEachLine {
                if (!it.trim().startsWith(";")) {
                    val parts = it.split("=", limit = 2)
                    if (parts.size == 2) {
                        put(parts[0].trim(), parts[1].trim())
                    }
                }
            }
        } catch (e: FileNotFoundException) {
           // Ignore a lack of file, the tests will fail if the data is the map is empty.
        }
    }
}
