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

import com.android.emulator.webrtc.MouseButton
import com.android.emulator.webrtc.TouchAction
import com.android.emulator.webrtc.session.EmulatorStreamSession
import java.awt.Color
import java.awt.Font
import java.awt.Graphics
import java.awt.Graphics2D
import java.awt.RenderingHints
import java.awt.event.KeyAdapter
import java.awt.event.KeyEvent
import java.awt.event.MouseAdapter
import java.awt.event.MouseEvent
import java.awt.image.BufferedImage
import java.util.logging.Logger
import javax.swing.JPanel

/**
 * Custom Swing Panel that renders WebRTC video frames and handles touch/mouse/keyboard interactions.
 *
 * Automatically scales panel mouse coordinates to target emulator screen resolution and dispatches
 * input events to [EmulatorStreamSession].
 */
class EmulatorVideoPanel(private val session: EmulatorStreamSession) : JPanel() {

    private val logger = Logger.getLogger(EmulatorVideoPanel::class.java.name)
    private var currentImage: BufferedImage? = null
    private var frameCount: Long = 0
    private var statusText: String = "Connecting..."

    init {
        background = Color(30, 30, 30)
        isFocusable = true

        val mouseHandler = object : MouseAdapter() {
            override fun mousePressed(e: MouseEvent) {
                requestFocusInWindow()
                dispatchTouchEvent(e.x, e.y, TouchAction.DOWN)
            }

            override fun mouseDragged(e: MouseEvent) {
                dispatchTouchEvent(e.x, e.y, TouchAction.MOVE)
            }

            override fun mouseReleased(e: MouseEvent) {
                dispatchTouchEvent(e.x, e.y, TouchAction.UP)
            }
        }

        addMouseListener(mouseHandler)
        addMouseMotionListener(mouseHandler)

        val keyHandler = object : KeyAdapter() {
            override fun keyPressed(e: KeyEvent) {
                val w3cKey = mapAwtKeyToW3cKey(e)
                logger.info("Key DOWN: ${e.keyCode} -> '$w3cKey'")
                session.sendKeyEvent(w3cKey, true)
            }

            override fun keyReleased(e: KeyEvent) {
                val w3cKey = mapAwtKeyToW3cKey(e)
                logger.info("Key UP: ${e.keyCode} -> '$w3cKey'")
                session.sendKeyEvent(w3cKey, false)
            }
        }

        addKeyListener(keyHandler)
    }

    private fun mapAwtKeyToW3cKey(e: KeyEvent): String {
        return when (e.keyCode) {
            KeyEvent.VK_ENTER -> "Enter"
            KeyEvent.VK_BACK_SPACE -> "Backspace"
            KeyEvent.VK_TAB -> "Tab"
            KeyEvent.VK_ESCAPE -> "GoBack"
            KeyEvent.VK_UP -> "ArrowUp"
            KeyEvent.VK_DOWN -> "ArrowDown"
            KeyEvent.VK_LEFT -> "ArrowLeft"
            KeyEvent.VK_RIGHT -> "ArrowRight"
            KeyEvent.VK_HOME -> "GoHome"
            KeyEvent.VK_END -> "End"
            KeyEvent.VK_PAGE_UP -> "PageUp"
            KeyEvent.VK_PAGE_DOWN -> "PageDown"
            KeyEvent.VK_DELETE -> "Delete"
            else -> {
                val ch = e.keyChar
                if (ch != KeyEvent.CHAR_UNDEFINED && ch.code in 32..126) {
                    ch.toString()
                } else {
                    KeyEvent.getKeyText(e.keyCode)
                }
            }
        }
    }

    /**
     * Updates the current video frame displayed on the panel.
     */
    fun updateFrame(img: BufferedImage) {
        this.currentImage = img
        this.frameCount++
        repaint()
    }

    /**
     * Updates the active connection state status message.
     */
    fun setConnectionState(state: String) {
        this.statusText = state
        repaint()
    }

    private fun dispatchTouchEvent(mouseX: Int, mouseY: Int, action: TouchAction) {
        val img = currentImage ?: return
        val panelW = width.toDouble()
        val panelH = height.toDouble()
        val imgW = img.width.toDouble()
        val imgH = img.height.toDouble()

        val scale = minOf(panelW / imgW, panelH / imgH)
        val drawW = imgW * scale
        val drawH = imgH * scale
        val offsetX = (panelW - drawW) / 2.0
        val offsetY = (panelH - drawH) / 2.0

        val relX = mouseX - offsetX
        val relY = mouseY - offsetY

        if (relX >= 0 && relX < drawW && relY >= 0 && relY < drawH) {
            val emuX = ((relX / drawW) * imgW).toInt().coerceIn(0, img.width - 1)
            val emuY = ((relY / drawH) * imgH).toInt().coerceIn(0, img.height - 1)

            val mouseButton = if (action == TouchAction.UP || action == TouchAction.CANCEL) {
                MouseButton.NONE
            } else {
                MouseButton.LEFT
            }

            logger.info("Mouse $action at ($emuX, $emuY) [button=$mouseButton]")
            session.sendMouseEvent(emuX, emuY, mouseButton, action.ordinal)
        }
    }

    override fun paintComponent(g: Graphics) {
        super.paintComponent(g)
        val g2 = g as Graphics2D
        g2.setRenderingHint(RenderingHints.KEY_INTERPOLATION, RenderingHints.VALUE_INTERPOLATION_BILINEAR)

        val img = currentImage
        if (img != null) {
            val panelW = width.toDouble()
            val panelH = height.toDouble()
            val imgW = img.width.toDouble()
            val imgH = img.height.toDouble()

            val scale = minOf(panelW / imgW, panelH / imgH)
            val drawW = (imgW * scale).toInt()
            val drawH = (imgH * scale).toInt()
            val offsetX = ((panelW - drawW) / 2.0).toInt()
            val offsetY = ((panelH - drawH) / 2.0).toInt()

            g2.drawImage(img, offsetX, offsetY, drawW, drawH, null)

            // Render overlay banner
            g2.color = Color(0, 0, 0, 160)
            g2.fillRect(0, 0, width, 32)

            g2.color = Color.WHITE
            g2.font = Font("SansSerif", Font.BOLD, 12)
            val infoStr = "Resolution: ${img.width}x${img.height}  |  Frames: $frameCount  |  State: $statusText"
            g2.drawString(infoStr, 12, 20)
        } else {
            g2.color = Color.LIGHT_GRAY
            g2.font = Font("SansSerif", Font.PLAIN, 14)
            val msg = "Connecting to Android Emulator WebRTC Stream ($statusText)..."
            val stringWidth = g2.fontMetrics.stringWidth(msg)
            g2.drawString(msg, (width - stringWidth) / 2, height / 2)
        }
    }
}
