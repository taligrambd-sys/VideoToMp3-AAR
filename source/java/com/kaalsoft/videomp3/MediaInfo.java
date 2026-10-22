package com.kaalsoft.videomp3;

/**
 * Information about a media file, returned by {@link VideoToMp3#probe(String)}.
 * All fields describe the file's audio track. (c) Kaalsoft
 */
public final class MediaInfo {

    /** Total duration in seconds, or -1 if the file does not say. */
    public final double durationSeconds;
    /** Audio codec name, e.g. "aac", "mp3", "opus", "ac3". */
    public final String audioCodec;
    /** Audio sample rate in Hz. */
    public final int sampleRate;
    /** Audio channel count of the source track. */
    public final int channels;
    /** Audio bitrate in bits per second, 0 if unknown. */
    public final long bitRate;
    /** True if the file also contains a video stream. */
    public final boolean hasVideo;
    /** Container format name reported by FFmpeg, e.g. "mov,mp4,m4a,3gp,...". */
    public final String format;

    MediaInfo(double durationSeconds, String audioCodec, int sampleRate,
              int channels, long bitRate, boolean hasVideo, String format) {
        this.durationSeconds = durationSeconds;
        this.audioCodec = audioCodec;
        this.sampleRate = sampleRate;
        this.channels = channels;
        this.bitRate = bitRate;
        this.hasVideo = hasVideo;
        this.format = format;
    }

    /** Duration formatted as m:ss (or h:mm:ss), "-" when unknown. */
    public String durationFormatted() {
        if (durationSeconds < 0) return "-";
        long total = Math.round(durationSeconds);
        long h = total / 3600;
        long m = (total % 3600) / 60;
        long s = total % 60;
        if (h > 0) return h + ":" + pad(m) + ":" + pad(s);
        return m + ":" + pad(s);
    }

    private static String pad(long v) {
        return v < 10 ? "0" + v : Long.toString(v);
    }

    @Override
    public String toString() {
        return "MediaInfo{duration=" + durationFormatted()
                + ", codec=" + audioCodec
                + ", sampleRate=" + sampleRate
                + ", channels=" + channels
                + ", bitRate=" + bitRate
                + ", hasVideo=" + hasVideo
                + ", format=" + format + "}";
    }
}
