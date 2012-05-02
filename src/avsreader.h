/*

AviSynth Script Reader for AviUtl

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
#ifndef AVSREADER_H
#define AVSREADER_H

#include <inttypes.h>

typedef enum {
    TYPE_AVS,
    TYPE_D2V,
    TYPE_NONE
} input_type_t;

typedef struct {
    input_type_t    type;
    void *      (*parse)(LPSTR input);
    void        (*release)(void *h);
    uint8_t *   (*create_keyframe_list)(void *h);
    uint32_t    (*get_total_frames)(void *h);
} parser_t;

#endif /* AVSREADER_H */
