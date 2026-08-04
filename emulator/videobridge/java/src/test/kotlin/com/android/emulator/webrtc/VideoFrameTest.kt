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
package com.android.emulator.webrtc

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.nio.ByteBuffer
import java.util.concurrent.atomic.AtomicBoolean

class VideoFrameTest {

    @Test
    fun testVideoFramePropertiesAndReleaseCallback() {
        val released = AtomicBoolean(false)
        val releaseCb = Runnable { released.set(true) }

        val buf = ByteBuffer.allocateDirect(1024)
        val frame = VideoFrame(640, 480, PixelFormat.RGBA8888, 1000000L, buf, releaseCb)

        assertEquals(640, frame.width)
        assertEquals(480, frame.height)
        assertEquals(PixelFormat.RGBA8888, frame.format)
        assertEquals(1000000L, frame.timestampNs)
        assertNotNull(frame.buffer)

        assertFalse(released.get())
        frame.close()
        assertTrue(released.get())

        // Idempotent close
        frame.close()
        assertTrue(released.get())
    }

    @Test
    fun testVideoFrameNullReleaseCallback() {
        val buf = ByteBuffer.allocateDirect(1024)
        val frame = VideoFrame(320, 240, PixelFormat.BGRA8888, 5000L, buf, null)

        assertEquals(320, frame.width)
        assertEquals(240, frame.height)
        frame.close() // should not throw NPE
    }
}
