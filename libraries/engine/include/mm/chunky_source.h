/* Chunky source parsing converted from Kauai chcm.cpp by ShonK.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_CHUNKY_SOURCE_H
#define MM_ENGINE_CHUNKY_SOURCE_H

#include "chunky_lexer.h"

#ifdef __cplusplus
namespace Chunky {
#endif

enum MMChunkyError
{
    ertNil = 0,
    ertOom,
    ertOpenFile,
    ertReadMeta,
    ertRangeByte,
    ertRangeShort,
    ertBufData,
    ertParenOpen,
    ertEof,
    ertNeedString,
    ertNeedNumber,
    ertBadToken,
    ertParenClose,
    ertChunkHead,
    ertDupChunk,
    ertBodyChildHead,
    ertChildMissing,
    ertCycle,
    ertBodyParentHead,
    ertParentMissing,
    ertBodyAlignRange,
    ertBodyFile,
    ertNeedEndChunk,
    ertListHead,
    ertListEntrySize,
    ertVarUndefined,
    ertItemOverflow,
    ertBadFree,
    ertSyntax,
    ertGroupHead,
    ertGroupEntrySize,
    ertGstHead,
    ertGstEntrySize,
    ertScript,
    ertAdoptHead,
    ertNeedChunk,
    ertBodyBitmapHead,
    ertReadBitmap,
    ertBadScript,
    ertReadCursor,
    ertPackedFile,
    ertReadMidi,
    ertBadPackFmt,
    ertLonerInSub,
    ertNoEndSubFile,
    ertLim
};

#ifdef __cplusplus
}
extern "C" {
#endif

enum { MM_CHUNKY_ALIGN_MIN = 2, MM_CHUNKY_ALIGN_MAX = 1024 };

enum MMChunkySourceRead
{
    MM_CHUNKY_READ_RAW,
    MM_CHUNKY_READ_CLEAN,
    MM_CHUNKY_READ_CLEAN_EOF
};

typedef struct MMChunkySourceToken
{
    int kind;
    MMI32 number;
    const void *text;
    MMU32 length;
} MMChunkySourceToken;

typedef struct MMChunkyHeaderField
{
    MMI32 number;
    MMLexText *text; /* Optional destination for a counted string. */
} MMChunkyHeaderField;

typedef struct MMChunkySource
{
    void *context;
    /* Read zero at EOF. CLEAN reports mandatory EOF, while CLEAN_EOF accepts it.
     * Counted text is borrowed until the next read. Modes update format in place. */
    int (*next)(void *, MMChunkySourceToken *, int read_mode);
    void (*report)(void *, int error);

    /* Append synchronously. The destination may discard data after a diagnostic.
     * An allocation or write failure returns zero. File import reports its errors. */
    int (*append)(void *, const void *bytes, MMU32 size);
    MMU32 (*size)(void *);
    void (*import_file)(void *);

    const MMChunkyFormat *format;
    unsigned unit_bytes;
    int native_encoding, native_order;
} MMChunkySource;

/* Copies a counted string token whose borrowed text is still valid. */
int mm_chunky_copy_text(MMLexText *, const MMChunkySourceToken *, unsigned unit_bytes);
/* Returns the populated field count, or -1 for a structural failure. Field
 * type errors report diagnostics and leave header parsing at its usual boundary. */
MMI32 mm_chunky_parse_header(const MMChunkySource *, MMChunkyHeaderField *, MMU32 capacity);
/* Consumes payload tokens starting with token, then leaves the first following
 * token there. Returns zero when the first token cannot begin payload data. */
int mm_chunky_parse_data(const MMChunkySource *, MMChunkySourceToken *);

#ifdef __cplusplus
}
#endif
#endif
