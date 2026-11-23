/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_COMPRESSED_WAVE_DECODE_H
#define KAUAI_COMPRESSED_WAVE_DECODE_H
#include "mm/wave.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMDecodedWave
{
    short *samples;
    size_t count;
    MMI32 rate, channels;
} MMDecodedWave;

/* Returns the encoding on success. Output must be empty on entry. A failure
 * or an unsupported format leaves it untouched. */
int mm_compressed_wave_decode(const void *, size_t, MMDecodedWave *);
void mm_compressed_wave_dispose(MMDecodedWave *);

#ifdef __cplusplus
}
#endif
#endif
