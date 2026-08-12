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

import com.android.emulator.control.Id
import com.android.emulator.control.JsepMsg
import com.android.emulator.control.ReceiveJsepMessageRequest
import com.android.emulator.control.ReceiveJsepMessageResponse
import com.android.emulator.control.RtcGrpc
import com.android.emulator.control.RtcStreamRequest
import com.android.emulator.control.SendJsepMessageRequest
import com.android.emulator.webrtc.JsepMessageListener
import io.grpc.ManagedChannel
import io.grpc.ManagedChannelBuilder
import io.grpc.stub.StreamObserver
import java.util.concurrent.TimeUnit
import java.util.logging.Logger

/**
 * UI-Agnostic gRPC signaling client for Video Bridge (`RtcService` v2 API).
 *
 * Handles gRPC channel setup, stream creation (`requestRtcStream`), JSEP message sending
 * (`sendJsepMessage`), and inbound JSEP message streaming (`receiveJsepMessageStream`).
 *
 * @param host Video Bridge server hostname or IP address (e.g. "localhost").
 * @param port Video Bridge gRPC port (default: 8554).
 * @param channel Optional pre-configured gRPC [ManagedChannel] (created automatically if omitted).
 */
class VideoBridgeSignalingClient(
    val host: String = "localhost",
    val port: Int = 8554,
    channel: ManagedChannel? = null
) : AutoCloseable {

    private val logger = Logger.getLogger(VideoBridgeSignalingClient::class.java.name)
    private val managedChannel: ManagedChannel = channel ?: ManagedChannelBuilder.forAddress(host, port)
        .usePlaintext()
        .build()

    private val asyncStub: RtcGrpc.RtcStub = RtcGrpc.newStub(managedChannel)
    private val blockingStub: RtcGrpc.RtcBlockingStub = RtcGrpc.newBlockingStub(managedChannel)

    private var connectionId: Id? = null
    private var activeStreamObserver: StreamObserver<ReceiveJsepMessageResponse>? = null

    /** Returns the active connection GUID assigned by Video Bridge, or empty if disconnected. */
    val connectionGuid: String
        get() = connectionId?.guid ?: ""

    /**
     * Connects to Video Bridge and requests a new WebRTC session stream via `requestRtcStream`.
     *
     * @return The assigned [Id] connection handle.
     * @throws RuntimeException if gRPC connection fails.
     */
    @Synchronized
    fun requestRtcStream(): Id {
        logger.info("Requesting RTC Stream from Video Bridge at $host:$port...")
        val response = blockingStub.requestRtcStream(RtcStreamRequest.newBuilder().build())
        connectionId = response.id
        logger.info("Connected to Video Bridge. Connection GUID: ${connectionId?.guid}")
        return connectionId!!
    }

    /**
     * Sends an outbound JSEP JSON message (SDP offer or candidate) to Video Bridge via `sendJsepMessage`.
     *
     * @param jsepJson JSEP JSON string message.
     * @return True if message was sent successfully; false otherwise.
     */
    @Synchronized
    fun sendJsepMessage(jsepJson: String): Boolean {
        val cid = connectionId ?: run {
            logger.severe("Cannot send JSEP message: connectionId is null (not connected).")
            return false
        }

        logger.info("Sending outbound JSEP message to Video Bridge: $jsepJson")
        val msg = JsepMsg.newBuilder()
            .setId(cid)
            .setMessage(jsepJson)
            .build()

        return try {
            blockingStub.sendJsepMessage(SendJsepMessageRequest.newBuilder().setJsepMsg(msg).build())
            true
        } catch (e: Exception) {
            logger.severe("Failed to send JSEP message to Video Bridge: ${e.message}")
            false
        }
    }

    /**
     * Subscribes to the inbound JSEP message stream from Video Bridge via `receiveJsepMessageStream`.
     *
     * @param listener Callback invoked when inbound JSEP messages arrive.
     * @param onError Callback invoked if a stream error occurs.
     * @param onCompleted Callback invoked when the server closes the JSEP stream.
     */
    @Synchronized
    fun subscribeJsepStream(
        listener: JsepMessageListener,
        onError: (Throwable) -> Unit = {},
        onCompleted: () -> Unit = {}
    ) {
        val cid = connectionId ?: run {
            logger.severe("Cannot subscribe to JSEP stream: connectionId is null.")
            return
        }

        logger.info("Subscribing to JSEP message stream for connection GUID: ${cid.guid}")
        val req = ReceiveJsepMessageRequest.newBuilder().setId(cid).build()

        val observer = object : StreamObserver<ReceiveJsepMessageResponse> {
            override fun onNext(response: ReceiveJsepMessageResponse) {
                val json = response.jsepMsg.message
                logger.info("Received JSEP message from Video Bridge stream: $json")
                listener.onOutboundJsepMessage(json)
            }

            override fun onError(t: Throwable) {
                logger.severe("Video Bridge JSEP Stream error: ${t.message}")
                onError(t)
            }

            override fun onCompleted() {
                logger.info("Video Bridge JSEP Stream completed.")
                onCompleted()
            }
        }

        asyncStub.receiveJsepMessageStream(req, observer)
    }

    /**
     * Closes the gRPC channel and releases network resources.
     */
    override fun close() {
        logger.info("Closing VideoBridgeSignalingClient channel...")
        try {
            managedChannel.shutdownNow()
            managedChannel.awaitTermination(2, TimeUnit.SECONDS)
        } catch (e: Exception) {
            logger.warning("Interrupted while shutting down signaling channel: ${e.message}")
        }
    }
}
