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

import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.util.concurrent.atomic.AtomicBoolean
import java.util.logging.Logger

/**
 * Utility for loading native shared library (`libwebrtc_java_receiver.so` / `.dylib` / `.dll`).
 */
object NativeLibraryLoader {
    private const val LIBRARY_NAME = "webrtc_java_receiver"
    private val logger = Logger.getLogger(NativeLibraryLoader::class.java.name)
    private val isLoaded = AtomicBoolean(false)

    @JvmStatic
    fun loadNativeLibrary() {
        if (isLoaded.get()) return
        synchronized(isLoaded) {
            if (isLoaded.get()) return

            runCatching {
                System.loadLibrary(LIBRARY_NAME)
                isLoaded.set(true)
                logger.info("Successfully loaded native library via System.loadLibrary('$LIBRARY_NAME')")
                return
            }.onFailure { e ->
                logger.warning("System.loadLibrary('$LIBRARY_NAME') failed: ${e.message}. Attempting fallback extract.")
            }

            val osName = System.getProperty("os.name")?.lowercase() ?: ""
            val extension = when {
                osName.contains("mac") || osName.contains("darwin") -> ".dylib"
                osName.contains("win") -> ".dll"
                else -> ".so"
            }

            val resourceName = "lib${LIBRARY_NAME}$extension"
            val packageResourcePath = "emulator/videobridge/java/$resourceName"
            val externalResourcePath = "external/goldfish+/emulator/videobridge/java/liblibwebrtc_java_receiver$extension"
            val externalResourcePath2 = "external/goldfish+/emulator/videobridge/java/libwebrtc_java_receiver$extension"

            val resourceStream: InputStream? = NativeLibraryLoader::class.java.classLoader.getResourceAsStream(resourceName)
                ?: NativeLibraryLoader::class.java.getResourceAsStream("/$resourceName")
                ?: NativeLibraryLoader::class.java.classLoader.getResourceAsStream(packageResourcePath)
                ?: NativeLibraryLoader::class.java.getResourceAsStream("/$packageResourcePath")
                ?: NativeLibraryLoader::class.java.classLoader.getResourceAsStream(externalResourcePath)
                ?: NativeLibraryLoader::class.java.getResourceAsStream("/$externalResourcePath")
                ?: NativeLibraryLoader::class.java.classLoader.getResourceAsStream(externalResourcePath2)
                ?: NativeLibraryLoader::class.java.getResourceAsStream("/$externalResourcePath2")

            if (resourceStream == null) {
                val err = "Could not locate native library resource in classpath ($resourceName, $externalResourcePath)"
                logger.severe(err)
                throw UnsatisfiedLinkError(err)
            }

            try {
                val tempFile = File.createTempFile("lib${LIBRARY_NAME}_", extension).apply {
                    deleteOnExit()
                }
                FileOutputStream(tempFile).use { out ->
                    resourceStream.copyTo(out)
                }
                System.load(tempFile.absolutePath)
                isLoaded.set(true)
                logger.info("Successfully loaded extracted native library from: ${tempFile.absolutePath}")
            } catch (e: Throwable) {
                val err = "Failed to extract and load native library '$resourceName': ${e.message}"
                logger.severe(err)
                throw UnsatisfiedLinkError(err)
            }
        }
    }
}
