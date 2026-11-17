/* Filename naming policy converted from Kauai filename.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/filename_naming.h"
#include "mm/string_format.h"
#include "mm/string_ops.h"

typedef union NamingBuffer { unsigned char bytes[257]; unsigned short words[257]; } NamingBuffer;
static void put(NamingBuffer *b, unsigned size, unsigned i, unsigned ch)
{ if (size == 1) b->bytes[i] = (unsigned char)ch; else b->words[i] = (unsigned short)ch; }
static MMI32 length(const MMStringValue *s)
{ return s->unit == 1 ? ((const unsigned char *)s->buffer)[0] : ((const unsigned short *)s->buffer)[0]; }
static const void *characters(const MMStringValue *s)
{ return (const unsigned char *)s->buffer + s->unit; }
static void validate(const MMFilenameValue *v, const MMFilenameNamingHost *h, int flags)
{ if (h->path.value.validate) h->path.value.validate(h->path.value.context, v, flags); }
static MMU32 advance(MMFilenameNamingState *s)
{ s->sequence = (s->sequence + 1) & 0xffffUL; return s->sequence; }
static int number(void *c, unsigned control, MMStringFormatArgument *a)
{ (void)control; a->number = *(const MMU32 *)c; return 1; }
static void general(const MMFilenameValue *v, const MMFilenameNamingHost *h)
{ if (h->path.issue) h->path.issue(h->path.context, v, MM_FILENAME_GENERAL); }
int mm_filename_unique(const MMFilenameValue *v, const MMFilenameNamingHost *h, MMFilenameNamingState *s, MMI32 type)
{
    NamingBuffer candidate_buffer, old_buffer, pattern;
    MMStringValue candidate, old;
    MMStringFormatArguments arguments;
    MMU32 bits;
    unsigned i;
    int attempt;
    validate(v, h, MM_FILENAME_FILE | MM_FILENAME_DIR);
    candidate = v->path; candidate.buffer = &candidate_buffer; old = v->path; old.buffer = &old_buffer;
    validate(v, h, 0);
    if (v->state->type == MM_FILENAME_DIRECTORY) mm_string_value_clear(&old);
    else mm_filename_get_leaf(v, &h->path.value, &old);
    bits = h->clock(h->path.context); bits = (bits + advance(s)) & 0xffffUL;
    for (i = 0; i < 8; ++i) put(&pattern, candidate.unit, i, (unsigned)"Temp%04x"[i]);
    arguments.context = &bits; arguments.next = number; arguments.bad_format = 0;
    for (attempt = 20; attempt; --attempt, bits = (bits + advance(s)) & 0xffffUL) {
        mm_string_format(&candidate, &pattern, 8, &arguments);
        if (mm_string_value_equal(&candidate, characters(&old), length(&old))) continue;
        if (mm_filename_set_leaf(v, &h->path.value, characters(&candidate), length(&candidate), type) &&
            mm_filename_exists(v, &h->path) == MM_FILENAME_NO) return 1;
    }
    mm_filename_value_clear(v, &h->path.value); general(v, h); return 0;
}
int mm_filename_temp(const MMFilenameValue *v, const MMFilenameNamingHost *h, MMFilenameNamingState *s,
                     const MMFilenameValue *cached, MMI32 type)
{
    NamingBuffer buffer;
    validate(v, h, 0);
    if (cached->state->type != MM_FILENAME_DIRECTORY) {
        if (!h->temp_path(h->path.context, &buffer, 255)) { general(v, h); return 0; }
        mm_string_value_set(&cached->path, &buffer, (MMI32)mm_string_zero_length(&buffer, cached->path.unit));
        cached->state->type = MM_FILENAME_DIRECTORY; validate(cached, h, MM_FILENAME_DIR);
    }
    if (h->path.copy_name) h->path.copy_name(h->path.context, v, cached);
    else { v->state->type = cached->state->type; mm_string_value_copy(&v->path, cached->path.buffer); }
    return mm_filename_unique(v, h, s, type);
}
