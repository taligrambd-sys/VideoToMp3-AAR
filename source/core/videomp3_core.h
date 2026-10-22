/*
 * VideoToMp3 core - (c) Kaalsoft
 * Plain-C conversion core (no JNI / no Android dependency) so the exact same
 * code can be host-tested and linked into the Android JNI bridge.
 * Engine: FFmpeg 6.1.2 (trimmed) + LAME 3.100 via libmp3lame. LGPL.
 */
#ifndef VIDEOMP3_CORE_H
#define VIDEOMP3_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*vmp3_progress_cb)(int percent, void *userdata);

/* Bitrate modes */
#define VMP3_MODE_CBR 0
#define VMP3_MODE_ABR 1
#define VMP3_MODE_VBR 2

typedef struct {
    int mode;             /* VMP3_MODE_* */
    int bitrate_kbps;     /* CBR / ABR target, 8..320 */
    int vbr_quality;      /* VBR: 0 = best quality .. 9 = smallest file */
    int sample_rate;      /* 0 = keep source rate (nearest LAME-supported) */
    int channels;         /* 0 = keep (capped at stereo), 1 = mono, 2 = stereo */
    double start_sec;     /* trim start, 0 = from beginning */
    double duration_sec;  /* trim length, 0 = to the end */
    double volume;        /* linear gain, 1.0 = unchanged */
    int audio_stream;     /* audio-track ordinal: 0 = first audio track,
                             1 = second, ... ; -1 = best/first */
    const char *title;    /* ID3 tags, NULL / "" = skip */
    const char *artist;
    const char *album;
    int threads;          /* parallel chunk encoding: 0 = auto (up to 4,
                             by CPU cores and duration), 1 = single-threaded,
                             2..8 = that many chunks. CBR/ABR only; any
                             chunk failure falls back to the single path. */
    int encoder_quality;  /* LAME algorithmic quality 0 (best/slow) .. 9
                             (fastest); -1 = LAME default. Trades a little
                             quality for speed when raised. */
} Vmp3Options;

typedef struct {
    double duration_sec;  /* -1 if unknown */
    char codec[32];       /* audio codec name, e.g. "aac" */
    int sample_rate;
    int channels;
    int64_t bit_rate;     /* bits per second, 0 if unknown */
    int has_video;        /* 1 if the file also has a video stream */
    char format[32];      /* container name, e.g. "mov,mp4,..." */
} Vmp3Info;

/* Returns 0 on success, < 0 on failure. On failure vmp3_last_error() tells why
 * and any partial output file is removed. Blocking - call from a worker
 * thread. Progress callback receives 0..100. */
int vmp3_convert(const char *input, const char *output, const Vmp3Options *opt,
                 vmp3_progress_cb cb, void *userdata);

/* Returns 0 on success and fills *info. */
int vmp3_probe(const char *input, Vmp3Info *info);

const char *vmp3_last_error(void);
const char *vmp3_version(void);

/* Ask a running conversion (on any thread) to stop. Thread-safe flag. */
void vmp3_request_cancel(void);

#ifdef __cplusplus
}
#endif
#endif /* VIDEOMP3_CORE_H */
