/*

AviSynth Script Reader for AviUtl version 0.5.0

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <windows.h>

#define AVSC_NO_DECLSPEC
#undef EXTERN_C
#include "avisynth_c.h"
#include "input.h"

typedef enum {
    TYPE_AVS,
    TYPE_D2V,
    TYPE_NONE
} input_type_t;

INPUT_PLUGIN_TABLE input_plugin_table = {
    INPUT_PLUGIN_FLAG_VIDEO | INPUT_PLUGIN_FLAG_AUDIO,
    "AviSynth Script Reader",
    "AviSynth Script (*.avs)\0*.avs\0" "D2V File (*.d2v)\0*.d2v\0",
    "AviSynth Script Reader version 0.5.0 by Chikuzen",
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

EXTERN_C INPUT_PLUGIN_TABLE __declspec(dllexport) * __stdcall GetInputPluginTable(void)
{
    return &input_plugin_table;
}

#define AVSC_DECLARE_FUNC(name) name##_func name
#define AVS_INTERFACE_25 2
typedef struct {
    int version;
    int highbit_depth;
    int adjust_audio;
    int d2v_upconv;
    char yuy2converter[32];
    int display_width;
    input_type_t ext;
    AVS_Clip *clip;
    AVS_ScriptEnvironment *env;
    const AVS_VideoInfo *vi;
    BITMAPINFOHEADER vfmt;
    WAVEFORMATEX afmt;
    HMODULE library;
    struct {
        AVSC_DECLARE_FUNC(avs_clip_get_error);
        AVSC_DECLARE_FUNC(avs_create_script_environment);
        AVSC_DECLARE_FUNC(avs_delete_script_environment);
        AVSC_DECLARE_FUNC(avs_get_error);
        AVSC_DECLARE_FUNC(avs_get_frame);
        AVSC_DECLARE_FUNC(avs_get_audio);
        AVSC_DECLARE_FUNC(avs_get_version);
        AVSC_DECLARE_FUNC(avs_get_video_info);
        AVSC_DECLARE_FUNC(avs_function_exists);
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
    LOAD_AVS_FUNC(avs_get_version, 0);
    LOAD_AVS_FUNC(avs_get_video_info, 0);
    LOAD_AVS_FUNC(avs_function_exists, 0);
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

static int get_avisynth_version(avs_hnd_t *ah)
{
    if (!ah->func.avs_function_exists(ah->env, "VersionNumber"))
        return 0;

    AVS_Value ver = ah->func.avs_invoke(ah->env, "VersionNumber", avs_new_value_array(NULL, 0), NULL);
    if (avs_is_error(ver))
        return 0;
    if (!avs_is_float(ver))
        return 0;
    int version = (int)(avs_as_float(ver) * 100 + 0.5);
    ah->func.avs_release_value(ver);
    return version;
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

static AVS_Value import_avs(avs_hnd_t *ah, LPSTR input)
{
    AVS_Value arg = avs_new_value_string(input);
    return ah->func.avs_invoke(ah->env, "Import", arg, NULL);
}

static AVS_Value import_d2v(avs_hnd_t *ah, LPSTR input)
{
    if (!ah->func.avs_function_exists(ah->env, "MPEG2Source"))
        return avs_void;

    AVS_Value arg_arr[2] = {avs_new_value_string(input), avs_new_value_int(1)};
    const char *name[2] = {NULL, "upConv"};
    return ah->func.avs_invoke(ah->env, "MPEG2Source", avs_new_value_array(arg_arr, ah->d2v_upconv + 1), name);
}

static AVS_Value initialize_avisynth(avs_hnd_t *ah, LPSTR input)
{
    if (load_avisynth_dll(ah))
        return avs_void;

    ah->env = ah->func.avs_create_script_environment(AVS_INTERFACE_25);
    if (ah->func.avs_get_error && ah->func.avs_get_error(ah->env))
        return avs_void;

    ah->version = get_avisynth_version(ah);

    AVS_Value res = avs_void;
    switch (ah->ext) {
    case TYPE_AVS:
        res = import_avs(ah, input);
        break;
    case TYPE_D2V:
        res = import_d2v(ah, input);
        break;
    default:
        break;
    }
    if (avs_is_error(res) || !avs_defined(res))
        return res;

    if (ah->adjust_audio) {
        AVS_Value arg_arr[3] = {res, avs_new_value_int(0), avs_new_value_int(0)};
        AVS_Value tmp = ah->func.avs_invoke(ah->env, "Trim", avs_new_value_array(arg_arr, 3), NULL);
        ah->func.avs_release_value(res);
        res = tmp;
    }

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

    if (ah->highbit_depth && ah->version >= 260 && avs_is_planar(ah->vi)) {
        if (avs_is_yv411(ah->vi) ||
            ((avs_is_yv24(ah->vi) || avs_is_y8(ah->vi)) && (ah->vi->width & 1)) ||
            ((avs_is_yv16(ah->vi) || avs_is_yv12(ah->vi)) && (ah->vi->width & 3))) {
            ah->func.avs_release_value(res);
            return avs_void;
        }
        if (avs_is_yv12(ah->vi))
            res = invoke_filter(ah, res, "ConvertToYV16");

    } else if (avs_is_yv12(ah->vi) || avs_is_yv16(ah->vi) || avs_is_yv411(ah->vi)) {
        if (ah->func.avs_function_exists(ah->env, ah->yuy2converter))
            res = invoke_filter(ah, res, ah->yuy2converter);
        else
            res = invoke_filter(ah, res, "ConvertToYUY2");
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

static void create_bmp_header(avs_hnd_t *ah)
{
    int pix_type = avs_is_planar(ah->vi) ? 0 : avs_is_rgb(ah->vi) ? 1 : 2;
    LONG width = ah->vi->width >> (!pix_type * ah->highbit_depth);
    if (avs_is_planar(ah->vi) && width & 1)
        width++;
    ah->display_width = width;
    LONG height = ah->vi->height;
    struct {
        WORD  bit_cnt;
        DWORD fourcc;
        DWORD bmp_size;
    } color_table[3] = {
        { 48, MAKEFOURCC('Y', 'C', '4', '8'), width * 6 * height },
        { 24, 0x00000000, (((width * 3 + 3) >> 2) << 2) * height },
        { 16, MAKEFOURCC('Y', 'U', 'Y', '2'), width * height * 2 }
    };

    ah->vfmt.biSize = sizeof(BITMAPINFOHEADER);
    ah->vfmt.biWidth = width;
    ah->vfmt.biHeight = height;
    ah->vfmt.biPlanes = 1;
    ah->vfmt.biBitCount = color_table[pix_type].bit_cnt;
    ah->vfmt.biCompression = color_table[pix_type].fourcc;
    ah->vfmt.biSizeImage = color_table[pix_type].bmp_size;
}

static void create_wav_header(avs_hnd_t *ah)
{
    ah->afmt.wFormatTag = WAVE_FORMAT_PCM;
    ah->afmt.nChannels = ah->vi->nchannels;
    ah->afmt.nSamplesPerSec = ah->vi->audio_samples_per_second;
    ah->afmt.nBlockAlign = avs_bytes_per_audio_sample(ah->vi);
    ah->afmt.nAvgBytesPerSec = ah->afmt.nSamplesPerSec * ah->afmt.nBlockAlign;
    ah->afmt.wBitsPerSample = avs_bytes_per_channel_sample(ah->vi) * 8;
    ah->afmt.cbSize = 0;
}

static int get_config(avs_hnd_t *ah)
{
    FILE *config = NULL;
    while (!config) {
        config = fopen("avsreader.ini", "r");
        if (!config) {
            config = fopen("avsreader.ini", "w");
            if (!config)
                return -1;
            fprintf(config, "highbit_depth=0\n");
            fprintf(config, "adjust_audio_length=1\n");
            fprintf(config, "d2v_upconv=1\n");
            fprintf(config, "yuy2converter=ConvertToYUY2\n");
            fclose(config);
            config = NULL;
        }
    }

    char buf[64];
    if (!fgets(buf, sizeof buf, config) || !sscanf(buf, "highbit_depth=%d", &ah->highbit_depth))
        ah->highbit_depth = 0;
    if (!fgets(buf, sizeof buf, config) || !sscanf(buf, "adjust_audio_length=%d", &ah->adjust_audio))
        ah->adjust_audio = 1;
    if (!fgets(buf, sizeof buf, config) || !sscanf(buf, "d2v_upconv=%d", &ah->d2v_upconv))
        ah->d2v_upconv = 1;
    if (!fgets(buf, sizeof buf, config) || !sscanf(buf, "yuy2converter=%s", ah->yuy2converter))
        strncpy(ah->yuy2converter, "ConvertToYUY2", 32);

    ah->highbit_depth = !!ah->highbit_depth;
    ah->adjust_audio = !!ah->adjust_audio;
    ah->d2v_upconv = !!ah->d2v_upconv;
    fclose(config);

    return 0;
}

INPUT_HANDLE func_open(LPSTR file)
{
    avs_hnd_t *ah = (avs_hnd_t *)calloc(sizeof(avs_hnd_t), 1);
    if (!ah)
        return NULL;

    if (get_config(ah))
        return NULL;

    ah->ext = TYPE_AVS;
    char *ext = strrchr(file, '.');
    if (!ext)
        return NULL;
    if (strcasecmp(ext, ".d2v") == 0)
        ah->ext = TYPE_D2V;

    AVS_Value res = initialize_avisynth(ah, file);
    if (!avs_is_clip(res)) {
        if (ah->library)
            close_avisynth_dll(ah);
        free(ah);
        return NULL;
    }
    ah->func.avs_release_value(res);

    create_bmp_header(ah);
    create_wav_header(ah);

    return ah;
}

BOOL func_close(INPUT_HANDLE ih)
{
    avs_hnd_t *ah = (avs_hnd_t *)ih;
    if (!ah)
        return TRUE;

    if (ah->library)
        close_avisynth_dll(ah);
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
        iip->format = &(ah->vfmt);
        iip->format_size = sizeof(BITMAPINFOHEADER);
        iip->handler = 0;
    }

    if (avs_has_audio(ah->vi)) {
        iip->flag |= INPUT_INFO_FLAG_AUDIO;
        iip->audio_n = ah->vi->num_audio_samples;
        iip->audio_format = &(ah->afmt);
        iip->audio_format_size = sizeof(WAVEFORMATEX);
    }

    return TRUE;
}

typedef struct {
    short y;
    short u;
    short v;
} PIXEL_YC;

static int y8_to_yc48(avs_hnd_t *ah, AVS_VideoFrame *frame, BYTE *dst_p)
{
    int width = ah->vi->width;
    int height = ah->vi->height;
    int dst_pitch_yc = ah->display_width * 6;

    const BYTE *src_pix_y = avs_get_read_ptr(frame);
    int src_pitch_y = avs_get_pitch(frame);

    for (int y = 0; y < height; y++) {
        PIXEL_YC *dst_pix_yc = (PIXEL_YC *)dst_p;
        for (int x = 0; x < width; x++) {
            dst_pix_yc[x].y = (((short)(src_pix_y[x]) * 1197) >> 6) - 299;
            dst_pix_yc[x].u = 0;
            dst_pix_yc[x].v = 0;
        }
        if (width != ah->display_width)
            dst_pix_yc[width] = dst_pix_yc[width - 1];

        dst_p += dst_pitch_yc;
        src_pix_y += src_pitch_y;
    }
    ah->func.avs_release_video_frame(frame);

    return (int)ah->vfmt.biSizeImage;
}

static int yv24_to_yc48(avs_hnd_t *ah, AVS_VideoFrame *frame, BYTE *dst_p)
{
    int width = ah->vi->width;
    int height = ah->vi->height;
    int dst_pitch_yc = ah->display_width * 6;

    const BYTE *src_pix_y = avs_get_read_ptr_p(frame, AVS_PLANAR_Y);
    const BYTE *src_pix_u = avs_get_read_ptr_p(frame, AVS_PLANAR_U);
    const BYTE *src_pix_v = avs_get_read_ptr_p(frame, AVS_PLANAR_V);
    int src_pitch_y = avs_get_pitch_p(frame, AVS_PLANAR_Y);
    int src_pitch_u = avs_get_pitch_p(frame, AVS_PLANAR_U);
    int src_pitch_v = avs_get_pitch_p(frame, AVS_PLANAR_V);

    for (int y = 0; y < height; y++) {
        PIXEL_YC *dst_pix_yc = (PIXEL_YC *)dst_p;
        for (int x = 0; x < width; x++) {
            dst_pix_yc[x].y = (((short)(src_pix_y[x]) * 1197) >> 6) - 299;
            dst_pix_yc[x].u = (((short)(src_pix_u[x]) - 128) * 4681 + 164) >> 8;
            dst_pix_yc[x].v = (((short)(src_pix_v[x]) - 128) * 4681 + 164) >> 8;
        }
        if (width != ah->display_width)
            dst_pix_yc[width] = dst_pix_yc[width - 1];

        dst_p += dst_pitch_yc;
        src_pix_y += src_pitch_y;
        src_pix_u += src_pitch_u;
        src_pix_v += src_pitch_v;
    }
    ah->func.avs_release_video_frame(frame);

    return (int)ah->vfmt.biSizeImage;
}

static int yuv400p16le_to_yc48(avs_hnd_t *ah, AVS_VideoFrame *frame, BYTE *dst_p)
{
    int width = ah->vi->width >> 1;
    int height = ah->vi->height;
    int dst_pitch_yc = ah->display_width * 6;

    const uint16_t *src_pix_y16 = (uint16_t *)avs_get_read_ptr_p(frame, AVS_PLANAR_Y);
    int src_pitch_y16 = avs_get_pitch_p(frame, AVS_PLANAR_Y) >> 1;

    for (int y = 0; y < height; y++) {
        PIXEL_YC *dst_pix_yc = (PIXEL_YC *)dst_p;
        for (int x = 0; x < width; x++) {
            dst_pix_yc[x].y = (short)((((int32_t)src_pix_y16[x] - 4096 ) * 4789) >> 16);
            dst_pix_yc[x].u = 0;
            dst_pix_yc[x].v = 0;
        }
        if (width != ah->display_width)
            dst_pix_yc[width] = dst_pix_yc[width - 1];

        dst_p += dst_pitch_yc;
        src_pix_y16 += src_pitch_y16;
    }
    ah->func.avs_release_video_frame(frame);

    return (int)ah->vfmt.biSizeImage;
}

static int yuv444p16le_to_yc48(avs_hnd_t *ah, AVS_VideoFrame *frame, BYTE *dst_p)
{
    int width = ah->vi->width >> 1;
    int height = ah->vi->height;
    int dst_pitch_yc = ah->display_width * 6;

    const uint16_t *src_pix_y16 = (uint16_t *)avs_get_read_ptr_p(frame, AVS_PLANAR_Y);
    const uint16_t *src_pix_u16 = (uint16_t *)avs_get_read_ptr_p(frame, AVS_PLANAR_U);
    const uint16_t *src_pix_v16 = (uint16_t *)avs_get_read_ptr_p(frame, AVS_PLANAR_V);
    int src_pitch_y16 = avs_get_pitch_p(frame, AVS_PLANAR_Y) >> 1;
    int src_pitch_u16 = avs_get_pitch_p(frame, AVS_PLANAR_U) >> 1;
    int src_pitch_v16 = avs_get_pitch_p(frame, AVS_PLANAR_V) >> 1;

    for (int y = 0; y < height; y++) {
        PIXEL_YC *dst_pix_yc = (PIXEL_YC *)dst_p;
        for (int x = 0; x < width; x++) {
            dst_pix_yc[x].y = (short)((((int32_t)src_pix_y16[x] - 4096) * 4789) >> 16);
            dst_pix_yc[x].u = (short)((((int32_t)src_pix_u16[x] - 32768) * 4683) >> 16);
            dst_pix_yc[x].v = (short)((((int32_t)src_pix_v16[x] - 32768) * 4683) >> 16);
        }
        if (width != ah->display_width)
            dst_pix_yc[width] = dst_pix_yc[width - 1];

        dst_p += dst_pitch_yc;
        src_pix_y16 += src_pitch_y16;
        src_pix_u16 += src_pitch_u16;
        src_pix_v16 += src_pitch_v16;
    }
    ah->func.avs_release_video_frame(frame);

    return (int)ah->vfmt.biSizeImage;
}

static int yuv422p16le_to_yc48(avs_hnd_t *ah, AVS_VideoFrame *frame, BYTE *dst_p)
{
    int width = ah->vi->width >> 1;
    int height = ah->vi->height;
    int dst_pitch_yc = width * 6;

    const uint16_t *src_pix_y16 = (uint16_t *)avs_get_read_ptr_p(frame, AVS_PLANAR_Y);
    const uint16_t *src_pix_u16 = (uint16_t *)avs_get_read_ptr_p(frame, AVS_PLANAR_U);
    const uint16_t *src_pix_v16 = (uint16_t *)avs_get_read_ptr_p(frame, AVS_PLANAR_V);
    int src_pitch_y16 = avs_get_pitch_p(frame, AVS_PLANAR_Y) >> 1;
    int src_pitch_u16 = avs_get_pitch_p(frame, AVS_PLANAR_U) >> 1;
    int src_pitch_v16 = avs_get_pitch_p(frame, AVS_PLANAR_V) >> 1;

    for (int y = 0; y < height; y++) {
        PIXEL_YC *dst_pix_yc = (PIXEL_YC *)dst_p;
        for (int x = 0; x < width; x++)
            dst_pix_yc[x].y = (short)((((int32_t)src_pix_y16[x] - 4096) * 4789) >> 16);

        int tmp = width >> 1;
        for (int x = 0; x < tmp; x++) {
            dst_pix_yc[x * 2].u = (short)((((int32_t)src_pix_u16[x] - 32768) * 4683) >> 16);
            dst_pix_yc[x * 2].v = (short)((((int32_t)src_pix_v16[x] - 32768) * 4683) >> 16);
        }

        tmp--;
        for (int x = 0; x < tmp; x++) {
            dst_pix_yc[x * 2 + 1].u = (short)(((int32_t)dst_pix_yc[x * 2].u + dst_pix_yc[x * 2 + 2].u) >> 1);
            dst_pix_yc[x * 2 + 1].v = (short)(((int32_t)dst_pix_yc[x * 2].v + dst_pix_yc[x * 2 + 2].v) >> 1);
        }

        dst_pix_yc[width - 1].u = dst_pix_yc[width - 2].u;
        dst_pix_yc[width - 1].v = dst_pix_yc[width - 2].v;

        dst_p += dst_pitch_yc;
        src_pix_y16 += src_pitch_y16;
        src_pix_u16 += src_pitch_u16;
        src_pix_v16 += src_pitch_v16;
    }
    ah->func.avs_release_video_frame(frame);

    return (int)ah->vfmt.biSizeImage;
}

int func_read_video(INPUT_HANDLE ih, int n, void *buf)
{
    avs_hnd_t *ah = (avs_hnd_t *)ih;
    BYTE *dst_p = (BYTE *)buf;

    AVS_VideoFrame *frame = ah->func.avs_get_frame(ah->clip, n);
    if (ah->func.avs_clip_get_error(ah->clip))
        return 0;

    if (ah->highbit_depth) {
        if (avs_is_yv24(ah->vi))
            return yuv444p16le_to_yc48(ah, frame, dst_p);
        if (avs_is_yv16(ah->vi))
            return yuv422p16le_to_yc48(ah, frame, dst_p);
        if (avs_is_y8(ah->vi))
            return yuv400p16le_to_yc48(ah, frame, dst_p);
    }

    if (avs_is_yv24(ah->vi))
        return yv24_to_yc48(ah, frame, dst_p);

    if (avs_is_y8(ah->vi))
        return y8_to_yc48(ah, frame, dst_p);

    int row_size = avs_get_row_size(frame);
    int dst_pitch = ((row_size + 3) >> 2) << 2;
    ah->func.avs_bit_blt(ah->env, dst_p, dst_pitch, avs_get_read_ptr(frame),
                         avs_get_pitch(frame), row_size, ah->vi->height);
    ah->func.avs_release_video_frame(frame);

    return (int)ah->vfmt.biSizeImage;
}

int func_read_audio(INPUT_HANDLE ih,int start,int length,void *buf)
{
    avs_hnd_t *ah = (avs_hnd_t *)ih;

    ah->func.avs_get_audio(ah->clip, buf, (INT64)start, (INT64)length);

    return length;
}
