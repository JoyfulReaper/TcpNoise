package com.kgivler.androidnoiseclicker

import android.util.Log
import io.nats.client.Nats
import io.nats.client.Options
import org.json.JSONObject
import java.nio.charset.StandardCharsets
import io.nats.client.ConnectionListener
import io.nats.client.Connection

class TcpNoiseListener(
    private val onEvent: (TcpNoisePayload) -> Unit,
    private val onStatusChanged: (String) -> Unit
) {
    @Volatile
    private var connection: Connection? = null
    fun start() {
        Thread {
            try {
                Log.d("TcpNoise", "Connecting to nats://10.99.0.1:4222")

                val options = Options.Builder()
                    .server("nats://10.99.0.1:4222")
                    .maxReconnects(-1)
                    .connectionListener { _, event ->
                        val status = when (event) {
                            ConnectionListener.Events.CONNECTED -> "Connected"
                            ConnectionListener.Events.DISCONNECTED -> "Disconnected"
                            ConnectionListener.Events.RECONNECTED -> "Connected"
                            ConnectionListener.Events.CLOSED -> "Disconnected"
                            else -> event.toString()
                        }

                        Log.d("TcpNoise", "NATS status: $status")
                        onStatusChanged(status)
                    }
                    .build()

                onStatusChanged("Connecting")
                connection = Nats.connect(options)

                val activeConnection = connection
                    ?: return@Thread

                Log.d("TcpNoise", "Connected to NATS")

                val dispatcher = activeConnection.createDispatcher { message ->
                    val text = String(
                        message.data,
                        StandardCharsets.UTF_8
                    )

                    Log.d("TcpNoise", "tcpnoise: $text")

                    try {
                        val root = JSONObject(text)
                        val json = root.getJSONObject("Payload")

                        val payload = TcpNoisePayload(
                            sensor = json.optString("sensor", "unknown"),
                            connectionNumber = json.getLong("connectionNumber"),
                            listenPort = json.getInt("listenPort"),
                            ipVersion = json.getInt("ipVersion"),
                            remoteAddress = json.getString("remoteAddress"),
                            remotePort = json.getInt("remotePort"),
                            seenCount = json.getLong("seenCount"),
                            banner = if (json.isNull("banner")) {
                                null
                            } else {
                                json.getString("banner")
                            },
                            bannerSent = json.optBoolean("bannerSent", false),
                            payloadLength = json.getInt("payloadLength"),
                            payload = json.optString("payload", "")
                        )

                        onEvent(payload)
                    } catch (ex: Exception) {
                        Log.e("TcpNoise", "Failed to parse tcpnoise event", ex)
                    }
                }

                dispatcher.subscribe("tcpnoise.connection")

            } catch (ex: Exception) {
                Log.e("TcpNoise", "NATS connection failed", ex)
            }
        }.start()
    }

    fun stop() {
        try {
            connection?.close()
        } catch (ex: Exception) {
            Log.e("TcpNoise", "Failed to close NATS connection", ex)
        } finally {
            connection = null
        }
    }
}