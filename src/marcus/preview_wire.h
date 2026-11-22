#ifndef MARCUS_PREVIEW_WIRE_H
#define MARCUS_PREVIEW_WIRE_H

#include "mm/types.h"
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { MARCUS_PREVIEW_HEADER_BYTES = 24, MARCUS_PREVIEW_PREFIX_BYTES = 4, MARCUS_PREVIEW_MAX_TEXT = 16384 };
enum { MARCUS_PREVIEW_WIRE_OK = 1, MARCUS_PREVIEW_WIRE_HEADER = 0, MARCUS_PREVIEW_WIRE_LIMIT = -1 };

typedef struct MarcusPreviewBytes
{
    const void *data;
    size_t size;
} MarcusPreviewBytes;

/* Spans borrow the caller's buffers. RGBA size counts serialized bytes,
 * excluding any padding between rows in the source. */
typedef struct MarcusPreviewPacket
{
    MMU32 width, height;
    MarcusPreviewBytes title, details, error, rgba;
    size_t pitch;
} MarcusPreviewPacket;

typedef int (*MarcusPreviewWrite)(void *, const void *, size_t);

int marcus_preview_wire_read(const unsigned char *, size_t, MarcusPreviewPacket *);
int marcus_preview_wire_write(const MarcusPreviewPacket *, int framed, void *, MarcusPreviewWrite);
size_t marcus_preview_wire_frame_size(const unsigned char prefix[MARCUS_PREVIEW_PREFIX_BYTES], size_t limit);
int marcus_preview_wire_write_file(const MarcusPreviewPacket *, int framed, FILE *);
void marcus_preview_packet_error(MarcusPreviewPacket *, const char *);

#ifdef __cplusplus
}
#endif
#endif
