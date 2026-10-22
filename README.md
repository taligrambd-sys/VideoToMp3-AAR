# VideoToMp3 AAR

A fast, native Android library that converts **any video (or audio) file to MP3** —
bitrate, quality, trim, volume, ID3 tags and more, all controllable from Java/Kotlin.

Built on a trimmed **FFmpeg 6.1.2 + LAME 3.100** engine compiled to a single native
library. Parallel chunk encoding on multi-core phones converts long videos in a
fraction of the single-thread time.

© Kaalsoft

## Features

- Ready-made presets — `Options.lowQuality()`, `Options.standard()`, `Options.high()`, `Options.veryHigh()`, `Options.max()`, `Options.vbrHigh()`, `Options.vbrBest()` — or full manual control via `Options.Builder`
- Modes: **CBR / ABR / VBR** (VBR quality 0–9), bitrate 8–320 kbps
- Sample-rate conversion, channel control (keep / mono / stereo)
- Trim by start + duration (sample-accurate)
- Volume by percent or dB
- Multi-track files: choose which audio track to convert
- ID3 tags: title / artist / album written into the MP3
- `probe()` media info, progress callbacks (0–100), `cancel()`
- Parallel encoding: `threads` option (0 = auto) and LAME `encoderQuality` tuning
- Zero dependencies — no AndroidX, plain Java 8 API

## Requirements

- Android **minSdk 21** or higher
- **arm64-v8a devices only** (virtually all phones since ~2019; no 32-bit or x86 emulator support)
- Works from Java or Kotlin

## Get the AAR

Download **`videomp3-release.aar`** from this repository (root folder) and copy it
into your app project's `libs/` folder. Then add one line to your module
`build.gradle`:

```gradle
dependencies {
    implementation(files("libs/videomp3-release.aar"))
}
```

## Quick start

```java
import com.kaalsoft.videomp3.VideoToMp3;
import com.kaalsoft.videomp3.Options;

// Simple: 192 kbps CBR
boolean ok = VideoToMp3.convert("/path/in.mp4", "/path/out.mp3", 192);

// Full control + progress
Options o = new Options.Builder()
        .bitrateKbps(320)
        .trim(1.0, 900.0)          // start at 1 s, take 900 s
        .title("My title")
        .threads(0)                // 0 = auto parallel
        .build();
boolean ok = VideoToMp3.convert(inPath, outPath, o,
        percent -> { /* 0..100, called on the conversion thread */ });

// Or a ready-made preset
boolean ok2 = VideoToMp3.convert(inPath, outPath, Options.vbrBest());

if (!ok) String err = VideoToMp3.lastError();
```

See `examples/ExampleUsage.java` and `examples/ExampleUsage.kt` for complete
examples, and `VideoToMp3-Sample` (in `source/aar-project/sample`) for a full
tester app with every option in the UI.

## Repository layout

- `videomp3-release.aar` — the ready-to-use library (arm64-v8a)
- `examples/` — Java + Kotlin usage examples
- `source/` — full source: the C engine core, the JNI bridge, the Java API,
  the Gradle project (library + sample app) and the build scripts
  (needs JDK 17, Android SDK 36, NDK r27c; see `source/scripts/`)
- `licenses/` — license texts for the bundled FFmpeg and LAME

## License

The library code by Kaalsoft (Java API, C conversion core, JNI bridge, sample app)
is released under the **MIT License** — see `LICENSE`.

The native engine bundles **FFmpeg 6.1.2** and **LAME 3.100**, which are licensed
under the **LGPL v2.1** — their texts are in `licenses/`. If you ship an app that
uses this library, keep the FFmpeg/LAME license notices in your app (for example
in an "Open-source licenses" page). Your own app code stays yours; LGPL does not
make it open source, and commercial use is allowed.
