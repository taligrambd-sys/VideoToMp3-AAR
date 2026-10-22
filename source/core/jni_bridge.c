/*
 * JNI bridge for com.kaalsoft.videomp3.VideoToMp3 - (c) Kaalsoft
 * Thin wrapper over videomp3_core.c. Native library name: "videomp3".
 */
#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "videomp3_core.h"

/*
 * Progress callbacks can arrive from ANY thread (the parallel engine
 * encodes chunks on worker threads). A JNIEnv belongs to one thread
 * only, and a local jobject reference is meaningless on other threads -
 * using either across threads is what crashed big conversions in v2.0.
 * So this context carries the JavaVM and a GLOBAL reference, and every
 * callback resolves/attaches its own thread's JNIEnv.
 */
typedef struct {
    JavaVM *vm;
    jobject listener; /* GLOBAL ref to the ProgressListener, or NULL */
    jmethodID mid;    /* onProgress(I)V */
} ProgressCtx;

static void progress_cb(int percent, void *userdata)
{
    ProgressCtx *ctx = (ProgressCtx *)userdata;
    JNIEnv *env = NULL;
    int attached = 0;
    if (!ctx || !ctx->vm || !ctx->listener || !ctx->mid)
        return;
    if ((*ctx->vm)->GetEnv(ctx->vm, (void **)&env, JNI_VERSION_1_6) !=
            JNI_OK ||
        !env) {
        if ((*ctx->vm)->AttachCurrentThread(ctx->vm, (void **)&env, NULL) != JNI_OK ||
            !env)
            return;
        attached = 1;
    }
    (*env)->CallVoidMethod(env, ctx->listener, ctx->mid, (jint)percent);
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
    if (attached)
        (*ctx->vm)->DetachCurrentThread(ctx->vm);
}

static const char *jstr(JNIEnv *env, jstring s, const char **out)
{
    *out = NULL;
    if (!s)
        return NULL;
    const char *p = (*env)->GetStringUTFChars(env, s, NULL);
    *out = p;
    return p;
}

static void jstr_release(JNIEnv *env, jstring s, const char *p)
{
    if (s && p)
        (*env)->ReleaseStringUTFChars(env, s, p);
}

JNIEXPORT jboolean JNICALL
Java_com_kaalsoft_videomp3_VideoToMp3_nativeConvert(
    JNIEnv *env, jclass clazz, jstring jIn, jstring jOut, jint mode,
    jint bitrateKbps, jint vbrQuality, jint sampleRate, jint channels,
    jdouble startSec, jdouble durationSec, jdouble volume, jint audioStream,
    jstring jTitle, jstring jArtist, jstring jAlbum, jint threads,
    jint encoderQuality, jobject listener)
{
    const char *in, *out, *title, *artist, *album;
    Vmp3Options opt;
    ProgressCtx ctx;
    jclass lclass = NULL;
    int rc;

    (void)clazz;
    if (!jIn || !jOut)
        return JNI_FALSE;
    jstr(env, jIn, &in);
    jstr(env, jOut, &out);
    jstr(env, jTitle, &title);
    jstr(env, jArtist, &artist);
    jstr(env, jAlbum, &album);
    if (!in || !out) {
        jstr_release(env, jIn, in);
        jstr_release(env, jOut, out);
        jstr_release(env, jTitle, title);
        jstr_release(env, jArtist, artist);
        jstr_release(env, jAlbum, album);
        return JNI_FALSE;
    }

    memset(&opt, 0, sizeof(opt));
    opt.mode = mode;
    opt.bitrate_kbps = bitrateKbps;
    opt.vbr_quality = vbrQuality;
    opt.sample_rate = sampleRate;
    opt.channels = channels;
    opt.start_sec = startSec;
    opt.duration_sec = durationSec;
    opt.volume = volume;
    opt.audio_stream = audioStream;
    opt.title = title;
    opt.artist = artist;
    opt.album = album;
    opt.threads = threads;
    opt.encoder_quality = encoderQuality;

    memset(&ctx, 0, sizeof(ctx));
    if (listener) {
        lclass = (*env)->GetObjectClass(env, listener);
        if (lclass) {
            ctx.mid = (*env)->GetMethodID(env, lclass, "onProgress", "(I)V");
            (*env)->DeleteLocalRef(env, lclass);
        }
        if (ctx.mid &&
            (*env)->GetJavaVM(env, &ctx.vm) == JNI_OK) {
            ctx.listener = (*env)->NewGlobalRef(env, listener);
            if (!ctx.listener)
                ctx.vm = NULL;
        }
    }

    rc = vmp3_convert(in, out, &opt, ctx.listener ? progress_cb : NULL,
                      &ctx);
    if (ctx.listener)
        (*env)->DeleteGlobalRef(env, ctx.listener);

    jstr_release(env, jIn, in);
    jstr_release(env, jOut, out);
    jstr_release(env, jTitle, title);
    jstr_release(env, jArtist, artist);
    jstr_release(env, jAlbum, album);
    return rc == 0 ? JNI_TRUE : JNI_FALSE;
}

/* Returns "duration|codec|sampleRate|channels|bitRate|hasVideo|format"
 * or NULL on failure (see nativeLastError). */
JNIEXPORT jstring JNICALL
Java_com_kaalsoft_videomp3_VideoToMp3_nativeProbe(JNIEnv *env, jclass clazz,
                                                  jstring jIn)
{
    const char *in;
    Vmp3Info info;
    char buf[256];
    (void)clazz;
    if (!jIn)
        return NULL;
    jstr(env, jIn, &in);
    if (!in)
        return NULL;
    if (vmp3_probe(in, &info) != 0) {
        jstr_release(env, jIn, in);
        return NULL;
    }
    jstr_release(env, jIn, in);
    snprintf(buf, sizeof(buf), "%.3f|%s|%d|%d|%lld|%d|%s", info.duration_sec,
             info.codec, info.sample_rate, info.channels,
             (long long)info.bit_rate, info.has_video, info.format);
    return (*env)->NewStringUTF(env, buf);
}

JNIEXPORT jstring JNICALL
Java_com_kaalsoft_videomp3_VideoToMp3_nativeVersion(JNIEnv *env, jclass clazz)
{
    (void)clazz;
    return (*env)->NewStringUTF(env, vmp3_version());
}

JNIEXPORT jstring JNICALL
Java_com_kaalsoft_videomp3_VideoToMp3_nativeLastError(JNIEnv *env,
                                                       jclass clazz)
{
    (void)clazz;
    return (*env)->NewStringUTF(env, vmp3_last_error());
}

JNIEXPORT void JNICALL
Java_com_kaalsoft_videomp3_VideoToMp3_nativeCancel(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;
    vmp3_request_cancel();
}
