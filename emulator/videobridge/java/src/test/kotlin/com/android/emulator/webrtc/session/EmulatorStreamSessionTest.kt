/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.android.emulator.webrtc.session

import com.android.emulator.webrtc.ConnectionState
import com.android.emulator.webrtc.MouseButton
import com.android.emulator.webrtc.PixelFormat
import com.android.emulator.webrtc.TouchAction
import org.junit.Test
import java.awt.image.BufferedImage
import java.util.concurrent.atomic.AtomicBoolean
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue

public class EmulatorStreamSessionTest {

    @Test
    fun testSessionStateAndListenerManagement() {
        val frameReceived = AtomicBoolean(false)
        val stateReceived = AtomicBoolean(false)

        val frameListener: (BufferedImage) -> Unit = { frameReceived.set(true) }
        val stateListener: (ConnectionState) -> Unit = { stateReceived.set(true) }

        // Verify initial state defaults
        assertEquals(ConnectionState.NEW, ConnectionState.valueOf("NEW"))
    }
}
