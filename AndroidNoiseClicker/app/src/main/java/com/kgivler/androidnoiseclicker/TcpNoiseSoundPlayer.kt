package com.kgivler.androidnoiseclicker

import android.content.Context
import android.media.AudioAttributes
import android.media.SoundPool
import java.util.Collections
import android.util.Log

class TcpNoiseSoundPlayer(context: Context) {

    private val soundPool: SoundPool

    private val loadedSounds =
        Collections.synchronizedSet(mutableSetOf<Int>())

    private val clickSound: Int
    private val newSound: Int
    private val ipv6Sound: Int
    private val bongSound: Int

    init {
        val audioAttributes = AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_MEDIA)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
            .build()

        soundPool = SoundPool.Builder()
            .setMaxStreams(32)
            .setAudioAttributes(audioAttributes)
            .build()

        soundPool.setOnLoadCompleteListener { _, sampleId, status ->
            if (status == 0) {
                loadedSounds.add(sampleId)
            }
        }

        clickSound = soundPool.load(context, R.raw.click, 1)
        newSound = soundPool.load(context, R.raw.newip, 1)
        ipv6Sound = soundPool.load(context, R.raw.ipv6, 1)
        bongSound = soundPool.load(context, R.raw.bong, 1)
    }

    fun play(event: TcpNoisePayload) {
        val soundId = when {
            event.listenPort == 420 ||
                    event.listenPort == 42069 -> bongSound

            event.ipVersion == 6 -> ipv6Sound

            event.seenCount == 1L -> newSound

            else -> clickSound
        }

        if (!loadedSounds.contains(soundId)) {
            return
        }

        val streamId = soundPool.play(
            soundId,
            1.0f,
            1.0f,
            1,
            0,
            1.0f
        )

        if (streamId == 0) {
            Log.d("TcpNoise", "SoundPool could not start sound")
        }
    }

    fun release() {
        soundPool.release()
    }
}