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
import com.android.emulator.webrtc.LoggingSeverity
import com.android.emulator.webrtc.MouseButton
import com.android.emulator.webrtc.PixelFormat
import com.android.emulator.webrtc.TouchAction
import com.android.emulator.webrtc.WebRtcClient
import com.android.emulator.webrtc.signaling.VideoBridgeSignalingClient
import java.awt.image.BufferedImage
import java.awt.image.DataBufferInt
import java.util.concurrent.CopyOnWriteArrayList
import java.util.logging.Logger

/**
 * UI-Agnostic higher-level WebRTC stream session controller.
 *
 * Orchestrates [WebRtcClient] JNI receiver, [VideoBridgeSignalingClient] gRPC connection,
 * image buffer decoding, and touch/mouse event injection.
 *
 * Can be consumed by Swing, Compose Desktop, CLI, or any Java/Kotlin UI component.
 *
 * @param host Video Bridge server hostname or IP address (default: "localhost").
 * @param port Video Bridge gRPC port (default: 8554).
 * @param format Requested pixel format for video frame decoding (default: RGBA8888).
 * @param signalingClient Optional custom [VideoBridgeSignalingClient] instance for testing.
 * @param client Optional custom [WebRtcClient] instance for testing.
 */
class EmulatorStreamSession(
    val host: String = "localhost",
    val port: Int = 8554,
    val format: PixelFormat = PixelFormat.RGBA8888,
    signalingClient: VideoBridgeSignalingClient? = null,
    client: WebRtcClient? = null
) : AutoCloseable {

    private val logger = Logger.getLogger(EmulatorStreamSession::class.java.name)
    private val signaling = signalingClient ?: VideoBridgeSignalingClient(host, port)
    private val webRtcClient = client ?: WebRtcClient.create(format)

    private val frameListeners = CopyOnWriteArrayList<(BufferedImage) -> Unit>()
    private val stateListeners = CopyOnWriteArrayList<(ConnectionState) -> Unit>()
    private var isClosed = false

    /** Active WebRTC connection state. */
    var currentState: ConnectionState = ConnectionState.NEW
        private set

    init {
        // Silence native debug logs; forward warnings and errors to Java Logger
        WebRtcClient.setLoggingListener(LoggingSeverity.WARNING) { severity, msg ->
            logger.warning("[WebRTC Native $severity] $msg")
        }

        // Configure WebRTC client callbacks
        webRtcClient.setJsepMessageListener { jsepJson ->
            signaling.sendJsepMessage(jsepJson)
        }

        webRtcClient.setConnectionStateListener { newState ->
            currentState = newState
            logger.info("WebRTC Connection State changed to: $newState")
            stateListeners.forEach { it(newState) }
        }

        webRtcClient.setVideoFrameListener { videoFrame ->
            videoFrame.use { frame ->
                val width = frame.width
                val height = frame.height
                val buffer = frame.buffer ?: return@setVideoFrameListener

                val img = BufferedImage(width, height, BufferedImage.TYPE_INT_ARGB)
                val intData = (img.raster.dataBuffer as DataBufferInt).data
                val intBuffer = buffer.asIntBuffer()
                val totalPixels = width * height

                for (i in 0 until totalPixels) {
                    val rgba = intBuffer.get(i)
                    val r = rgba and 0xFF
                    val g = (rgba shr 8) and 0xFF
                    val b = (rgba shr 16) and 0xFF
                    val a = (rgba shr 24) and 0xFF
                    intData[i] = (a shl 24) or (r shl 16) or (g shl 8) or b
                }

                frameListeners.forEach { listener ->
                    listener(img)
                }
            }
        }
    }

    /**
     * Registers a listener to receive decoded [BufferedImage] video frames.
     */
    fun addFrameListener(listener: (BufferedImage) -> Unit) {
        frameListeners.add(listener)
    }

    /**
     * Removes a frame listener.
     */
    fun removeFrameListener(listener: (BufferedImage) -> Unit) {
        frameListeners.remove(listener)
    }

    /**
     * Registers a listener to receive WebRTC connection state updates.
     */
    fun addStateListener(listener: (ConnectionState) -> Unit) {
        stateListeners.add(listener)
    }

    /**
     * Starts the WebRTC stream session by connecting gRPC signaling and creating SDP offer.
     */
    fun start() {
        if (isClosed) return
        logger.info("Starting EmulatorStreamSession to $host:$port...")
        signaling.requestRtcStream()
        signaling.subscribeJsepStream(
            listener = { jsepJson -> webRtcClient.acceptJsepMessage(jsepJson) },
            onError = { logger.severe("Stream session error: ${it.message}") }
        )
        webRtcClient.createOffer()
    }

    /**
     * Sends a mouse event (coordinates + button + action) to the emulator display.
     *
     * @param x Target X pixel coordinate.
     * @param y Target Y pixel coordinate.
     * @param button Mouse button state ([MouseButton.LEFT], [MouseButton.NONE], etc.).
     * @param action Touch/Mouse action enum ordinal ([TouchAction.DOWN], [TouchAction.MOVE], [TouchAction.UP]).
     */
    fun sendMouseEvent(x: Int, y: Int, button: MouseButton, action: Int) {
        if (!isClosed) {
            webRtcClient.sendMouseEvent(x, y, button, action)
        }
    }

    /**
     * Sends a keyboard key event to the emulator display.
     *
     * @param key W3C `KeyboardEvent.key` string representation (e.g. "a", "Enter", "GoHome", "GoBack").
     * @param down True for key down, false for key up.
     */
    fun sendKeyEvent(key: String, down: Boolean) {
        if (!isClosed) {
            webRtcClient.sendKeyEvent(key, down)
        }
    }

    /**
     * Closes the stream session, disposing of WebRTC JNI resources and gRPC signaling.
     */
    @Synchronized
    override fun close() {
        if (!isClosed) {
            isClosed = true
            logger.info("Closing EmulatorStreamSession...")
            frameListeners.clear()
            stateListeners.clear()
            runCatching { webRtcClient.close() }
            runCatching { signaling.close() }
        }
    }
}
