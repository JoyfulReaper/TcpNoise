package com.kgivler.androidnoiseclicker

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel

class TcpNoiseViewModel : ViewModel() {

    var latestEvent by mutableStateOf<TcpNoisePayload?>(null)
        private set

    var eventCount by mutableIntStateOf(0)
        private set

    var connectionStatus by mutableStateOf("Connecting")
        private set

    val recentEvents = mutableStateListOf<TcpNoisePayload>()

    fun addEvent(event: TcpNoisePayload) {
        latestEvent = event
        eventCount++

        recentEvents.add(0, event)

        if (recentEvents.size > 10) {
            recentEvents.removeAt(recentEvents.lastIndex)
        }
    }

    fun updateConnectionStatus(status: String) {
        connectionStatus = status
    }
}