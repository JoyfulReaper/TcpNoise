package com.kgivler.androidnoiseclicker

import android.util.Log
import io.nats.client.Nats
import io.nats.client.Options
import org.json.JSONObject
import java.nio.charset.StandardCharsets

class TcpNoiseListener(
    private val onEvent: (TcpNoisePayload) -> Unit
) {

    fun start() {
        Thread {
            try {
                Log.d("TcpNoise", "Connecting to nats://10.99.0.1:4222")

                val options = Options.Builder()
                    .server("nats://10.99.0.1:4222")
                    .maxReconnects(-1)
                    .build()

                val connection = Nats.connect(options)

                Log.d("TcpNoise", "Connected to NATS")

                val dispatcher = connection.createDispatcher { message ->
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
}