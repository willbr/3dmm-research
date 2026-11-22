/* Chunky lexer tokens and rules converted from Kauai chcm.h and chcm.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_CHUNKY_LEXER_H
#define MM_ENGINE_CHUNKY_LEXER_H

#include "collection_storage.h"
#include "lexer.h"
#include "string_table.h"

#ifdef __cplusplus
namespace Chunky {
#endif

enum MMChunkyTokenKind
{
    ttChunk = ttLimBase,
    ttChild,
    ttParent,
    ttAlign,
    ttFile,
    ttMeta,
    ttBitmap,
    ttFree,
    ttItem,
    ttVar,
    ttGl,
    ttAl,
    ttGg,
    ttAg,
    ttGst,
    ttAst,
    ttScript,
    ttScriptP,
    ttModeStn,
    ttModeStz,
    ttModeSz,
    ttModeSt,
    ttModeByte,
    ttModeShort,
    ttModeLong,
    ttEndChunk,
    ttAdopt,
    ttMacBo,
    ttWinBo,
    ttMacOsk,
    ttWinOsk,
    ttBo,
    ttOsk,
    ttMask,
    ttLoner,
    ttCursor,
    ttPalette,
    ttPrePacked,
    ttPack,
    ttPackedFile,
    ttMidi,
    ttPackFmt,
    ttSubFile,

    ttLimChlx
};

enum MMChunkyStringMode
{
    smStn,
    smStz,
    smSz,
    smSt
};

#ifdef __cplusplus
}
extern "C" {
#endif

typedef struct MMChunkyLexer
{
    MMCollectionStorage storage;
    MMStringTable variables;
} MMChunkyLexer;

/* The variable table borrows its own storage member. Keep the lexer in place
 * after init. Pass a base lexer of the same unit width to every next call. */
int mm_chunky_lexer_init(MMChunkyLexer *, unsigned unit_bytes, const MMStorageAllocator *);
void mm_chunky_lexer_dispose(MMChunkyLexer *);
int mm_chunky_lexer_next(MMChunkyLexer *, MMLexer *, MMLexToken *, MMLexNumberReader, void *);

typedef struct MMChunkyFormat
{
    MMI32 string_mode, number_bytes;
    short byte_order, os_kind;
} MMChunkyFormat;

typedef struct MMChunkyFormats
{
    short mac_order, windows_order, mac_os_kind, windows_os_kind;
} MMChunkyFormats;

/* Consume mode directives and signs. Returns zero to continue reading.
 * A returned token with a pending sign needs a source diagnostic. */
int mm_chunky_clean_token(MMChunkyFormat *, const MMChunkyFormats *, int kind, MMI32 *value, int *sign);

enum MMChunkyNumberResult
{
    MM_CHUNKY_NUMBER_OK,
    MM_CHUNKY_NUMBER_BYTE_RANGE,
    MM_CHUNKY_NUMBER_SHORT_RANGE
};
/* Writes the low bytes in the requested order, including on a range error.
 * Width must be 1, 2, or 4. A nonzero swap reverses native byte order. */
int mm_chunky_write_number(unsigned width, int swap, MMI32 value, unsigned char *out);

#ifdef __cplusplus
}
#endif
#endif
