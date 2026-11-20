/* AVI and Video 1 decoding converted from Kauai avi_movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_AVI_MOVIE_H
#define MM_ENGINE_AVI_MOVIE_H
#include "types.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMAviMovie MMAviMovie;

typedef struct MMAviInfo
{
    MMI32 width, height, frames;
    MMU32 scale, rate, start;
} MMAviInfo;

typedef struct MMAviAudio
{
    MMU32 rate, start;
    unsigned channels, bits;
    const unsigned char *pcm; /* Borrowed interleaved U8 or little-endian S16. */
    size_t pcm_bytes;
} MMAviAudio;

typedef struct MMAviAllocator
{
    void *context;
    void *(*allocate)(void *, size_t);
    void (*release)(void *, void *);
} MMAviAllocator;

/* Slot is initially null. Successful reading replaces its owner and copies
 * the RIFF bytes. Failure preserves the prior movie and its borrowed audio.
 * A null allocator uses the C heap. A supplied allocator is copied, borrows
 * its context until close, and must supply aligned storage. Its callbacks
 * must not re-enter the same owner. */
int mm_avi_read(MMAviMovie **slot, const void *source, size_t bytes, const MMAviAllocator *);
void mm_avi_close(MMAviMovie *);
MMAviInfo mm_avi_info(const MMAviMovie *);
MMAviAudio mm_avi_audio(const MMAviMovie *);

/* Indices are relative to stream start. Time includes the stream offset.
 * The end index returns the exclusive end time, in milliseconds. */
MMI32 mm_avi_time_for_frame(const MMAviMovie *, MMI32 frame);
MMI32 mm_avi_frame_for_time(const MMAviMovie *, MMI32 milliseconds);

/* Values are top-down BGRX32 with a zero X byte. Invalid indices report zero
 * bytes. Decode preserves the caller's output on failure. Destination memory
 * is disjoint from the movie's borrowed storage. Backward seeks rebuild the
 * internal indexed reference frame; packet validation is deferred to decode. */
size_t mm_avi_frame_bytes(const MMAviMovie *, MMI32 frame);
int mm_avi_decode_frame(MMAviMovie *, MMI32 frame, unsigned char *output, size_t capacity);

/* Decode one packet into an existing top-down indexed frame. Skip blocks
 * retain the previous pixels. A failed decode leaves the buffer untouched. */
int mm_avi_decode_video1(const void *packet, size_t bytes, MMI32 width, MMI32 height,
                         unsigned char *pixels, size_t pixel_count);

#ifdef __cplusplus
}
#endif
#endif
