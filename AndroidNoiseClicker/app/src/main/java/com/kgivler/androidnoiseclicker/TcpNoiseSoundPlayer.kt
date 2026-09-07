package com.kgivler.androidnoiseclicker

import android.content.Context
import android.media.AudioAttributes
import android.media.SoundPool
import android.util.Log
import java.util.Collections

class TcpNoiseSoundPlayer(context: Context) {

    private var soundPool: SoundPool? = null
    private val loadedSounds = Collections.synchronizedSet(mutableSetOf<Int>())

    private val clickSound: Int
    private val newSound: Int
    private val ipv6Sound: Int
    private val bongSound: Int

    @Volatile
    private var lastClickTime = 0L
    private val minClickIntervalMs = 25L // Max 40 clicks/sec to prevent audio clipping

    init {
        val audioAttributes = AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_MEDIA)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
            .build()

        val pool = SoundPool.Builder()
            .setMaxStreams(32)
            .setAudioAttributes(audioAttributes)
            .build()

        pool.setOnLoadCompleteListener { _, sampleId, status ->
            if (status == 0) {
                loadedSounds.add(sampleId)
            } else {
                Log.e("TcpNoise", "Failed to load sample ID: $sampleId with status: $status")
            }
        }

        clickSound = pool.load(context, R.raw.click, 1)
        newSound = pool.load(context, R.raw.newip, 1)
        ipv6Sound = pool.load(context, R.raw.ipv6, 1)
        bongSound = pool.load(context, R.raw.bong, 1)

        soundPool = pool
    }

    fun play(event: TcpNoisePayload) {
        val pool = soundPool ?: return

        // Priority ladder: 3 = meme ports, 2 = unique IPs / IPv6, 1 = routine clicks
        val (soundId, priority) = when {
            event.listenPort == 420 || event.listenPort == 42069 -> Pair(bongSound, 3)
            event.ipVersion == 6 -> Pair(ipv6Sound, 2)
            event.seenCount == 1L -> Pair(newSound, 2)
            else -> Pair(clickSound, 1)
        }

        if (!loadedSounds.contains(soundId)) {
            return
        }

        // Throttle base click rate during massive scanner bursts
        if (soundId == clickSound) {
            val now = System.currentTimeMillis()
            if (now - lastClickTime < minClickIntervalMs) {
                return
            }
            lastClickTime = now
        }

        val streamId = pool.play(
            soundId,
            1.0f,  // Left volume
            1.0f,  // Right volume
            priority,
            0,     // Loop count
            1.0f   // Playback rate
        )

        if (streamId == 0) {
            Log.w("TcpNoise", "SoundPool stream rejected soundId: $soundId")
        }
    }

    fun release() {
        synchronized(this) {
            loadedSounds.clear()
            soundPool?.release()
            soundPool = null
        }
    }
}