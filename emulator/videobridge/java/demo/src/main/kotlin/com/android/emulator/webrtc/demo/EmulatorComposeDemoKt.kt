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
package com.android.emulator.webrtc.demo

import com.android.emulator.webrtc.demo.ui.EmulatorComposeFrame
import com.android.emulator.webrtc.session.EmulatorStreamSession
import java.awt.GraphicsEnvironment
import java.util.concurrent.CountDownLatch
import java.util.logging.Logger
import javax.swing.SwingUtilities

/**
 * Interactive Desktop GUI demo application connecting to Video Bridge using [EmulatorStreamSession].
 */
object EmulatorComposeDemoKt {
    private val logger = Logger.getLogger(EmulatorComposeDemoKt::class.java.name)

    @JvmStatic
    fun main(args: Array<String>) {
        // Pre-initialize Protobuf internal classes to prevent static initializer loop
        runCatching {
            Class.forName("com.google.protobuf.CodedInputStream")
            Class.forName("com.google.protobuf.Internal")
        }

        val host = args.getOrNull(0) ?: "localhost"
        val port = args.getOrNull(1)?.toIntOrNull() ?: 8554

        val shutdownLatch = CountDownLatch(1)
        val isHeadless = GraphicsEnvironment.isHeadless()

        logger.info("Starting WebRTC Desktop Demo App connecting to Video Bridge at $host:$port...")

        EmulatorStreamSession(host, port).use { session ->
            var demoFrame: EmulatorComposeFrame? = null
            var frameCount = 0L

            if (!isHeadless) {
                SwingUtilities.invokeAndWait {
                    demoFrame = EmulatorComposeFrame(session) {
                        shutdownLatch.countDown()
                    }.apply { isVisible = true }
                }
            } else {
                logger.info("Headless environment detected. Running CLI-only mode.")
            }

            // Register stream listeners
            session.addStateListener { newState ->
                demoFrame?.updateStatus(newState.toString())
            }

            session.addFrameListener { bufferedImage ->
                frameCount++
                if (!isHeadless && demoFrame != null) {
                    val count = frameCount
                    SwingUtilities.invokeLater {
                        demoFrame?.updateFrame(bufferedImage, count)
                    }
                } else {
                    logger.info("Rendered Frame: ${bufferedImage.width}x${bufferedImage.height}")
                }
            }

            // Start session
            session.start()
            logger.info("WebRTC video stream active.")

            shutdownLatch.await()

            if (demoFrame != null && demoFrame!!.isVisible) {
                SwingUtilities.invokeLater {
                    demoFrame!!.dispose()
                }
            }
        }
    }
}
