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

/**
 * Client for receiving Android Emulator WebRTC video streams and sending user input events.
 *
 * Wraps native C++ JNI bindings to WebRTC (`JniRtcReceiver`), managing session description exchange
 * (JSEP SDP/ICE), decoding video frames, and sending touch/mouse/keyboard events over WebRTC data channels.
 *
 * Implements [AutoCloseable] to release native C++ resources cleanly.
 */
class WebRtcClient private constructor(private var nativeHandle: Long) : AutoCloseable {

    init {
        NativeLibraryLoader.loadNativeLibrary()
    }

    /**
     * Registers a callback listener for receiving decoded video frames.
     */
    fun setVideoFrameListener(listener: VideoFrameListener?) {
        checkNativeHandle()
        nativeSetVideoFrameListener(nativeHandle, listener)
    }

    /**
     * Registers a callback listener for outbound JSEP signaling messages (SDP offer/candidates).
     */
    fun setJsepMessageListener(listener: JsepMessageListener?) {
        checkNativeHandle()
        nativeSetJsepMessageListener(nativeHandle, listener)
    }

    /**
     * Registers a callback listener for WebRTC peer connection state changes.
     */
    fun setConnectionStateListener(listener: ConnectionStateListener?) {
        checkNativeHandle()
        nativeSetConnectionStateListener(nativeHandle, listener)
    }

    /**
     * Initiates creation of a local WebRTC SDP offer.
     */
    fun createOffer() {
        checkNativeHandle()
        nativeCreateOffer(nativeHandle)
    }

    /**
     * Passes an inbound JSEP JSON payload (SDP answer or ICE candidate) received from Video Bridge into WebRTC.
     */
    fun acceptJsepMessage(jsepJson: String) {
        checkNativeHandle()
        nativeAcceptJsepMessage(nativeHandle, jsepJson)
    }

    /**
     * Sends a multi-touch input event over the WebRTC input data channel.
     */
    fun sendTouchEvent(id: Int, x: Int, y: Int, action: TouchAction) {
        checkNativeHandle()
        nativeSendTouchEvent(nativeHandle, id, x, y, action.ordinal)
    }

    /**
     * Sends a single-touch input event (id = 0) over the WebRTC input data channel.
     */
    fun sendTouchEvent(x: Int, y: Int, action: TouchAction) {
        sendTouchEvent(0, x, y, action)
    }

    /**
     * Sends a mouse input event over the WebRTC input data channel.
     */
    fun sendMouseEvent(x: Int, y: Int, button: MouseButton, action: Int) {
        checkNativeHandle()
        nativeSendMouseEvent(nativeHandle, x, y, button.ordinal, action)
    }

    /**
     * Sends a keyboard input event over the WebRTC input data channel.
     */
    fun sendKeyEvent(key: String, down: Boolean) {
        checkNativeHandle()
        nativeSendKeyEvent(nativeHandle, key, down)
    }

    /**
     * Closes the WebRTC client connection and releases native C++ resources.
     */
    @Synchronized
    override fun close() {
        if (nativeHandle != 0L) {
            nativeDestroy(nativeHandle)
            nativeHandle = 0L
        }
    }

    private fun checkNativeHandle() {
        check(nativeHandle != 0L) { "WebRtcClient has already been closed" }
    }

    companion object {
        init {
            NativeLibraryLoader.loadNativeLibrary()
        }

        /**
         * Factory method to create a new [WebRtcClient] with the specified target pixel format.
         */
        @JvmStatic
        fun create(targetFormat: PixelFormat = PixelFormat.RGBA8888): WebRtcClient {
            val handle = nativeCreate(targetFormat.ordinal)
            check(handle != 0L) { "Failed to initialize native WebRTC client" }
            return WebRtcClient(handle)
        }

        /**
         * Configures global native WebRTC logging and redirects log messages to a listener.
         */
        @JvmStatic
        fun setLoggingListener(minSeverity: LoggingSeverity, listener: LoggingListener?) {
            nativeSetLoggingListener(minSeverity.value, listener)
        }

        /**
         * Sets native WebRTC log severity level.
         */
        @JvmStatic
        fun setLogLevel(minSeverity: LoggingSeverity) {
            setLoggingListener(minSeverity, null)
        }

        @JvmStatic private external fun nativeCreate(pixelFormat: Int): Long
        @JvmStatic private external fun nativeSetVideoFrameListener(handle: Long, listener: VideoFrameListener?)
        @JvmStatic private external fun nativeSetJsepMessageListener(handle: Long, listener: JsepMessageListener?)
        @JvmStatic private external fun nativeSetConnectionStateListener(handle: Long, listener: ConnectionStateListener?)
        @JvmStatic private external fun nativeCreateOffer(handle: Long)
        @JvmStatic private external fun nativeAcceptJsepMessage(handle: Long, jsepJson: String)
        @JvmStatic private external fun nativeSendTouchEvent(handle: Long, id: Int, x: Int, y: Int, actionEnum: Int)
        @JvmStatic private external fun nativeSendMouseEvent(handle: Long, x: Int, y: Int, buttonEnum: Int, actionEnum: Int)
        @JvmStatic private external fun nativeSendKeyEvent(handle: Long, key: String, down: Boolean)
        @JvmStatic private external fun nativeSetLoggingListener(minSeverity: Int, listener: LoggingListener?)
        @JvmStatic private external fun nativeDestroy(handle: Long)
    }
}
