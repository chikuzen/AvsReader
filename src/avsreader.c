/*
This file is part of AvsReader


AviSynth Script Reader for AviUtl version 0.6.2

Copyright (c) 2012 Oka Motofumi (chikuzen.mo at gmail dot com)
                   Tanaka Masaki

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

#ifdef DEBUG_ENABLED
#include <stdarg.h>
#endif

#define AVSC_NO_DECLSPEC
#undef EXTERN_C
#include "avisynth_c.h"
#include "input.h"
#include "avsreader.h"
#include "d2v_parser.h"

INPUT_PLUGIN_TABLE input_plugin_table = {
    INPUT_PLUGIN_FLAG_VIDEO | INPUT_PLUGIN_FLAG_AUDIO,
    "AviSynth Script Reader",
    "AviSynth Script (*.avs)\0*.avs\0" "D2V File (*.d2v)\0*.d2v\0",
    "AviSynth Script Reader version 0.6.1",
    NULL,
    NULL,
    func_open,
    func_close,
    func_info_get,
    func_read_video,
    func_read_audio,
    func_is_keyframe,
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
    int adjust_audio_length;
    char yuy2converter[32];
    struct {
        int upconv;
        int idct;
        int cpu;
        int moderate_h;
        int moderate_v;
        char cpu2[8];
        int info;
        int showq;
        int fastmc;
        int keyframe_judge;
        int total_frames;
        uint8_t *keyframe_list;
    } d2v;
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

#ifdef DEBUG_ENABLED
void debug_msg(char *format, ...)
{
    va_list args;
    char buf[1024];
    va_start(args, format);
    vsprintf(buf, format, args);
    va_end(args);
    MessageBox(NULL, buf, "debug info", MB_OK);
}
#endif

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
    if (avs_is_error(ver) || !avs_is_float(ver))
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

static AVS_Value import_d2v_donald(avs_hnd_t *ah, LPSTR input)
{
    if (!ah->func.avs_function_exists(ah->env, "DGDecode_MPEG2Source"))
        return avs_void;

    AVS_Value arg_arr[10] = {
        avs_new_value_string(input),
        avs_new_value_int(ah->d2v.upconv),
        avs_new_value_int(ah->d2v.idct),
        avs_new_value_int(ah->d2v.cpu),
        avs_new_value_int(ah->d2v.moderate_h),
        avs_new_value_int(ah->d2v.moderate_v),
        avs_new_value_string(ah->d2v.cpu2),
        avs_new_value_int(ah->d2v.info),
        avs_new_value_bool(ah->d2v.showq),
        avs_new_value_bool(ah->d2v.fastmc)
    };
    const char *name[10] = {
        NULL, "upConv", "idct", "cpu", "moderate_h",
        "moderate_v", "cpu2", "info", "showQ", "fastMC"
    };
    return ah->func.avs_invoke(ah->env, "DGDecode_MPEG2Source", avs_new_value_array(arg_arr, 10), name);
}

#ifdef D2V_DVD2AVI_ENABLED
static AVS_VAlue import_d2v_jackie(avs_hnd_t *ah, LPSTR input)
{
    if (!ah->function_exists(ah->env, "MPEG2Dec3_MPEG2Source"))
        return avs_void;
    AVS_Value arg_arr[8] = {
        avs_new_value_string(input),
        avs_new_value_int(ah->d2v.idct),
        avs_new_value_int(ah->d2v.cpu),
        avs_new_value_int(ah->d2v.moderate_h),
        avs_new_value_int(ah->d2v.moderate_v),
        avs_new_value_string(ah->d2v.cpu2),
        avs_new_value_bool(ah->d2v.showq),
        avs_new_value_bool(ah->d2v.fastmc)
    };
    const char *name[8] = {
        NULL, "idct", "cpu", "moderate_h",
        "moderate_v", "cpu2", "showQ", "fastMC"
    };
    AVS_Value arg =avs_new_value_string(input);
    return ah->avs_invoke(ah->env, "MPEG2Dec3_MPEG2Source", avs_new_value_array(arg_arr, 8), name);
}
#endif

static void create_index(avs_hnd_t *ah, LPSTR input)
{
    static parser_t *parser_table[3] = {
        NULL, &d2v_parser, NULL
    };

    parser_t *parser = NULL;
    for (int i = 0; i < TYPE_NONE; i++)
        if (parser_table[i] && parser_table[i]->type == ah->ext) {
            parser = parser_table[i];
            break;
        }
    if (!parser)
        return;

    void *info = parser->parse(input);
    if (!info) {
        MessageBox(HWND_DESKTOP, "input file parse failed.\ndisable keyframe judge.", "AvsReader", MB_OK | MB_ICONWARNING);
        return;
    }

    ah->d2v.keyframe_list = parser->create_keyframe_list(info);
    ah->d2v.total_frames  = parser->get_total_frames(info);

    parser->release(info);
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
    case TYPE_D2V_DONALD:
        res = import_d2v_donald(ah, input);
        break;
    case TYPE_D2V_JACKIE:
#ifdef D2V_DVD2AVI_ENABLED
        res = import_d2v_jackie(ah, input);
        break;
#endif
    default:
        break;
    }

    if (avs_is_error(res) || !avs_defined(res))
        return res;

    if (ah->ext == TYPE_D2V_DONALD && ah->d2v.keyframe_judge)
        create_index(ah, input);

    if (ah->adjust_audio_length) {
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
    if (ah->func.avs_delete_script_environment)
        ah->func.avs_delete_script_environment(ah->env);

    FreeLibrary(ah->library);
    return 0;
}

static void create_bmp_header(avs_hnd_t *ah)
{
    int pix_type = avs_is_planar(ah->vi) ? 0 : avs_is_rgb(ah->vi) ? 1 : 2;
    LONG width = ah->vi->width >> (!pix_type * ah->highbit_depth);
    LONG height = ah->vi->height;
    struct {
        WORD  bit_cnt;
        DWORD fourcc;
        DWORD bmp_size;
    } color_table[3] = {
        { 48, MAKEFOURCC('Y', 'C', '4', '8'), (((width * 6 + 3) >> 2) << 2) * height },
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

#define CONFIG_FILE "avsreader.ini"
static int generate_default_config(void)
{
    FILE *config = fopen(CONFIG_FILE, "wt");
    if (!config)
        return -1;
    fprintf(config,
            "highbit_depth=0\n"
            "adjust_audio_length=1\n"
            "yuy2converter=ConvertToYUY2\n"
            "d2v_upconv=1\n"
            "d2v_idct=0\n"
            "d2v_cpu=0\n"
            "d2v_moderate_h=20\n"
            "d2v_moderate_v=40\n"
            "d2v_cpu2=\n"
            "d2v_info=0\n"
            "d2v_showq=0\n"
            "d2v_fastmc=0\n"
            "d2v_keyframe_judge=0\n");
    fclose(config);
    return 0;
}

static int get_config(avs_hnd_t *ah)
{
    FILE *config = NULL;
    while (!config) {
        config = fopen(CONFIG_FILE, "rt");
        if (!config && generate_default_config())
            return -1;
    }

    struct {
        const char *prefix;
        size_t length;
        const char *format;
        void *address;
    } conf_table[] = {
        { "highbit_depth=",       14, "%d", &ah->highbit_depth       },
        { "adjust_audio_length=", 20, "%d", &ah->adjust_audio_length },
        { "yuy2converter=",       14, "%s",  ah->yuy2converter       },
        { "d2v_upconv=",          11, "%d", &ah->d2v.upconv          },
        { "d2v_idct=",             9, "%d", &ah->d2v.idct            },
        { "d2v_cpu=",              8, "%d", &ah->d2v.cpu             },
        { "d2v_moderate_h=",      15, "%d", &ah->d2v.moderate_h      },
        { "d2v_moderate_v=",      15, "%d", &ah->d2v.moderate_v      },
        { "d2v_cpu2=",             9, "%s",  ah->d2v.cpu2            },
        { "d2v_info=",             9, "%d", &ah->d2v.info            },
        { "d2v_showq=",           10, "%d", &ah->d2v.showq           },
        { "d2v_fastmc=",          11, "%d", &ah->d2v.fastmc          },
        { "d2v_keyframe_judge=",  19, "%d", &ah->d2v.keyframe_judge  },
        { 0 }
    };

    char buf[64];
    while (fgets(buf, sizeof buf, config)) {
        for (int i = 0; conf_table[i].prefix; i++) {
            if (strncmp(buf, conf_table[i].prefix, conf_table[i].length) == 0) {
                sscanf(buf + conf_table[i].length, conf_table[i].format, conf_table[i].address);
                break;
            }
        }
    }
#ifdef DEBUG_ENABLED
    debug_msg("hbd=%d, aal=%d, yuy2=%s, upcnv=%d, idct=%d\n"
              "cpu=%d, modh=%d, modv=%d, cpu2=%s, info=%d\n"
              "showq=%d, fstmc=%d, keyfrm=%d",
              ah->highbit_depth, ah->adjust_audio_length, ah->yuy2converter, ah->d2v.upconv, ah->d2v.idct,
              ah->d2v.cpu, ah->d2v.moderate_h, ah->d2v.moderate_v, ah->d2v.cpu2, ah->d2v.info,
              ah->d2v.showq, ah->d2v.fastmc, ah->d2v.keyframe_judge);
#endif
    ah->d2v.upconv = !!ah->d2v.upconv;
    ah->d2v.info = !!ah->d2v.info;
    if (!ah->yuy2converter)
        strncpy(ah->yuy2converter, "ConvertToYUY2", 32);
    if (ah->d2v.idct > 7 || ah->d2v.idct < 0)
        ah->d2v.idct = 0;
    if (ah->d2v.cpu < 0 || ah->d2v.cpu > 6)
        ah->d2v.cpu = 0;
    if (ah->d2v.moderate_h < 0 || ah->d2v.moderate_h > 255)
        ah->d2v.moderate_h = 20;
    if (ah->d2v.moderate_v < 0 || ah->d2v.moderate_v > 255)
        ah->d2v.moderate_v = 40;
    if (strlen(ah->d2v.cpu2) != 0 && strspn(ah->d2v.cpu2, "ox") != 6)
        strncpy(ah->d2v.cpu2, "", 8);
#ifdef DEBUG_ENABLED
    debug_msg("hbd=%d, aal=%d, yuy2=%s, upcnv=%d, idct=%d\n"
              "cpu=%d, modh=%d, modv=%d, cpu2=%s, info=%d\n"
              "showq=%d, fstmc=%d keyfrm=%d",
              ah->highbit_depth, ah->adjust_audio_length, ah->yuy2converter, ah->d2v.upconv, ah->d2v.idct,
              ah->d2v.cpu, ah->d2v.moderate_h, ah->d2v.moderate_v, ah->d2v.cpu2, ah->d2v.info,
              ah->d2v.showq, ah->d2v.fastmc, ah->d2v.keyframe_judge);
#endif
    fclose(config);
    return 0;
}
#undef CONFIG_FILE

static int d2v_type_check(avs_hnd_t *ah, LPSTR input)
{
    FILE *d2v = fopen(input, "rt");
    if (!d2v)
        return -1;

    ah->ext = TYPE_D2V_DONALD;
    char buf[32];
    fgets(buf, sizeof buf, d2v);
    if (strncmp(buf, "DGIndexProjectFile", 18)) {
        ah->ext = TYPE_D2V_JACKIE;
        if (ah->d2v.idct > 5)
            ah->d2v.idct = 0;
    }
    fclose(d2v);

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
    if (!ext || (strcasecmp(ext, ".d2v") == 0 && d2v_type_check(ah, file)))
        return NULL;

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

    if (ah->d2v.keyframe_list)
        free(ah->d2v.keyframe_list);
    if (ah->library)
        close_avisynth_dll(ah);
    free(ah);

    return TRUE;
}

BOOL func_info_get(INPUT_HANDLE ih, INPUT_INFO *iip)
{
    avs_hnd_t *ah = (avs_hnd_t *)ih;
    memset(iip, 0, sizeof(INPUT_INFO));

    if (avs_has_video(ah->vi)) {
        iip->flag |= INPUT_INFO_FLAG_VIDEO;
        iip->rate = ah->vi->fps_numerator;
        iip->scale = ah->vi->fps_denominator;
        iip->n = ah->vi->num_frames;
        iip->format = &ah->vfmt;
        iip->format_size = sizeof(BITMAPINFOHEADER);
    }

    if (avs_has_audio(ah->vi)) {
        iip->flag |= INPUT_INFO_FLAG_AUDIO;
        iip->audio_n = ah->vi->num_audio_samples;
        iip->audio_format = &ah->afmt;
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
    int dst_pitch_yc = ((width * 6 + 3) >> 2) << 2;

    const BYTE *src_pix_y = avs_get_read_ptr(frame);
    int src_pitch_y = avs_get_pitch(frame);

    for (int y = 0; y < height; y++) {
        PIXEL_YC *dst_pix_yc = (PIXEL_YC *)dst_p;
        for (int x = 0; x < width; x++)
            dst_pix_yc[x].y = (((short)(src_pix_y[x]) * 1197) >> 6) - 299;

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
    int dst_pitch_yc = ((width * 6 + 3) >> 2) << 2;

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
    int dst_pitch_yc = ((width * 6 + 3) >> 2) << 2;

    const uint16_t *src_pix_y16 = (uint16_t *)avs_get_read_ptr_p(frame, AVS_PLANAR_Y);
    int src_pitch_y16 = avs_get_pitch_p(frame, AVS_PLANAR_Y) >> 1;

    for (int y = 0; y < height; y++) {
        PIXEL_YC *dst_pix_yc = (PIXEL_YC *)dst_p;
        for (int x = 0; x < width; x++)
            dst_pix_yc[x].y = (short)((((int32_t)src_pix_y16[x] - 4096 ) * 4789) >> 16);

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
    int dst_pitch_yc = ((width * 6 + 3) >> 2) << 2;

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

BOOL func_is_keyframe(INPUT_HANDLE ih,int frame)
{
    avs_hnd_t *ah = (avs_hnd_t *)ih;
    if (ah->ext != TYPE_D2V_DONALD || !ah->d2v.keyframe_list || !ah->d2v.keyframe_judge)
        return TRUE;
    if (frame >= ah->d2v.total_frames)
        return FALSE;

    return ah->d2v.keyframe_list[frame];
}
