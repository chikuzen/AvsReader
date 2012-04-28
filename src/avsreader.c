/*

AviSynth Script Reader for AviUtl version 0.0.2

Copyright (c) 2012 Oka Motofumi (chikuzen.mo at gmail dot com)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <stdlib.h>
#include <windows.h>

#define AVSC_NO_DECLSPEC
#undef EXTERN_C
#include "avisynth_c.h"
#include "input.h"

INPUT_PLUGIN_TABLE input_plugin_table = {
    INPUT_PLUGIN_FLAG_VIDEO | INPUT_PLUGIN_FLAG_AUDIO,
    "AviSynth Script Reader",
    "AviSynth Script (*.avs)\0*.avs\0",
    "AviSynth Script Reader version 0.0.2 by Chikuzen",
    NULL,
    NULL,
    func_open,
    func_close,
    func_info_get,
    func_read_video,
    func_read_audio,
    NULL,
    NULL,
};

EXTERN_C INPUT_PLUGIN_TABLE __declspec(dllexport) * __stdcall GetInputPluginTable( void )
{
    return &input_plugin_table;
}

#define AVSC_DECLARE_FUNC(name) name##_func name
#define AVS_INTERFACE_25 2
typedef struct {
    float version;
    AVS_Clip *clip;
    AVS_ScriptEnvironment *env;
    const AVS_VideoInfo *vi;
    BITMAPINFOHEADER *vfmt;
    WAVEFORMATEX *afmt;
    HMODULE library;
    struct {
        AVSC_DECLARE_FUNC(avs_clip_get_error);
        AVSC_DECLARE_FUNC(avs_create_script_environment);
        AVSC_DECLARE_FUNC(avs_delete_script_environment);
        AVSC_DECLARE_FUNC(avs_get_error);
        AVSC_DECLARE_FUNC(avs_get_frame);
        AVSC_DECLARE_FUNC(avs_get_audio);
        AVSC_DECLARE_FUNC(avs_get_video_info);
        AVSC_DECLARE_FUNC(avs_invoke);
        AVSC_DECLARE_FUNC(avs_bit_blt);
        AVSC_DECLARE_FUNC(avs_release_clip);
        AVSC_DECLARE_FUNC(avs_release_value);
        AVSC_DECLARE_FUNC(avs_release_video_frame);
        AVSC_DECLARE_FUNC(avs_take_clip);
    } func;
} avs_hnd_t;

#define LOAD_AVS_FUNC(name, continue_on_fail)\
{\
    ah->func.name = (void*)GetProcAddress(ah->library, #name);\
    if( !continue_on_fail && !ah->func.name )\
        goto fail;\
}
static int load_avisynth_dll(avs_hnd_t *ah)
{
    ah->library = LoadLibrary("avisynth");
    if(!ah->library)
        return -1;
    LOAD_AVS_FUNC(avs_clip_get_error, 0 );
    LOAD_AVS_FUNC(avs_create_script_environment, 0);
    LOAD_AVS_FUNC(avs_delete_script_environment, 1);
    LOAD_AVS_FUNC(avs_get_error, 1);
    LOAD_AVS_FUNC(avs_get_frame, 0);
    LOAD_AVS_FUNC(avs_get_audio, 0);
    LOAD_AVS_FUNC(avs_get_video_info, 0);
    LOAD_AVS_FUNC(avs_invoke, 0);
    LOAD_AVS_FUNC(avs_bit_blt, 0);
    LOAD_AVS_FUNC(avs_release_clip, 0);
    LOAD_AVS_FUNC(avs_release_value, 0);
    LOAD_AVS_FUNC(avs_release_video_frame, 0);
    LOAD_AVS_FUNC(avs_take_clip, 0);
    return 0;
fail:
    FreeLibrary(ah->library);
    return -1;
}

static AVS_Value invoke_filter(avs_hnd_t *ah, AVS_Value before, const char *filter)
{
    ah->func.avs_release_clip(ah->clip);
    AVS_Value after = ah->func.avs_invoke(ah->env, filter, before, NULL);
    ah->func.avs_release_value(before);
    ah->clip = ah->func.avs_take_clip(after, ah->env);
    ah->vi = ah->func.avs_get_video_info(ah->clip);
    return after;
}

static BITMAPINFOHEADER *create_bmp_header(const AVS_VideoInfo *vi)
{
    BITMAPINFOHEADER *header = (BITMAPINFOHEADER *)calloc(sizeof(BITMAPINFOHEADER), 1);
    if (!header)
        return NULL;

    header->biSize = sizeof(BITMAPINFOHEADER);
    header->biWidth = vi->width;
    header->biHeight = vi->height;
    header->biPlanes = 1;
    header->biBitCount = avs_bits_per_pixel(vi);
    header->biCompression = avs_is_rgb(vi) ? 0 : MAKEFOURCC( 'Y', 'U', 'Y', '2' );
    header->biSizeImage = avs_bmp_size(vi);

    return header;
}

static WAVEFORMATEX *create_wav_header(const AVS_VideoInfo *vi)
{
    WAVEFORMATEX *header = (WAVEFORMATEX *)calloc(sizeof(WAVEFORMATEX), 1);
    if (!header)
        return NULL;

    header->wFormatTag = WAVE_FORMAT_PCM;
    header->nChannels = vi->nchannels;
    header->nSamplesPerSec = vi->audio_samples_per_second;
    header->nBlockAlign = avs_bytes_per_audio_sample(vi);
    header->nAvgBytesPerSec = header->nSamplesPerSec * header->nBlockAlign;
    header->wBitsPerSample = avs_bytes_per_channel_sample(vi) * 8;
    header->cbSize = 0;

    return header;
}

static AVS_Value initialize_avisynth(avs_hnd_t *ah, LPSTR input)
{
    if (load_avisynth_dll(ah))
        return avs_void;

    ah->env = ah->func.avs_create_script_environment(AVS_INTERFACE_25);
    if (ah->func.avs_get_error && ah->func.avs_get_error(ah->env))
        return avs_void;

    AVS_Value arg = avs_new_value_string(input);
    AVS_Value res = ah->func.avs_invoke(ah->env, "Import", arg, NULL);
    if (avs_is_error(res))
        return res;

    AVS_Value mt_test = ah->func.avs_invoke(ah->env, "GetMTMode", avs_new_value_bool(0), NULL);
    int mt_mode = avs_is_int(mt_test) ? avs_as_int(mt_test) : 0;
    ah->func.avs_release_value(mt_test);
    if (mt_mode > 0 && mt_mode < 5) {
        AVS_Value temp = ah->func.avs_invoke(ah->env, "Distributor", res, NULL);
        ah->func.avs_release_value(res);
        res = temp;
    }

    ah->clip = ah->func.avs_take_clip(res, ah->env);
    ah->vi = ah->func.avs_get_video_info(ah->clip);
    if (avs_is_planar(ah->vi)) {
        if (!(ah->vi->width & 1))
            res = invoke_filter(ah, res, "ConvertToYUY2");
        else {
            ah->func.avs_release_value(res);
            return avs_void;
        }
    }
    if (avs_is_rgb32(ah->vi))
        res = invoke_filter(ah, res, "ConvertToRGB24");
    if (ah->vi->sample_type & 0x1C)
        res = invoke_filter(ah, res, "ConvertAudioTo16bit");

    return res;
}

static int close_avisynth_dll(avs_hnd_t *ah)
{
    if (ah->clip)
        ah->func.avs_release_clip(ah->clip);
    if (ah->func.avs_delete_script_environment) {
        ah->func.avs_delete_script_environment(ah->env);
    }
    FreeLibrary(ah->library);
    return 0;
}

INPUT_HANDLE func_open(LPSTR file)
{
    avs_hnd_t *ah = (avs_hnd_t *)calloc(sizeof(avs_hnd_t), 1);
    if (!ah)
        return NULL;

    AVS_Value res = initialize_avisynth(ah, file);
    if (!avs_is_clip(res)) {
        if (ah->library)
            close_avisynth_dll(ah);
        free(ah);
        return NULL;
    }
    ah->func.avs_release_value(res);

    ah->vfmt = create_bmp_header(ah->vi);
    if (!ah->vfmt)
        return NULL;

    ah->afmt = create_wav_header(ah->vi);
    if (!ah->afmt)
        return NULL;

    return ah;
}

BOOL func_close(INPUT_HANDLE ih)
{
    avs_hnd_t *ah = (avs_hnd_t *)ih;
    if (!ah)
        return TRUE;

    if (ah->library)
        close_avisynth_dll(ah);
    if (ah->vfmt)
        free(ah->vfmt);
    if (ah->afmt)
        free(ah->afmt);
    free(ah);

    return TRUE;
}

BOOL func_info_get( INPUT_HANDLE ih, INPUT_INFO *iip )
{
    avs_hnd_t *ah = (avs_hnd_t *)ih;
    memset(iip, 0, sizeof(INPUT_INFO));

    if (avs_has_video(ah->vi)) {
        iip->flag |= INPUT_INFO_FLAG_VIDEO;
        iip->rate = ah->vi->fps_numerator;
        iip->scale = ah->vi->fps_denominator;
        iip->n = ah->vi->num_frames;
        iip->format = ah->vfmt;
        iip->format_size = sizeof(BITMAPINFOHEADER);
        iip->handler = 0;
    }

    if (avs_has_audio(ah->vi)) {
        iip->flag |= INPUT_INFO_FLAG_AUDIO;
        iip->audio_n = ah->vi->num_audio_samples;
        iip->audio_format = ah->afmt;
        iip->audio_format_size = sizeof(WAVEFORMATEX);
    }

    return TRUE;
}

int func_read_video(INPUT_HANDLE ih, int n, void *buf)
{
    avs_hnd_t *ah = (avs_hnd_t *)ih;

    int width = ah->vi->width * avs_bits_per_pixel(ah->vi) >> 3;

    AVS_VideoFrame *frame = ah->func.avs_get_frame(ah->clip, n);
    if (ah->func.avs_clip_get_error(ah->clip))
        return 0;

    ah->func.avs_bit_blt(ah->env, buf, width, avs_get_read_ptr(frame),
                         avs_get_pitch(frame), width, ah->vi->height);
    ah->func.avs_release_video_frame(frame);

    return avs_bmp_size(ah->vi);
}

int func_read_audio(INPUT_HANDLE ih,int start,int length,void *buf)
{
    avs_hnd_t *ah = (avs_hnd_t *)ih;

    ah->func.avs_get_audio(ah->clip, buf, (INT64)start, (INT64)length);

    return length;
}
