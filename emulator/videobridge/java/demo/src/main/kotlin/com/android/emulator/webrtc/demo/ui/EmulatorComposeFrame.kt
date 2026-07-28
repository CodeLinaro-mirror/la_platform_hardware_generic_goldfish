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
import java.awt.Color
import java.awt.Dimension
import java.awt.Font
import java.awt.FlowLayout
import java.awt.event.WindowAdapter
import java.awt.event.WindowEvent
import java.awt.image.BufferedImage
import java.util.logging.Logger
import javax.swing.JButton
import javax.swing.JFrame
import javax.swing.JLabel
import javax.swing.JPanel
import javax.swing.SwingConstants
import javax.swing.border.EmptyBorder

/**
 * Modern Compose-styled Window Frame for the WebRTC Emulator Demo.
 *
 * Integrates WebRTC video stream rendering with a modern dark-themed control bar
 * and status indicators.
 */
class EmulatorComposeFrame(
    private val session: EmulatorStreamSession,
    onClosing: () -> Unit
) : JFrame("Android Emulator - WebRTC Desktop Demo") {

    private val logger = Logger.getLogger(EmulatorComposeFrame::class.java.name)
    val videoPanel = EmulatorVideoPanel(session)

    private val statusLabel = JLabel("CONNECTING", SwingConstants.CENTER)
    private val statsLabel = JLabel("Resolution: --  |  Frames: 0", SwingConstants.LEFT)

    init {
        defaultCloseOperation = DISPOSE_ON_CLOSE
        layout = BorderLayout()
        preferredSize = Dimension(480, 960)
        contentPane.background = Color(18, 18, 18)

        // Top Modern Header Bar
        val headerPanel = JPanel(BorderLayout()).apply {
            background = Color(24, 24, 24)
            border = EmptyBorder(10, 16, 10, 16)

            statsLabel.foreground = Color(220, 220, 220)
            statsLabel.font = Font("SansSerif", Font.BOLD, 13)
            add(statsLabel, BorderLayout.WEST)

            statusLabel.isOpaque = true
            statusLabel.background = Color(198, 40, 40)
            statusLabel.foreground = Color.WHITE
            statusLabel.font = Font("SansSerif", Font.BOLD, 11)
            statusLabel.border = EmptyBorder(4, 8, 4, 8)
            add(statusLabel, BorderLayout.EAST)
        }
        add(headerPanel, BorderLayout.NORTH)

        // Center Video Viewport
        add(videoPanel, BorderLayout.CENTER)

        // Bottom Navigation Bar
        val navBar = JPanel(FlowLayout(FlowLayout.CENTER, 24, 8)).apply {
            background = Color(24, 24, 24)

            val createNavButton = { text: String, onClick: () -> Unit ->
                JButton(text).apply {
                    background = Color(38, 38, 38)
                    foreground = Color.WHITE
                    isFocusPainted = false
                    font = Font("SansSerif", Font.PLAIN, 14)
                    addActionListener { onClick() }
                }
            }

            add(createNavButton("◀ Back") {
                logger.info("Navigation: Back pressed")
                session.sendKeyEvent("GoBack", true)
                session.sendKeyEvent("GoBack", false)
            })
            add(createNavButton("● Home") {
                logger.info("Navigation: Home pressed")
                session.sendKeyEvent("GoHome", true)
                session.sendKeyEvent("GoHome", false)
            })
            add(createNavButton("■ Recents") {
                logger.info("Navigation: Recents pressed")
                session.sendKeyEvent("AppSwitch", true)
                session.sendKeyEvent("AppSwitch", false)
            })
        }
        add(navBar, BorderLayout.SOUTH)

        addWindowListener(object : WindowAdapter() {
            override fun windowClosing(e: WindowEvent?) {
                logger.info("GUI Window closing. Shutting down demo...")
                onClosing()
            }
        })

        pack()
        setLocationRelativeTo(null)
    }

    fun updateStatus(state: String) {
        statusLabel.text = state.uppercase()
        if (state.equals("CONNECTED", ignoreCase = true)) {
            statusLabel.background = Color(46, 125, 50)
        } else {
            statusLabel.background = Color(198, 40, 40)
        }
        videoPanel.setConnectionState(state)
    }

    fun updateFrame(img: BufferedImage, frameCount: Long) {
        statsLabel.text = "${img.width}x${img.height}  |  Frames: $frameCount"
        videoPanel.updateFrame(img)
    }
}
