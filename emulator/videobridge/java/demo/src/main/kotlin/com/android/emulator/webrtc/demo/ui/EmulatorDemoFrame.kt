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
package com.android.emulator.webrtc.demo.ui

import com.android.emulator.webrtc.session.EmulatorStreamSession
import java.awt.BorderLayout
import java.awt.Dimension
import java.awt.event.WindowAdapter
import java.awt.event.WindowEvent
import java.util.logging.Logger
import javax.swing.JFrame

/**
 * Top-level Swing Window Frame for the WebRTC Emulator Demo.
 */
class EmulatorDemoFrame(
    session: EmulatorStreamSession,
    onClosing: () -> Unit
) : JFrame("Android Emulator - WebRTC Swing Demo") {

    private val logger = Logger.getLogger(EmulatorDemoFrame::class.java.name)
    val videoPanel = EmulatorVideoPanel(session)

    init {
        defaultCloseOperation = DISPOSE_ON_CLOSE
        layout = BorderLayout()
        preferredSize = Dimension(450, 920)

        add(videoPanel, BorderLayout.CENTER)

        addWindowListener(object : WindowAdapter() {
            override fun windowClosing(e: WindowEvent?) {
                logger.info("GUI Window closing. Shutting down demo...")
                onClosing()
            }
        })

        pack()
        setLocationRelativeTo(null)
    }
}
