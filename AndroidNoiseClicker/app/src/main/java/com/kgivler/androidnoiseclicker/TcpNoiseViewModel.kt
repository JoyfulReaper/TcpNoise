package com.kgivler.androidnoiseclicker

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import android.os.Handler
import android.os.Looper
import android.app.Application
import androidx.lifecycle.AndroidViewModel

class TcpNoiseViewModel(
    application: Application
) : AndroidViewModel(application) {

    private val soundPlayer = TcpNoiseSoundPlayer(application)

    override fun onCleared() {
        listener.stop()
        soundPlayer.release()
        mainHandler.removeCallbacksAndMessages(null)

        super.onCleared()
    }

    private val mainHandler = Handler(Looper.getMainLooper())

    var latestEvent by mutableStateOf<TcpNoisePayload?>(null)
        private set

    var eventCount by mutableIntStateOf(0)
        private set

    var connectionStatus by mutableStateOf("Connecting")
        private set

    val recentEvents = mutableStateListOf<TcpNoisePayload>()

    private val listener = TcpNoiseListener(
        onEvent = { event ->
            mainHandler.post {
                addEvent(event)
            }
        },
        onStatusChanged = { status ->
            mainHandler.post {
                updateConnectionStatus(status)
            }
        }
    )

    init {
        listener.start()
    }

    private fun addEvent(event: TcpNoisePayload) {
        latestEvent = event
        eventCount++

        recentEvents.add(0, event)

        if (recentEvents.size > 10) {
            recentEvents.removeAt(recentEvents.lastIndex)
        }

        soundPlayer.play(event)
    }

    private fun updateConnectionStatus(status: String) {
        connectionStatus = status
    }
}