/* Host test harness for videomp3_core (not shipped in the AAR). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "videomp3_core.h"

static void prog(int pct, void *ud)
{
    (void)ud;
    printf("PROGRESS %d\n", pct);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "version") == 0) {
        printf("VERSION %s\n", vmp3_version());
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "probe") == 0) {
        Vmp3Info info;
        if (vmp3_probe(argv[2], &info) != 0) {
            printf("PROBE_FAIL %s\n", vmp3_last_error());
            return 1;
        }
        printf("PROBE duration=%.3f codec=%s rate=%d ch=%d br=%lld video=%d fmt=%s\n",
               info.duration_sec, info.codec, info.sample_rate, info.channels,
               (long long)info.bit_rate, info.has_video, info.format);
        return 0;
    }
    if (argc >= 13 && strcmp(argv[1], "convert") == 0) {
        Vmp3Options o;
        int rc;
        memset(&o, 0, sizeof(o));
        o.mode = atoi(argv[4]);
        o.bitrate_kbps = atoi(argv[5]);
        o.vbr_quality = atoi(argv[6]);
        o.sample_rate = atoi(argv[7]);
        o.channels = atoi(argv[8]);
        o.start_sec = atof(argv[9]);
        o.duration_sec = atof(argv[10]);
        o.volume = atof(argv[11]);
        o.audio_stream = atoi(argv[12]);
        o.title = argc > 13 ? argv[13] : NULL;
        o.artist = argc > 14 ? argv[14] : NULL;
        o.album = argc > 15 ? argv[15] : NULL;
        o.threads = argc > 16 ? atoi(argv[16]) : 0;
        o.encoder_quality = argc > 17 ? atoi(argv[17]) : -1;
        rc = vmp3_convert(argv[2], argv[3], &o, prog, NULL);
        if (rc != 0) {
            printf("CONVERT_FAIL %s\n", vmp3_last_error());
            return 1;
        }
        printf("CONVERT_OK\n");
        return 0;
    }
    fprintf(stderr,
            "usage: %s version | probe <file> | convert <in> <out> <mode> "
            "<br> <vbrq> <sr> <ch> <start> <dur> <vol> <stream> [title] "
            "[artist] [album]\n",
            argv[0]);
    return 2;
}
