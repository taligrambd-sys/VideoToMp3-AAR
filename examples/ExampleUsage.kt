// VideoToMp3 AAR - Kotlin usage examples (copy-paste ready)
// Package: com.kaalsoft.videomp3  |  (c) Kaalsoft
//
// IMPORTANT: convert() blocks the calling thread - call it from a background
// thread / coroutine (withContext(Dispatchers.IO)), never from the UI thread.

import com.kaalsoft.videomp3.Options
import com.kaalsoft.videomp3.VideoToMp3

// 1) Simplest: default quality (192 kbps CBR)
fun simple(inPath: String, outPath: String) {
    val ok = VideoToMp3.convert(inPath, outPath)
    if (!ok) {
        val error = VideoToMp3.lastError()
        // show error to the user
    }
}

// 2) Choose a bitrate directly
fun withBitrate(inPath: String, outPath: String) {
    val ok = VideoToMp3.convert(inPath, outPath, 320) // 320 kbps
}

// 3) Full control + progress
fun fullControl(inPath: String, outPath: String) {
    val options = Options.Builder()
        .bitrateKbps(192)
        .channels(Options.CHANNELS_STEREO)
        .title("My Song")
        .artist("Unknown Artist")
        .build()

    val ok = VideoToMp3.convert(inPath, outPath, options) { percent ->
        // 0..100 - called on the converting (background) thread
        println("Progress: $percent%")
    }
}

// 4) Best quality VBR + trim + probe
fun moreExamples(inPath: String, outPath: String) {
    val info = VideoToMp3.probe(inPath)
    println("Duration: ${info?.durationFormatted()}, codec: ${info?.audioCodec}")

    val options = Options.Builder()
        .mode(Options.Mode.VBR)
        .vbrQuality(0)          // 0 = best
        .trim(30.0, 60.0)       // from 0:30, 60 seconds
        .build()
    val ok = VideoToMp3.convert(inPath, outPath, options)

    // Speed (v2.0): parallel is automatic by default; override if needed:
    val fast = Options.Builder().threads(4).encoderQuality(7).build()

    // Cancel a running conversion from any thread:
    // VideoToMp3.cancel()
}
