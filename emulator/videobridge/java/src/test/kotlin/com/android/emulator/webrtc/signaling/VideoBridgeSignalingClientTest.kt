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
package com.android.emulator.webrtc.signaling

import io.grpc.ManagedChannel
import io.grpc.inprocess.InProcessChannelBuilder
import io.grpc.inprocess.InProcessServerBuilder
import io.grpc.testing.GrpcCleanupRule
import org.junit.Rule
import org.junit.Test
import java.util.concurrent.atomic.AtomicBoolean
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue

public class VideoBridgeSignalingClientTest {

    @get:Rule
    val grpcCleanup = GrpcCleanupRule()

    @Test
    fun testSignalingClientChannelLifecycle() {
        val serverName = InProcessServerBuilder.generateName()
        val server = InProcessServerBuilder.forName(serverName)
            .directExecutor()
            .build()
        grpcCleanup.register(server.start())

        val channel = grpcCleanup.register(
            InProcessChannelBuilder.forName(serverName).directExecutor().build()
        )

        val client = VideoBridgeSignalingClient(host = "localhost", port = 8554, channel = channel)
        assertEquals("", client.connectionGuid)
        client.close()
    }
}
