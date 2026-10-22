package com.kaalsoft.videomp3;

/**
 * VideoToMp3 - extract the audio of any video (or audio) file as MP3.
 *
 * <p>Powered by a trimmed native engine (FFmpeg + LAME). No Android
 * dependencies, no AndroidX, no permissions and no internet needed - it works
 * in Android Studio / Gradle projects, Kotlin projects and Sketchware.
 *
 * <p>Supported input containers include MP4/MOV/3GP, MKV/WebM, AVI, FLV,
 * MPEG-TS/PS, WMV/ASF and pure audio files (MP3, AAC, WAV, FLAC, OGG...),
 * with audio codecs AAC, MP3, AC3/E-AC3, Opus, Vorbis, FLAC, ALAC, WMA,
 * AMR and PCM. Only the audio track is decoded, so conversion is fast.
 *
 * <p>All {@code convert(...)} methods <b>block the calling thread</b> until
 * the conversion finishes - always call them from a background thread,
 * never from the UI thread.
 *
 * <p>Simple use:
 * <pre>
 * boolean ok = VideoToMp3.convert("/sdcard/Movies/clip.mp4",
 *                                 "/sdcard/Music/clip.mp3");
 * if (!ok) Log.e("TAG", VideoToMp3.lastError());
 * </pre>
 *
 * <p>With full control:
 * <pre>
 * Options o = new Options.Builder()
 *         .bitrateKbps(192)
 *         .title("Clip audio")
 *         .build();
 * boolean ok = VideoToMp3.convert(in, out, o, percent -&gt; {
 *     // progress 0..100, called on the converting thread
 * });
 * </pre>
 *
 * (c) Kaalsoft
 */
public final class VideoToMp3 {

    static {
        System.loadLibrary("videomp3");
    }

    private VideoToMp3() {
    }

    /** Progress callback. {@code percent} runs 0..100 and fires on the
     *  thread that called {@code convert(...)} - post to a Handler if you
     *  need to update the UI. */
    public interface ProgressListener {
        void onProgress(int percent);
    }

    /**
     * Convert with default settings (192 kbps CBR).
     *
     * @return true on success; on false call {@link #lastError()}.
     */
    public static boolean convert(String inputPath, String outputPath) {
        return convert(inputPath, outputPath, Options.defaults(), null);
    }

    /** Convert with the given CBR bitrate (e.g. 128, 192, 320). */
    public static boolean convert(String inputPath, String outputPath,
                                  int bitrateKbps) {
        return convert(inputPath, outputPath,
                new Options.Builder().bitrateKbps(bitrateKbps).build(), null);
    }

    /** Convert with full {@link Options} control. */
    public static boolean convert(String inputPath, String outputPath,
                                  Options options) {
        return convert(inputPath, outputPath, options, null);
    }

    /** Convert with full {@link Options} control and progress updates. */
    public static boolean convert(String inputPath, String outputPath,
                                  Options options,
                                  ProgressListener listener) {
        if (inputPath == null || outputPath == null) return false;
        if (options == null) options = Options.defaults();
        return nativeConvert(inputPath, outputPath,
                options.mode.ordinal(),
                options.bitrateKbps,
                options.vbrQuality,
                options.sampleRate,
                options.channels,
                options.startSeconds,
                options.durationSeconds,
                options.volume,
                options.audioStream,
                options.title,
                options.artist,
                options.album,
                options.threads,
                options.encoderQuality,
                listener);
    }

    /**
     * Ask a conversion running on another thread to stop. The running
     * {@code convert(...)} call returns false and {@link #lastError()}
     * reports "Conversion cancelled"; the partial output file is removed.
     */
    public static void cancel() {
        nativeCancel();
    }

    /**
     * Read information about a media file without converting it.
     *
     * @return a {@link MediaInfo}, or null if the file cannot be read or
     *         has no audio track (see {@link #lastError()}).
     */
    public static MediaInfo probe(String inputPath) {
        if (inputPath == null) return null;
        String raw = nativeProbe(inputPath);
        if (raw == null) return null;
        try {
            String[] p = raw.split("\\|", -1);
            return new MediaInfo(
                    Double.parseDouble(p[0]),
                    p[1],
                    Integer.parseInt(p[2]),
                    Integer.parseInt(p[3]),
                    Long.parseLong(p[4]),
                    "1".equals(p[5]),
                    p.length > 6 ? p[6] : "");
        } catch (RuntimeException e) {
            return null;
        }
    }

    /** Engine version string, e.g. "FFmpeg 6.1.2 / LAME 3.100 (libmp3lame)". */
    public static String version() {
        return nativeVersion();
    }

    /** Why the last convert/probe call failed (empty string if none). */
    public static String lastError() {
        String e = nativeLastError();
        return e == null ? "" : e;
    }

    // ---- native ----

    private static native boolean nativeConvert(
            String inputPath, String outputPath,
            int mode, int bitrateKbps, int vbrQuality,
            int sampleRate, int channels,
            double startSeconds, double durationSeconds, double volume,
            int audioStream,
            String title, String artist, String album,
            int threads, int encoderQuality,
            ProgressListener listener);

    private static native String nativeProbe(String inputPath);

    private static native String nativeVersion();

    private static native String nativeLastError();

    private static native void nativeCancel();
}
