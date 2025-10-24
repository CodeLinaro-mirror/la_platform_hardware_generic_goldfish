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

import java.nio.file.Path
import java.nio.file.Paths
import java.util.HashMap
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class DiscoveryTest {
  @Rule
  @JvmField
  val tempFolder = TemporaryFolder()

  @Test
  fun testConstructor() {
    val pidFile = tempFolder.newFile("pid_1234.ini")
    val tempPath = tempFolder.getRoot().toString()
    pidFile.writeText("foo=bar\n; comment\navd.dir=$tempPath\n")
    tempFolder.newFile("config.ini").writeText("cat=meow\n")
    val emu = Discovery(Paths.get(pidFile.toString()))

    assertEquals(mapOf("foo" to "bar", "avd.dir" to "$tempPath"), emu.discoveryIni)
    assertEquals(mapOf("cat" to "meow"), emu.configIni)
  }

  @Test
  fun testFindEmulator() {
    createEmulatorIn(Paths.get(tempFolder.getRoot().toString()), "5678")
    val env = mapOf("ANDROID_EMULATOR_HOME" to tempFolder.getRoot().toString())
    val emu = findEmulator("emulator-5678", env)
    assertNotNull(emu)
    if (emu != null) {
      assertEquals(mapOf("port.serial" to "5678"), emu.discoveryIni)
    }
  }

  @Test
  fun testRunningEmulators() {
    var serial = 5000
    val env = HashMap<String, String>()
    val want = HashSet<String>()
    want.add("$serial")

    // Create the OS specific files at the same serial since only one will show up.
    for (e in listOf(EnvDir("LOCALAPPDATA", arrayOf("Temp")), EnvDir("XDG_RUNTIME_DIR", arrayOf()),
                     EnvDir("HOME", arrayOf("Library", "Caches", "TemporaryItems")))) {
      val baseDir = tempFolder.newFolder(e.name, *e.subPath)
      createEmulatorIn(Paths.get(baseDir.toString()), "$serial")
      env[e.name] = Paths.get(tempFolder.getRoot().toString(), e.name).toString()
    }
    // Now increase the serial every emulator since these will all show up.
    for (e in listOf(EnvDir("ANDROID_EMULATOR_HOME", arrayOf()), EnvDir("ANDROID_AVD_HOME", arrayOf()),
                     EnvDir("ANDROID_SDK_HOME", arrayOf(".android")))) {
      serial++
      want.add("$serial")
      val baseDir = tempFolder.newFolder(e.name, *e.subPath)
      createEmulatorIn(Paths.get(baseDir.toString()), "$serial")
      env[e.name] = Paths.get(tempFolder.getRoot().toString(), e.name).toString()
    }

    val emus = runningEmulators(env)
    assertEquals(want.size, emus.size)

    val got = buildSet {
      emus.forEach {
        add(it.discoveryIni["port.serial"])
      }
    }
    assertEquals(want, got)
  }
}

class EnvDir(val name: String, val subPath: Array<String>) {}


private fun createEmulatorIn(path: Path, portSerial: String) {
  val runDir = path.resolve("avd").resolve("running")
  runDir.toFile().mkdirs()
  val pidFile = runDir.resolve("pid_1234.ini").toFile()
  pidFile.writeText("port.serial=$portSerial\n")
}
