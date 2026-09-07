package com.kgivler.androidnoiseclicker

data class TcpNoisePayload(
    val sensor: String,
    val connectionNumber: Long,
    val listenPort: Int,
    val ipVersion: Int,
    val remoteAddress: String,
    val remotePort: Int,
    val seenCount: Long,
    val banner: String?,
    val bannerSent: Boolean,
    val payloadLength: Int,
    val payload: String
)