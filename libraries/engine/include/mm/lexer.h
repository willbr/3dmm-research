/* Base lexer converted from Kauai lex.h.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_LEXER_H
#define MM_ENGINE_LEXER_H

#include "lex_token.h"
#include "read_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MM_LEX_TEXT_CAPACITY 255
#define MM_LEX_BUFFER_UNITS 512

typedef struct MMLexText
{
    unsigned length;
    union
    {
        unsigned char narrow[MM_LEX_TEXT_CAPACITY];
        unsigned short wide[MM_LEX_TEXT_CAPACITY];
    } units;
} MMLexText;

typedef struct MMLexToken
{
    int kind;
    MMI32 number;
    MMLexText text;
} MMLexToken;

typedef struct MMLexer
{
    MMReadStream input;
    MMLexText filename;
    MMU32 offset, line, column;
    unsigned begin, end, unit_bytes;
    int line_start, skip_line, union_strings, has_filename;
    union
    {
        unsigned char narrow[MM_LEX_BUFFER_UNITS];
        unsigned short wide[MM_LEX_BUFFER_UNITS];
    } buffer;
} MMLexer;

/* The optional reader continues an octal or decimal token after its first
 * digit, using the same lexer cursor. Hex and escapes always read in C. */
typedef void (*MMLexNumberReader)(void *context, MMLexToken *token, unsigned first, unsigned base);

/* Borrows positional input in one- or two-byte native text units. */
int mm_lexer_init(MMLexer *lexer, const MMReadStream *input, unsigned unit_bytes, int union_strings);
/* Returns zero at EOF, or one for a token including ttError. */
int mm_lexer_next(MMLexer *lexer, MMLexToken *token, MMLexNumberReader number_reader, void *context);
/* Chunky archive paths are quoted without interpreting escapes. */
int mm_lexer_path(MMLexer *lexer, MMLexText *text);
/* Cursor operations for derived number readers. Advance after a successful peek. */
int mm_lexer_peek(MMLexer *lexer, unsigned *ch);
void mm_lexer_advance(MMLexer *lexer);
int mm_lexer_is_decimal(unsigned ch);
MMI32 mm_lexer_number(MMLexer *lexer, unsigned first, unsigned base, MMU32 limit);

#ifdef __cplusplus
}
#endif
#endif
