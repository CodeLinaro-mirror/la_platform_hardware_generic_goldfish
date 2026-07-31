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

import java.nio.ByteBuffer
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Represents a decoded WebRTC video frame with direct native memory buffer access.
 *
 * Implements [AutoCloseable]. The consumer MUST invoke [close] when finished processing
 * to return the native buffer to the [FrameBufferPool].
 *
 * @property width Frame width in pixels.
 * @property height Frame height in pixels.
 * @property format Color pixel format ([PixelFormat]).
 * @property timestampNs Frame capture/presentation timestamp in nanoseconds.
 * @property buffer Direct native [ByteBuffer] containing raw pixel bytes.
 * @property releaseCallback Internal native release callback trigger.
 */
class VideoFrame(
    val width: Int,
    val height: Int,
    val format: PixelFormat,
    val timestampNs: Long,
    val buffer: ByteBuffer?,
    private val releaseCallback: Runnable?
) : AutoCloseable {

    private val isClosed = AtomicBoolean(false)

    /**
     * Reclaims native buffer memory. Idempotent and thread-safe.
     */
    override fun close() {
        if (isClosed.compareAndSet(false, true)) {
            releaseCallback?.run()
        }
    }

    companion object {
        /**
         * Internal factory helper to create a native buffer release Runnable for JNI.
         */
        @JvmStatic
        fun createReleaseCallback(bufferId: Long, poolPtr: Long): Runnable {
            return Runnable { nativeReleaseFrameBuffer(bufferId, poolPtr) }
        }

        @JvmStatic
        private external fun nativeReleaseFrameBuffer(bufferId: Long, poolPtr: Long)
    }
}
