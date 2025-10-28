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
package com.android.tools.e2etests

import com.android.tools.testlib.emu.Discovery
import com.android.tools.testlib.emu.findEmulator
import com.android.tradefed.log.LogUtil.CLog
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test
import org.junit.runner.RunWith
import org.junit.Assert
import org.junit.Test

@RunWith(DeviceJUnit4ClassRunner::class)
class DisplayNameTest : BaseHostJUnit4Test() {

    @Test
    fun testCheckDiscoveryNameMatchesConfigIni() {
        Assert.assertNotNull(getDevice())
        System.out.println(getDevice().getSerialNumber())
        val discovery = findEmulator(getDevice().getSerialNumber())
        Assert.assertNotNull(discovery)
        if (discovery != null) {
            Assert.assertNotNull(discovery.discoveryIni["avd.name"])
            Assert.assertEquals(discovery.discoveryIni["avd.name"], discovery.configIni["avd.ini.displayname"])
        }
    }

    @Test
    fun testCheckDisplayNameContainsNonAscii() {
        Assert.assertNotNull(getDevice())
        System.out.println(getDevice().getSerialNumber())
        val discovery = findEmulator(getDevice().getSerialNumber())
        Assert.assertNotNull(discovery)
        if (discovery != null) {
            Assert.assertNotNull(discovery.discoveryIni["avd.name"])
            Assert.assertTrue(containsNonAscii(discovery.discoveryIni["avd.name"] ?: ""))
        }
    }
}

fun containsNonAscii(s: String): Boolean {
    for (char in s) {
        if (char.code > 127) {
            return true
        }
    }
    return false
}
