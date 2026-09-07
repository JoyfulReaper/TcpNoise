package com.kgivler.androidnoiseclicker

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.kgivler.androidnoiseclicker.ui.theme.AndroidNoiseClickerTheme

class MainActivity : ComponentActivity() {

    private var latestEvent by mutableStateOf<TcpNoisePayload?>(null)
    private var eventCount by mutableIntStateOf(0)

    private lateinit var tcpNoiseListener: TcpNoiseListener

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        tcpNoiseListener = TcpNoiseListener { event ->
            runOnUiThread {
                latestEvent = event
                eventCount++
            }
        }

        tcpNoiseListener.start()

        enableEdgeToEdge()

        setContent {
            AndroidNoiseClickerTheme {
                Scaffold(
                    modifier = Modifier.fillMaxSize()
                ) { innerPadding ->
                    TcpNoiseScreen(
                        eventCount = eventCount,
                        latestEvent = latestEvent,
                        modifier = Modifier.padding(innerPadding)
                    )
                }
            }
        }
    }
}

@Composable
fun TcpNoiseScreen(
    eventCount: Int,
    latestEvent: TcpNoisePayload?,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(24.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Text(
            text = "TCPNoise",
            style = MaterialTheme.typography.headlineLarge
        )

        Text(
            text = "$eventCount events",
            style = MaterialTheme.typography.headlineMedium
        )

        if (latestEvent == null) {
            Text("Waiting for Internet garbage...")
        } else {
            Text(
                text = "Last hit",
                style = MaterialTheme.typography.titleMedium
            )

            Text("Sensor: ${latestEvent.sensor}")

            Text(
                "${latestEvent.remoteAddress} → ${latestEvent.listenPort}"
            )

            Text(
                "IPv${latestEvent.ipVersion}  •  " +
                        "Seen ${latestEvent.seenCount} times"
            )

            Text(
                "Remote port: ${latestEvent.remotePort}"
            )
        }
    }
}