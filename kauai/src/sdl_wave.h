/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef KAUAI_SDL_WAVE_H
#define KAUAI_SDL_WAVE_H
#include "audio_mixer.h"
#include "mm/wave_import.h"

#ifdef __cplusplus
extern "C" {
#endif

/* On success the caller owns the stereo sample. Failure leaves out unchanged. */
int mm_sdl_wave_decode(const void *, size_t, MMAudioSample **out);
int mm_sdl_wave_seconds(const void *, size_t, double *seconds);
/* The selection borrows input or scratch and stays valid until either changes. */
int mm_sdl_wave_prepare(const void *, size_t, MMWaveBuffer *scratch, const unsigned char **, MMU32 *size);
int mm_sdl_wave_import(void *, const MMReadStream *, MMI32, const MMChunkNameValue *, MMU32 *, const MMArchiveWriter *);

#ifdef __cplusplus
}
#endif
#endif
