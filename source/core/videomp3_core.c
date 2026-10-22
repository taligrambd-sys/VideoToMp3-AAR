/*
 * VideoToMp3 core implementation - (c) Kaalsoft
 * Takes the audio track of any supported video/audio file and encodes it to
 * MP3 with libmp3lame. Only the audio stream is decoded - the video stream is
 * never decoded, which keeps this fast and small.
 */
#include "videomp3_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>

static __thread char g_err[512];
static volatile int g_cancel = 0;

static void set_err(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof(g_err), fmt, ap);
    va_end(ap);
}

static void set_ff_err(const char *what, int ret)
{
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, buf, sizeof(buf));
    set_err("%s: %s", what, buf);
}

const char *vmp3_last_error(void) { return g_err; }

const char *vmp3_version(void)
{
    static char v[128];
    snprintf(v, sizeof(v), "FFmpeg %s / LAME 3.100 (libmp3lame)",
             av_version_info());
    return v;
}

void vmp3_request_cancel(void) { g_cancel = 1; }

/* LAME only supports these sample rates; pick the closest one. */
static int nearest_lame_rate(int rate)
{
    static const int rates[] = {8000, 11025, 12000, 16000, 22050,
                                24000, 32000, 44100, 48000};
    int i, best = 44100, best_diff = 1 << 30;
    if (rate <= 0)
        return 44100;
    for (i = 0; i < (int)(sizeof(rates) / sizeof(rates[0])); i++) {
        int d = abs(rates[i] - rate);
        if (d < best_diff) {
            best_diff = d;
            best = rates[i];
        }
    }
    return best;
}

typedef struct {
    AVCodecContext *enc;
    AVFormatContext *oc;
    AVStream *st;
    int io_opened;
} EncSink;

/* Write out all packets the encoder currently has ready. */
static int drain_packets(EncSink *s)
{
    AVPacket *pkt = av_packet_alloc();
    int ret;
    if (!pkt)
        return -1;
    while (1) {
        ret = avcodec_receive_packet(s->enc, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0) {
            av_packet_free(&pkt);
            set_ff_err("Encoding failed", ret);
            return -1;
        }
        av_packet_rescale_ts(pkt, s->enc->time_base, s->st->time_base);
        pkt->stream_index = s->st->index;
        ret = av_interleaved_write_frame(s->oc, pkt);
        av_packet_unref(pkt);
        if (ret < 0) {
            av_packet_free(&pkt);
            set_ff_err("Writing output failed", ret);
            return -1;
        }
    }
    av_packet_free(&pkt);
    return 0;
}

/* Send one frame (NULL = flush) to the encoder and write out all packets. */
static int encode_and_write(EncSink *s, AVFrame *frame)
{
    int ret = avcodec_send_frame(s->enc, frame);
    if (ret == AVERROR(EAGAIN)) {
        if (drain_packets(s) < 0)
            return -1;
        ret = avcodec_send_frame(s->enc, frame);
    }
    if (ret < 0 && ret != AVERROR_EOF) {
        set_ff_err("Encoder rejected a frame", ret);
        return -1;
    }
    return drain_packets(s);
}

/*
 * libmp3lame accepts only exact frame_size sample frames (1152, or 576 at
 * low sample rates) except for the very last one, while decoders hand out
 * arbitrary sizes (AAC: 1024, ...). All resampled audio therefore goes
 * through an AVAudioFifo and is fed to the encoder in exact chunks.
 */
typedef struct {
    EncSink sink;
    AVAudioFifo *fifo;
    AVFrame *eframe; /* reusable chunk frame */
    int frame_size;
    int64_t written; /* samples sent to the encoder (for pts) */
} FeedCtx;

static int fifo_drain(FeedCtx *f, int final)
{
    while (av_audio_fifo_size(f->fifo) >= f->frame_size ||
           (final && av_audio_fifo_size(f->fifo) > 0)) {
        int n = av_audio_fifo_size(f->fifo);
        AVCodecContext *enc = f->sink.enc;
        if (n > f->frame_size)
            n = f->frame_size;
        av_frame_unref(f->eframe);
        f->eframe->format = enc->sample_fmt;
        av_channel_layout_copy(&f->eframe->ch_layout, &enc->ch_layout);
        f->eframe->sample_rate = enc->sample_rate;
        f->eframe->nb_samples = n;
        if (av_frame_get_buffer(f->eframe, 0) < 0) {
            set_err("Out of memory");
            return -1;
        }
        if (av_audio_fifo_read(f->fifo, (void **)f->eframe->data, n) < n) {
            set_err("Audio buffer error");
            return -1;
        }
        f->eframe->pts = f->written;
        f->written += n;
        if (encode_and_write(&f->sink, f->eframe) < 0)
            return -1;
        if (n < f->frame_size)
            break;
    }
    return 0;
}

static int fifo_feed(FeedCtx *f, AVFrame *frame, int n)
{
    if (av_audio_fifo_realloc(f->fifo,
                              av_audio_fifo_size(f->fifo) + n) < 0) {
        set_err("Out of memory");
        return -1;
    }
    if (av_audio_fifo_write(f->fifo, (void **)frame->extended_data, n) < n) {
        set_err("Audio buffer error");
        return -1;
    }
    return fifo_drain(f, 0);
}

static int convert_range(const char *input, const char *output,
                         const Vmp3Options *opt, vmp3_progress_cb cb,
                         void *userdata)
{
    Vmp3Options o;
    AVFormatContext *ic = NULL, *oc = NULL;
    AVCodecContext *dec = NULL, *enc = NULL;
    SwrContext *swr = NULL;
    AVPacket *pkt = NULL;
    AVFrame *frame = NULL, *rframe = NULL;
    FeedCtx feed;
    int stream_idx = -1, ret = -1, output_created = 0;
    int64_t samples_written = 0;
    int last_pct = -1;
    double start, want_dur, total_dur;

    if (!opt) {
        memset(&o, 0, sizeof(o));
        o.mode = VMP3_MODE_CBR;
        o.bitrate_kbps = 192;
        o.vbr_quality = 2;
        o.volume = 1.0;
        o.audio_stream = -1;
        o.threads = 0;
        o.encoder_quality = -1;
    } else {
        o = *opt;
    }
    if (o.bitrate_kbps < 8)
        o.bitrate_kbps = 8;
    if (o.bitrate_kbps > 320)
        o.bitrate_kbps = 320;
    if (o.vbr_quality < 0)
        o.vbr_quality = 0;
    if (o.vbr_quality > 9)
        o.vbr_quality = 9;
    if (o.volume <= 0.0)
        o.volume = 1.0;
    start = o.start_sec < 0 ? 0 : o.start_sec;
    want_dur = o.duration_sec < 0 ? 0 : o.duration_sec;

    memset(&feed, 0, sizeof(feed));
    g_err[0] = '\0';

    if (!input || !output || !input[0] || !output[0]) {
        set_err("Input or output path is empty");
        return -1;
    }
    if (strcmp(input, output) == 0) {
        set_err("Input and output paths are the same");
        return -1;
    }

    ret = avformat_open_input(&ic, input, NULL, NULL);
    if (ret < 0) {
        set_ff_err("Cannot open the input file", ret);
        return -1;
    }
    ret = avformat_find_stream_info(ic, NULL);
    if (ret < 0) {
        set_ff_err("Cannot read the input file", ret);
        goto done;
    }

    /* Pick the requested audio track (ordinal), else the best audio track. */
    if (o.audio_stream >= 0) {
        int seen = 0;
        unsigned i;
        for (i = 0; i < ic->nb_streams; i++) {
            if (ic->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                if (seen == o.audio_stream) {
                    stream_idx = (int)i;
                    break;
                }
                seen++;
            }
        }
    }
    if (stream_idx < 0)
        stream_idx = av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (stream_idx < 0) {
        set_err("No audio track found in this file");
        ret = -1;
        goto done;
    }

    {
        AVStream *in_st = ic->streams[stream_idx];
        const AVCodec *decoder = avcodec_find_decoder(in_st->codecpar->codec_id);
        if (!decoder) {
            set_err("Unsupported audio codec in this file");
            ret = -1;
            goto done;
        }
        dec = avcodec_alloc_context3(decoder);
        if (!dec) {
            set_err("Out of memory");
            ret = -1;
            goto done;
        }
        ret = avcodec_parameters_to_context(dec, in_st->codecpar);
        if (ret < 0) {
            set_ff_err("Cannot configure the decoder", ret);
            goto done;
        }
        ret = avcodec_open2(dec, decoder, NULL);
        if (ret < 0) {
            set_ff_err("Cannot open the audio decoder", ret);
            goto done;
        }
    }

    /* ---- output / encoder setup ---- */
    {
        AVStream *in_st = ic->streams[stream_idx];
        const AVCodec *encoder;
        AVStream *out_st;
        EncSink sink;
        int in_rate, out_rate, in_ch, out_ch;
        AVChannelLayout in_layout;
        int i, fmt_ok = 0;

        in_rate = dec->sample_rate > 0 ? dec->sample_rate
                                       : in_st->codecpar->sample_rate;
        out_rate = o.sample_rate > 0 ? nearest_lame_rate(o.sample_rate)
                                     : nearest_lame_rate(in_rate);
        in_ch = dec->ch_layout.nb_channels > 0 ? dec->ch_layout.nb_channels : 2;
        out_ch = (o.channels == 1 || o.channels == 2)
                     ? o.channels
                     : (in_ch > 2 ? 2 : in_ch);
        if (out_ch < 1)
            out_ch = 1;

        ret = avformat_alloc_output_context2(&oc, NULL, "mp3", output);
        if (ret < 0 || !oc) {
            set_err("Cannot create the MP3 output");
            ret = -1;
            goto done;
        }
        encoder = avcodec_find_encoder_by_name("libmp3lame");
        if (!encoder) {
            set_err("MP3 encoder (libmp3lame) is missing");
            ret = -1;
            goto done;
        }
        out_st = avformat_new_stream(oc, NULL);
        enc = avcodec_alloc_context3(encoder);
        if (!out_st || !enc) {
            set_err("Out of memory");
            ret = -1;
            goto done;
        }
        enc->sample_rate = out_rate;
        av_channel_layout_default(&enc->ch_layout, out_ch);
        enc->sample_fmt = AV_SAMPLE_FMT_S16P;
        if (encoder->sample_fmts) {
            for (i = 0; encoder->sample_fmts[i] != -1; i++)
                if (encoder->sample_fmts[i] == AV_SAMPLE_FMT_S16P)
                    fmt_ok = 1;
            if (!fmt_ok)
                enc->sample_fmt = encoder->sample_fmts[0];
        }
        enc->time_base = (AVRational){1, out_rate};

        if (o.mode == VMP3_MODE_VBR) {
            /* libmp3lame only switches to VBR when QSCALE is set */
            enc->bit_rate = 0;
            enc->flags |= AV_CODEC_FLAG_QSCALE;
            enc->global_quality = FF_QP2LAMBDA * o.vbr_quality;
        } else {
            enc->bit_rate = (int64_t)o.bitrate_kbps * 1000;
            if (o.mode == VMP3_MODE_ABR)
                av_opt_set_int(enc->priv_data, "abr", 1,
                               AV_OPT_SEARCH_CHILDREN);
        }
        if (oc->oformat->flags & AVFMT_GLOBALHEADER)
            enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (o.encoder_quality >= 0 && o.encoder_quality <= 9)
            enc->compression_level = o.encoder_quality;

        ret = avcodec_open2(enc, encoder, NULL);
        if (ret < 0) {
            set_ff_err("Cannot open the MP3 encoder", ret);
            goto done;
        }
        ret = avcodec_parameters_from_context(out_st->codecpar, enc);
        if (ret < 0) {
            set_ff_err("Cannot configure the output stream", ret);
            goto done;
        }
        out_st->time_base = enc->time_base;

        if (o.title && o.title[0])
            av_dict_set(&oc->metadata, "title", o.title, 0);
        if (o.artist && o.artist[0])
            av_dict_set(&oc->metadata, "artist", o.artist, 0);
        if (o.album && o.album[0])
            av_dict_set(&oc->metadata, "album", o.album, 0);

        if (!(oc->oformat->flags & AVFMT_NOFILE)) {
            ret = avio_open(&oc->pb, output, AVIO_FLAG_WRITE);
            if (ret < 0) {
                set_ff_err("Cannot create the output file", ret);
                goto done;
            }
            output_created = 1;
        }
        ret = avformat_write_header(oc, NULL);
        if (ret < 0) {
            set_ff_err("Cannot write the MP3 header", ret);
            goto done;
        }

        /* ---- resampler ---- */
        memset(&in_layout, 0, sizeof(in_layout));
        if (dec->ch_layout.nb_channels > 0)
            av_channel_layout_copy(&in_layout, &dec->ch_layout);
        else
            av_channel_layout_default(&in_layout, in_ch);
        ret = swr_alloc_set_opts2(&swr, &enc->ch_layout, enc->sample_fmt,
                                  out_rate, &in_layout, dec->sample_fmt,
                                  dec->sample_rate, 0, NULL);
        av_channel_layout_uninit(&in_layout);
        if (ret < 0 || !swr) {
            set_err("Cannot create the audio resampler");
            ret = -1;
            goto done;
        }
        ret = swr_init(swr);
        if (ret < 0) {
            set_ff_err("Cannot start the audio resampler", ret);
            goto done;
        }

        /* progress denominator: the trimmed window */
        if (want_dur > 0)
            total_dur = want_dur;
        else if (ic->duration > 0)
            total_dur = ic->duration / (double)AV_TIME_BASE - start;
        else
            total_dur = 0;
        if (total_dur < 0)
            total_dur = 0;

        /* ---- trim: seek near the start point ---- */
        if (start > 0.05) {
            int64_t ts = (int64_t)(start * AV_TIME_BASE);
            if (avformat_seek_file(ic, -1, INT64_MIN, ts, ts, 0) >= 0)
                avcodec_flush_buffers(dec);
        }

        pkt = av_packet_alloc();
        frame = av_frame_alloc();
        rframe = av_frame_alloc();
        if (!pkt || !frame || !rframe) {
            set_err("Out of memory");
            ret = -1;
            goto done;
        }
        sink.enc = enc;
        sink.oc = oc;
        sink.st = out_st;
        sink.io_opened = output_created;
        feed.sink = sink;
        feed.frame_size = enc->frame_size > 0 ? enc->frame_size : 1152;
        feed.written = 0;
        feed.eframe = av_frame_alloc();
        feed.fifo = av_audio_fifo_alloc(enc->sample_fmt, out_ch,
                                        feed.frame_size * 2);
        if (!feed.eframe || !feed.fifo) {
            set_err("Out of memory");
            ret = -1;
            goto done;
        }
        ret = 0;

        /* ---- decode -> resample -> encode loop ---- */
        {
            int trim_done = 0;
            while (ret == 0 && !trim_done && av_read_frame(ic, pkt) >= 0) {
            double sec = -1;
            if (g_cancel) {
                set_err("Conversion cancelled");
                ret = -1;
                break;
            }
            if (pkt->stream_index != stream_idx) {
                av_packet_unref(pkt);
                continue;
            }
            if (pkt->pts != AV_NOPTS_VALUE)
                sec = pkt->pts * av_q2d(in_st->time_base);
            if (sec >= 0 && cb && total_dur > 0) {
                int pct = (int)((sec - start) / total_dur * 100.0);
                if (pct < 0)
                    pct = 0;
                if (pct > 99)
                    pct = 99;
                if (pct != last_pct) {
                    last_pct = pct;
                    cb(pct, userdata);
                }
            }
            if (want_dur > 0 && sec >= 0 && sec > start + want_dur + 2.0) {
                av_packet_unref(pkt);
                break; /* safely past the trim end */
            }
            ret = avcodec_send_packet(dec, pkt);
            av_packet_unref(pkt);
            if (ret < 0 && ret != AVERROR_INVALIDDATA)
                ret = 0; /* skip a damaged packet, keep going */
            if (ret < 0)
                ret = 0;
            while (avcodec_receive_frame(dec, frame) == 0) {
                double fsec = -1, fend;
                int max_out, n;
                if (frame->best_effort_timestamp != AV_NOPTS_VALUE)
                    fsec = frame->best_effort_timestamp *
                           av_q2d(in_st->time_base);
                fend = (fsec >= 0 && dec->sample_rate > 0)
                           ? fsec + frame->nb_samples /
                                        (double)dec->sample_rate
                           : fsec;
                if (fsec >= 0 && fend <= start) {
                    av_frame_unref(frame);
                    continue; /* before the trim start */
                }
                if (want_dur > 0 && fsec >= 0 &&
                    fsec >= start + want_dur) {
                    av_frame_unref(frame);
                    trim_done = 1; /* reached the trim end */
                    break;
                }
                max_out = (int)av_rescale_rnd(
                              swr_get_delay(swr, dec->sample_rate) +
                                  frame->nb_samples,
                              out_rate, dec->sample_rate, AV_ROUND_UP) +
                          1024;
                rframe->format = enc->sample_fmt;
                av_channel_layout_copy(&rframe->ch_layout, &enc->ch_layout);
                rframe->sample_rate = out_rate;
                rframe->nb_samples = max_out;
                if (av_frame_get_buffer(rframe, 0) < 0) {
                    av_frame_unref(frame);
                    set_err("Out of memory");
                    ret = -1;
                    break;
                }
                n = swr_convert(swr, rframe->data, max_out,
                                (const uint8_t **)frame->data,
                                frame->nb_samples);
                av_frame_unref(frame);
                if (n < 0) {
                    av_frame_unref(rframe);
                    set_err("Audio resampling failed");
                    ret = -1;
                    break;
                }
                if (n == 0) {
                    av_frame_unref(rframe);
                    continue;
                }
                rframe->nb_samples = n;
                if (o.volume != 1.0 &&
                    enc->sample_fmt == AV_SAMPLE_FMT_S16P) {
                    int c, i2;
                    for (c = 0; c < out_ch; c++) {
                        int16_t *s = (int16_t *)rframe->data[c];
                        for (i2 = 0; i2 < n; i2++) {
                            double v = s[i2] * o.volume;
                            if (v > 32767.0)
                                v = 32767.0;
                            if (v < -32768.0)
                                v = -32768.0;
                            s[i2] = (int16_t)lrint(v);
                        }
                    }
                }
                samples_written += n;
                if (fifo_feed(&feed, rframe, n) < 0) {
                    av_frame_unref(rframe);
                    ret = -1;
                    break;
                }
                av_frame_unref(rframe);
            }
            if (ret < 0)
                break;
            } /* while read_frame */
        }

        /* ---- flush decoder, resampler, encoder ---- */
        if (ret == 0) {
            avcodec_send_packet(dec, NULL);
            while (ret == 0 && avcodec_receive_frame(dec, frame) == 0) {
                int max_out, n;
                max_out = (int)av_rescale_rnd(
                              swr_get_delay(swr, dec->sample_rate) +
                                  frame->nb_samples,
                              out_rate, dec->sample_rate, AV_ROUND_UP) +
                          1024;
                rframe->format = enc->sample_fmt;
                av_channel_layout_copy(&rframe->ch_layout, &enc->ch_layout);
                rframe->sample_rate = out_rate;
                rframe->nb_samples = max_out;
                if (av_frame_get_buffer(rframe, 0) >= 0) {
                    n = swr_convert(swr, rframe->data, max_out,
                                    (const uint8_t **)frame->data,
                                    frame->nb_samples);
                    if (n > 0) {
                        rframe->nb_samples = n;
                        samples_written += n;
                        if (fifo_feed(&feed, rframe, n) < 0)
                            ret = -1;
                    }
                }
                av_frame_unref(rframe);
                av_frame_unref(frame);
            }
        }
        if (ret == 0) {
            /* drain samples still held inside the resampler */
            while (1) {
                int n;
                rframe->format = enc->sample_fmt;
                av_channel_layout_copy(&rframe->ch_layout, &enc->ch_layout);
                rframe->sample_rate = out_rate;
                rframe->nb_samples = 8192;
                if (av_frame_get_buffer(rframe, 0) < 0)
                    break;
                n = swr_convert(swr, rframe->data, 8192, NULL, 0);
                if (n <= 0) {
                    av_frame_unref(rframe);
                    break;
                }
                rframe->nb_samples = n;
                samples_written += n;
                if (fifo_feed(&feed, rframe, n) < 0) {
                    ret = -1;
                    av_frame_unref(rframe);
                    break;
                }
                av_frame_unref(rframe);
            }
        }
        if (ret == 0 && fifo_drain(&feed, 1) < 0)
            ret = -1;
        if (ret == 0 && encode_and_write(&feed.sink, NULL) < 0)
            ret = -1;
        if (ret == 0) {
            if (samples_written == 0) {
                set_err("No audio could be decoded from this file");
                ret = -1;
            } else {
                av_write_trailer(oc);
                if (cb)
                    cb(100, userdata);
            }
        }
    }

done:
    if (feed.fifo)
        av_audio_fifo_free(feed.fifo);
    if (feed.eframe)
        av_frame_free(&feed.eframe);
    if (pkt)
        av_packet_free(&pkt);
    if (frame)
        av_frame_free(&frame);
    if (rframe)
        av_frame_free(&rframe);
    if (swr)
        swr_free(&swr);
    if (dec)
        avcodec_free_context(&dec);
    if (enc)
        avcodec_free_context(&enc);
    if (oc) {
        if (output_created && oc->pb)
            avio_closep(&oc->pb);
        avformat_free_context(oc);
    }
    if (ic)
        avformat_close_input(&ic);
    if (ret != 0 && output_created)
        remove(output);
    return ret;
}


/* ================= parallel chunk engine (v2) =================
 * Long conversions are split into time ranges that are encoded
 * concurrently (one convert_range per thread) and concatenated.
 * Only used for CBR/ABR - a VBR file needs one global Xing header to
 * report a correct duration, so VBR keeps the single path. Any chunk
 * failure falls back to the proven single path. MP3 frames are
 * self-contained, so plain concatenation plays everywhere; the only
 * clean-up needed is dropping each chunk's own ID3 tag (all but the
 * first) and its empty first Xing/Info frame. */

/* Total size of an ID3v2 tag at the start of buf, or 0. */
static long id3v2_size(const unsigned char *b, long len)
{
    long sz;
    if (len < 10 || memcmp(b, "ID3", 3) != 0)
        return 0;
    sz = ((long)(b[6] & 0x7F) << 21) | ((long)(b[7] & 0x7F) << 14) |
         ((long)(b[8] & 0x7F) << 7) | (long)(b[9] & 0x7F);
    sz += 10;
    if (b[5] & 0x10) /* footer present */
        sz += 10;
    return sz;
}

/* Length of the MPEG Layer III frame whose 4-byte header is at h, or 0. */
static long mp3_frame_len(const unsigned char *h)
{
    static const int br_mpeg1[15] = {0, 32, 40, 48, 56, 64, 80, 96,
                                     112, 128, 160, 192, 224, 256, 320};
    static const int br_mpeg2[15] = {0, 8, 16, 24, 32, 40, 48, 56,
                                     64, 80, 96, 112, 128, 144, 160};
    static const int sr_tab[3][3] = {{44100, 48000, 32000},
                                     {22050, 24000, 16000},
                                     {11025, 12000, 8000}};
    int ver, bri, sri, pad, br, sr;
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0)
        return 0;
    ver = (h[1] >> 3) & 3; /* 3=MPEG1, 2=MPEG2, 0=MPEG2.5 */
    if (ver == 1 || ((h[1] >> 1) & 3) != 1) /* reserved / not Layer III */
        return 0;
    bri = (h[2] >> 4) & 0xF;
    sri = (h[2] >> 2) & 3;
    pad = (h[2] >> 1) & 1;
    if (bri == 0 || bri == 15 || sri == 3)
        return 0;
    if (ver == 3) {
        br = br_mpeg1[bri];
        sr = sr_tab[0][sri];
        return (144000L * br) / sr + pad;
    }
    br = br_mpeg2[bri];
    sr = sr_tab[ver == 2 ? 1 : 2][sri];
    return (72000L * br) / sr + pad;
}

/* 1 if the frame at path offset `off` is a Xing/Info header frame. */
static int frame_is_xing(const char *path, long off, long frame_len)
{
    FILE *f = fopen(path, "rb");
    unsigned char buf[512];
    long n, i;
    int found = 0;
    if (!f)
        return 0;
    if (frame_len > (long)sizeof(buf))
        frame_len = sizeof(buf);
    if (fseek(f, off, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    n = (long)fread(buf, 1, frame_len, f);
    fclose(f);
    for (i = 0; i + 4 <= n; i++) {
        if (memcmp(buf + i, "Xing", 4) == 0 ||
            memcmp(buf + i, "Info", 4) == 0) {
            found = 1;
            break;
        }
    }
    return found;
}

/* Copy file `path` into `out`, optionally keeping its ID3v2 tag, and
 * skipping a leading Xing/Info frame. Returns 0 on success. */
static int concat_chunk(FILE *out, const char *path, int keep_id3)
{
    FILE *f = fopen(path, "rb");
    unsigned char head[10];
    unsigned char buf[65536];
    long id3 = 0, data_start, n;
    size_t r;
    if (!f)
        return -1;
    if (fread(head, 1, 10, f) == 10)
        id3 = id3v2_size(head, 10);
    data_start = id3;
    /* drop a leading Xing/Info frame (empty info frame, no real audio) */
    if (fseek(f, id3, SEEK_SET) == 0 &&
        fread(buf, 1, 4, f) == 4) {
        long fl = mp3_frame_len(buf);
        if (fl > 4 && frame_is_xing(path, id3, fl))
            data_start = id3 + fl;
    }
    if (keep_id3 && id3 > 0) {
        if (fseek(f, 0, SEEK_SET) != 0)
            goto fail;
        n = id3;
        while (n > 0) {
            r = fread(buf, 1, n > (long)sizeof(buf) ? sizeof(buf) : (size_t)n, f);
            if (r == 0 || fwrite(buf, 1, r, out) != r)
                goto fail;
            n -= (long)r;
        }
    }
    if (fseek(f, data_start, SEEK_SET) != 0)
        goto fail;
    while ((r = fread(buf, 1, sizeof(buf), f)) > 0)
        if (fwrite(buf, 1, r, out) != r)
            goto fail;
    fclose(f);
    return 0;
fail:
    fclose(f);
    return -1;
}


typedef struct ParallelCtx ParallelCtx;
typedef struct {
    const char *input;
    char tmpath[1100];
    Vmp3Options o;
    double start; /* window start of this chunk (seconds, absolute) */
    double len;   /* nominal chunk length */
    int pct;      /* this chunk's own last reported progress (0..100) */
    int rc;
    char err[512];
    ParallelCtx *shared;
} ChunkJob;

struct ParallelCtx {
    vmp3_progress_cb cb;
    void *ud;
    pthread_mutex_t mu;
    int last_pct;
    double total; /* window length being converted */
    ChunkJob *jobs;
    int njobs;
};

static void chunk_progress(int pct, void *ud)
{
    ChunkJob *job = (ChunkJob *)ud;
    ParallelCtx *sh = job->shared;
    int g, i;
    double done = 0;
    if (!sh || !sh->cb || sh->total <= 0)
        return;
    /* Overall progress = the amount of the window actually finished,
     * summed over all chunks. (v2.0 reported the furthest chunk
     * POSITION instead - with 4 chunks the bar jumped to ~75-80% the
     * moment the last chunk produced its first frames.) */
    pthread_mutex_lock(&sh->mu);
    job->pct = pct;
    for (i = 0; i < sh->njobs; i++)
        done += sh->jobs[i].len * sh->jobs[i].pct / 100.0;
    g = (int)(done / sh->total * 100.0);
    if (g < 0)
        g = 0;
    if (g > 99)
        g = 99;
    if (g > sh->last_pct) {
        sh->last_pct = g;
    } else {
        g = -1;
    }
    pthread_mutex_unlock(&sh->mu);
    if (g >= 0)
        sh->cb(g, sh->ud); /* outside the lock: this reaches Java code */
}

static void *chunk_worker(void *arg)
{
    ChunkJob *job = (ChunkJob *)arg;
    job->rc = convert_range(job->input, job->tmpath, &job->o,
                            chunk_progress, job);
    if (job->rc != 0)
        snprintf(job->err, sizeof(job->err), "%s", vmp3_last_error());
    return NULL;
}

int vmp3_convert(const char *input, const char *output, const Vmp3Options *opt,
                 vmp3_progress_cb cb, void *userdata)
{
    Vmp3Options o;
    Vmp3Info info;
    double win_start, win_len, total_dur, chunk_len;
    int want, chunks, i, created = 0, failed = 0;
    ChunkJob jobs[8];
    pthread_t tids[8];
    ParallelCtx shared;
    FILE *out = NULL;

    g_err[0] = '\0';
    g_cancel = 0;

    if (!opt) {
        /* let convert_range apply its own defaults */
        return convert_range(input, output, NULL, cb, userdata);
    }
    o = *opt;

    /* Parallel is only for CBR/ABR and explicit single-thread requests
     * stay single. */
    if (o.threads == 1 || o.mode == VMP3_MODE_VBR)
        return convert_range(input, output, &o, cb, userdata);
    if (vmp3_probe(input, &info) != 0 || info.duration_sec <= 0)
        return convert_range(input, output, &o, cb, userdata);
    total_dur = info.duration_sec;
    win_start = o.start_sec < 0 ? 0 : o.start_sec;
    if (win_start >= total_dur)
        return convert_range(input, output, &o, cb, userdata);
    win_len = o.duration_sec > 0 ? o.duration_sec : total_dur - win_start;
    if (win_len > total_dur - win_start)
        win_len = total_dur - win_start;

    if (o.threads > 1) {
        want = o.threads;
        if (win_len < 30.0)
            return convert_range(input, output, &o, cb, userdata);
    } else {
        long cores = sysconf(_SC_NPROCESSORS_ONLN);
        if (cores < 2)
            cores = 2;
        want = cores > 4 ? 4 : (int)cores;
        if (win_len < 120.0)
            return convert_range(input, output, &o, cb, userdata);
    }
    if (want > 8)
        want = 8;
    chunks = want;
    while (chunks > 1 && win_len / chunks < 30.0)
        chunks--;
    if (chunks < 2)
        return convert_range(input, output, &o, cb, userdata);
    if (!output || strlen(output) > 1000)
        return convert_range(input, output, &o, cb, userdata);

    chunk_len = win_len / chunks;
    memset(&shared, 0, sizeof(shared));
    shared.cb = cb;
    shared.ud = userdata;
    shared.total = win_len;
    shared.last_pct = -1;
    shared.jobs = jobs;
    shared.njobs = chunks;
    pthread_mutex_init(&shared.mu, NULL);

    for (i = 0; i < chunks; i++) {
        ChunkJob *j = &jobs[i];
        memset(j, 0, sizeof(*j));
        j->input = input;
        snprintf(j->tmpath, sizeof(j->tmpath), "%s.vmp3part%d", output, i);
        j->o = o;
        j->o.threads = 1; /* no recursion */
        j->o.start_sec = win_start + i * chunk_len;
        j->o.duration_sec = (i == chunks - 1)
                                ? (o.duration_sec > 0
                                       ? win_start + win_len - j->o.start_sec
                                       : 0)
                                : chunk_len;
        if (i > 0) { /* ID3 tags only on the first chunk */
            j->o.title = NULL;
            j->o.artist = NULL;
            j->o.album = NULL;
        }
        j->start = i * chunk_len;
        j->len = chunk_len;
        j->shared = &shared;
        j->rc = -99;
    }

    {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);
        for (i = 1; i < chunks; i++) {
            if (pthread_create(&tids[i], &attr, chunk_worker, &jobs[i]) !=
                0) {
                jobs[i].rc = -1;
                snprintf(jobs[i].err, sizeof(jobs[i].err),
                         "Cannot start a worker thread");
                created = i - 1;
                break;
            }
            created = i;
        }
        pthread_attr_destroy(&attr);
    }
    /* chunk 0 runs on the calling thread */
    jobs[0].rc = convert_range(input, jobs[0].tmpath, &jobs[0].o,
                               chunk_progress, &jobs[0]);
    if (jobs[0].rc != 0)
        snprintf(jobs[0].err, sizeof(jobs[0].err), "%s", vmp3_last_error());
    for (i = 1; i <= created; i++)
        pthread_join(tids[i], NULL);
    pthread_mutex_destroy(&shared.mu);

    for (i = 0; i < chunks; i++)
        if (jobs[i].rc != 0)
            failed = 1;

    if (!failed) {
        out = fopen(output, "wb");
        if (!out)
            failed = 1;
        else {
            for (i = 0; i < chunks && !failed; i++)
                if (concat_chunk(out, jobs[i].tmpath, i == 0) != 0)
                    failed = 1;
            if (fclose(out) != 0)
                failed = 1;
            out = NULL;
            if (failed)
                remove(output);
        }
    }
    for (i = 0; i < chunks; i++)
        remove(jobs[i].tmpath);

    if (failed) {
        /* Proven fallback: the plain single-threaded conversion. */
        return convert_range(input, output, &o, cb, userdata);
    }
    if (cb)
        cb(100, userdata);
    return 0;
}

int vmp3_probe(const char *input, Vmp3Info *info)
{
    AVFormatContext *ic = NULL;
    int idx, ret;

    if (!info)
        return -1;
    memset(info, 0, sizeof(*info));
    info->duration_sec = -1;
    g_err[0] = '\0';
    if (!input || !input[0]) {
        set_err("Input path is empty");
        return -1;
    }
    ret = avformat_open_input(&ic, input, NULL, NULL);
    if (ret < 0) {
        set_ff_err("Cannot open the input file", ret);
        return -1;
    }
    if (avformat_find_stream_info(ic, NULL) < 0) {
        set_err("Cannot read the input file");
        avformat_close_input(&ic);
        return -1;
    }
    if (ic->duration > 0)
        info->duration_sec = ic->duration / (double)AV_TIME_BASE;
    if (ic->iformat && ic->iformat->name)
        snprintf(info->format, sizeof(info->format), "%s", ic->iformat->name);
    {
        unsigned i;
        for (i = 0; i < ic->nb_streams; i++)
            if (ic->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                info->has_video = 1;
    }
    idx = av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (idx < 0) {
        set_err("No audio track found in this file");
        avformat_close_input(&ic);
        return -1;
    }
    {
        AVCodecParameters *par = ic->streams[idx]->codecpar;
        const AVCodecDescriptor *d = avcodec_descriptor_get(par->codec_id);
        if (d && d->name)
            snprintf(info->codec, sizeof(info->codec), "%s", d->name);
        info->sample_rate = par->sample_rate;
        info->channels = par->ch_layout.nb_channels;
        info->bit_rate = par->bit_rate ? par->bit_rate : ic->bit_rate;
        if (info->duration_sec < 0 && ic->streams[idx]->duration > 0)
            info->duration_sec =
                ic->streams[idx]->duration *
                av_q2d(ic->streams[idx]->time_base);
    }
    avformat_close_input(&ic);
    return 0;
}
