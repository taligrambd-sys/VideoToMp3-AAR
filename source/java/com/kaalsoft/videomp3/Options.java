package com.kaalsoft.videomp3;

/**
 * Conversion settings for {@link VideoToMp3}. Immutable; create instances
 * with the ready-made presets or the fluent {@link Builder}.
 *
 * <p>Example:
 * <pre>
 * Options o = new Options.Builder()
 *         .bitrateKbps(192)          // CBR bitrate
 *         .channels(Options.CHANNELS_STEREO)
 *         .trim(10, 60)              // from 10s, 60 seconds long
 *         .title("My Song")
 *         .build();
 * </pre>
 *
 * (c) Kaalsoft
 */
public final class Options {

    /** Keep the source sample rate (resampled only if LAME cannot use it). */
    public static final int SAMPLE_RATE_KEEP = 0;
    /** Keep the source channel count (surround is downmixed to stereo). */
    public static final int CHANNELS_KEEP = 0;
    public static final int CHANNELS_MONO = 1;
    public static final int CHANNELS_STEREO = 2;

    public enum Mode {
        /** Constant bitrate - predictable file size. */
        CBR,
        /** Average bitrate - LAME targets the bitrate on average. */
        ABR,
        /** Variable bitrate - best quality for a given size; uses vbrQuality. */
        VBR
    }

    final Mode mode;
    final int bitrateKbps;
    final int vbrQuality;
    final int sampleRate;
    final int channels;
    final double startSeconds;
    final double durationSeconds;
    final double volume; // linear gain, 1.0 = unchanged
    final int audioStream;
    final String title;
    final String artist;
    final String album;
    final int threads;
    final int encoderQuality;

    private Options(Builder b) {
        this.mode = b.mode;
        this.bitrateKbps = b.bitrateKbps;
        this.vbrQuality = b.vbrQuality;
        this.sampleRate = b.sampleRate;
        this.channels = b.channels;
        this.startSeconds = b.startSeconds;
        this.durationSeconds = b.durationSeconds;
        this.volume = b.volume;
        this.audioStream = b.audioStream;
        this.title = b.title;
        this.artist = b.artist;
        this.album = b.album;
        this.threads = b.threads;
        this.encoderQuality = b.encoderQuality;
    }

    public Mode getMode() { return mode; }
    public int getBitrateKbps() { return bitrateKbps; }
    public int getVbrQuality() { return vbrQuality; }
    public int getSampleRate() { return sampleRate; }
    public int getChannels() { return channels; }
    public double getStartSeconds() { return startSeconds; }
    public double getDurationSeconds() { return durationSeconds; }
    public double getVolume() { return volume; }
    public int getAudioStream() { return audioStream; }
    public int getThreads() { return threads; }
    public int getEncoderQuality() { return encoderQuality; }
    public String getTitle() { return title; }
    public String getArtist() { return artist; }
    public String getAlbum() { return album; }

    /** Default settings: 192 kbps CBR, everything else kept from the source. */
    public static Options defaults() {
        return new Builder().build();
    }

    /** 96 kbps CBR - small files, voice / low quality sources. */
    public static Options lowQuality() {
        return new Builder().bitrateKbps(96).build();
    }

    /** 128 kbps CBR - the classic standard quality. */
    public static Options standard() {
        return new Builder().bitrateKbps(128).build();
    }

    /** 192 kbps CBR - high quality, good all-round choice. */
    public static Options high() {
        return new Builder().bitrateKbps(192).build();
    }

    /** 256 kbps CBR - very high quality. */
    public static Options veryHigh() {
        return new Builder().bitrateKbps(256).build();
    }

    /** 320 kbps CBR - maximum MP3 bitrate. */
    public static Options max() {
        return new Builder().bitrateKbps(320).build();
    }

    /** VBR quality 2 - transparent quality for most music, smaller files. */
    public static Options vbrHigh() {
        return new Builder().mode(Mode.VBR).vbrQuality(2).build();
    }

    /** VBR quality 0 - the best quality LAME can produce. */
    public static Options vbrBest() {
        return new Builder().mode(Mode.VBR).vbrQuality(0).build();
    }

    public static final class Builder {
        private Mode mode = Mode.CBR;
        private int bitrateKbps = 192;
        private int vbrQuality = 2;
        private int sampleRate = SAMPLE_RATE_KEEP;
        private int channels = CHANNELS_KEEP;
        private double startSeconds = 0;
        private double durationSeconds = 0;
        private double volume = 1.0;
        private int audioStream = -1;
        private String title;
        private String artist;
        private String album;
        private int threads = 0;
        private int encoderQuality = -1;

        /** Bitrate mode: CBR (default), ABR or VBR. */
        public Builder mode(Mode mode) {
            if (mode == null) throw new IllegalArgumentException("mode == null");
            this.mode = mode;
            return this;
        }

        /** Target bitrate for CBR / ABR, 32..320 kbps (default 192). */
        public Builder bitrateKbps(int kbps) {
            if (kbps < 8 || kbps > 320)
                throw new IllegalArgumentException(
                        "bitrateKbps must be 8..320, was " + kbps);
            this.bitrateKbps = kbps;
            return this;
        }

        /** VBR quality: 0 = best quality/biggest file .. 9 = smallest file. */
        public Builder vbrQuality(int quality) {
            if (quality < 0 || quality > 9)
                throw new IllegalArgumentException(
                        "vbrQuality must be 0..9, was " + quality);
            this.vbrQuality = quality;
            return this;
        }

        /**
         * Output sample rate in Hz, e.g. 44100. Use {@link #SAMPLE_RATE_KEEP}
         * (default) to keep the source rate. Accepted values: 8000, 11025,
         * 12000, 16000, 22050, 24000, 32000, 44100, 48000.
         */
        public Builder sampleRate(int hz) {
            if (hz != 0) {
                switch (hz) {
                    case 8000: case 11025: case 12000: case 16000:
                    case 22050: case 24000: case 32000: case 44100:
                    case 48000:
                        break;
                    default:
                        throw new IllegalArgumentException(
                                "unsupported sampleRate: " + hz);
                }
            }
            this.sampleRate = hz;
            return this;
        }

        /** {@link #CHANNELS_KEEP} (default), {@link #CHANNELS_MONO} or {@link #CHANNELS_STEREO}. */
        public Builder channels(int channels) {
            if (channels < 0 || channels > 2)
                throw new IllegalArgumentException(
                        "channels must be 0 (keep), 1 (mono) or 2 (stereo)");
            this.channels = channels;
            return this;
        }

        /**
         * Convert only part of the file: start at {@code startSeconds} and
         * take {@code durationSeconds} seconds (0 duration = to the end).
         */
        public Builder trim(double startSeconds, double durationSeconds) {
            if (startSeconds < 0)
                throw new IllegalArgumentException("startSeconds < 0");
            if (durationSeconds < 0)
                throw new IllegalArgumentException("durationSeconds < 0");
            this.startSeconds = startSeconds;
            this.durationSeconds = durationSeconds;
            return this;
        }

        /** Volume in percent: 100 = unchanged, 50 = half, 200 = double. */
        public Builder volumePercent(int percent) {
            if (percent < 1 || percent > 400)
                throw new IllegalArgumentException(
                        "volumePercent must be 1..400, was " + percent);
            this.volume = percent / 100.0;
            return this;
        }

        /** Volume change in decibels: 0 = unchanged, +6 ≈ double, -6 ≈ half. */
        public Builder volumeDb(double db) {
            if (db < -48 || db > 24)
                throw new IllegalArgumentException("volumeDb must be -48..24");
            this.volume = Math.pow(10.0, db / 20.0);
            return this;
        }

        /**
         * Which audio track to convert when the file has several (multi
         * language videos): 0 = first audio track, 1 = second, ...
         * Default -1 = the best / first track.
         */
        public Builder audioStream(int audioTrackOrdinal) {
            if (audioTrackOrdinal < -1)
                throw new IllegalArgumentException("audioStream must be >= -1");
            this.audioStream = audioTrackOrdinal;
            return this;
        }

        /**
         * Parallel encoding threads: 0 = auto (default - up to 4 chunks
         * encoded at once on separate CPU cores for conversions of
         * 2 minutes or longer), 1 = single-threaded, 2..8 = that many
         * chunks. Applies to CBR/ABR; VBR always encodes single-threaded
         * so its duration metadata stays exact. If a chunk fails, the
         * conversion automatically falls back to single-threaded.
         */
        public Builder threads(int threads) {
            if (threads < 0 || threads > 8)
                throw new IllegalArgumentException(
                        "threads must be 0 (auto) .. 8, was " + threads);
            this.threads = threads;
            return this;
        }

        /**
         * LAME algorithmic quality: -1 = LAME default (default), 0 = best
         * quality / slowest, 9 = fastest / lowest quality. Raising it
         * (e.g. 7) speeds up encoding at a small quality cost; it is
         * unrelated to vbrQuality.
         */
        public Builder encoderQuality(int quality) {
            if (quality < -1 || quality > 9)
                throw new IllegalArgumentException(
                        "encoderQuality must be -1..9, was " + quality);
            this.encoderQuality = quality;
            return this;
        }

        /** ID3 title tag written into the MP3. */
        public Builder title(String title) { this.title = title; return this; }
        /** ID3 artist tag written into the MP3. */
        public Builder artist(String artist) { this.artist = artist; return this; }
        /** ID3 album tag written into the MP3. */
        public Builder album(String album) { this.album = album; return this; }

        public Options build() {
            return new Options(this);
        }
    }
}
