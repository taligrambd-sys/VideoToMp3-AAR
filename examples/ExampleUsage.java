// VideoToMp3 AAR - Java usage examples (copy-paste ready)
// Package: com.kaalsoft.videomp3  |  (c) Kaalsoft
//
// IMPORTANT: convert() blocks the calling thread - always run it on a
// background thread (new Thread / Executor / AsyncTask), never on the UI thread.

import com.kaalsoft.videomp3.MediaInfo;
import com.kaalsoft.videomp3.Options;
import com.kaalsoft.videomp3.VideoToMp3;

public class ExampleUsage {

    // 1) Simplest: default quality (192 kbps CBR)
    void simple(String in, String out) {
        boolean ok = VideoToMp3.convert(in, out);
        if (!ok) {
            String error = VideoToMp3.lastError();
            // show error to the user
        }
    }

    // 2) Choose a bitrate directly
    void withBitrate(String in, String out) {
        boolean ok = VideoToMp3.convert(in, out, 320); // 320 kbps
    }

    // 3) Full control + progress
    void fullControl(String in, String out) {
        Options options = new Options.Builder()
                .bitrateKbps(192)                 // CBR bitrate
                .channels(Options.CHANNELS_STEREO)
                .title("My Song")                 // ID3 tags
                .artist("Unknown Artist")
                .build();

        boolean ok = VideoToMp3.convert(in, out, options, percent -> {
            // 0..100 - called on THIS (background) thread.
            // Use a Handler / runOnUiThread to update a ProgressBar.
            System.out.println("Progress: " + percent + "%");
        });
    }

    // 4) Best quality VBR (variable bitrate)
    void bestQuality(String in, String out) {
        boolean ok = VideoToMp3.convert(in, out, Options.vbrBest());
    }

    // 5) Trim: convert only 0:30 to 1:30, make it mono, lower volume
    void trimExample(String in, String out) {
        Options options = new Options.Builder()
                .trim(30, 60)                     // start 30s, length 60s
                .channels(Options.CHANNELS_MONO)
                .volumePercent(80)
                .bitrateKbps(128)
                .build();
        boolean ok = VideoToMp3.convert(in, out, options);
    }

    // 6) Multi-language video: convert the SECOND audio track
    void secondAudioTrack(String in, String out) {
        Options options = new Options.Builder()
                .audioStream(1)                   // 0 = first track
                .bitrateKbps(192)
                .build();
        boolean ok = VideoToMp3.convert(in, out, options);
    }

    // 7) Probe a file before converting
    void probeExample(String in) {
        MediaInfo info = VideoToMp3.probe(in);
        if (info != null) {
            System.out.println("Duration: " + info.durationFormatted());
            System.out.println("Audio codec: " + info.audioCodec);
            System.out.println("Sample rate: " + info.sampleRate);
            System.out.println("Channels: " + info.channels);
            System.out.println("Has video: " + info.hasVideo);
        }
    }

    // 7b) Speed: parallel encoding (v2.0) - auto is the default,
    //     these are only needed to override it
    void speedExample(String in, String out) {
        Options fast = new Options.Builder()
                .bitrateKbps(192)
                .threads(4)          // 0 = auto (default), 1 = single
                .encoderQuality(7)   // faster LAME algorithm (default -1)
                .build();
        boolean ok = VideoToMp3.convert(in, out, fast);
    }

    // 8) Cancel a running conversion (call from any other thread)
    void cancelExample() {
        VideoToMp3.cancel();
    }
}
