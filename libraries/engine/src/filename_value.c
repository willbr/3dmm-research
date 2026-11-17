/* Filename value rules converted from Kauai filename.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/filename_value.h"
#include "mm/wide.h"
#include <stddef.h>
typedef union NameBuffer { unsigned char bytes[257]; unsigned short words[257]; } NameBuffer;
static unsigned unit(const MMStringValue *s, MMI32 i)
{ return s->unit == 1 ? ((const unsigned char *)s->buffer)[i] : ((const unsigned short *)s->buffer)[i]; }
static MMI32 length(const MMStringValue *s) { return (MMI32)unit(s, 0); }
static const void *characters(const MMStringValue *s)
{ return (const unsigned char *)s->buffer + s->unit; }
static void validate(const MMFilenameValue *v, const MMFilenameValueHost *h, int flags)
{ if (h->validate) h->validate(h->context, v, flags); }
static void assertion(const MMFilenameValueHost *h, int valid, int code)
{ if (!valid && h->assertion) h->assertion(h->context, code); }
static void put(NameBuffer *b, unsigned size, unsigned ch)
{ if (size == 1) b->bytes[0] = (unsigned char)ch; else b->words[0] = (unsigned short)ch; }
void mm_filename_value_clear(const MMFilenameValue *v, const MMFilenameValueHost *h)
{
    v->state->type = MM_FILENAME_NIL; mm_string_value_clear(&v->path);
    validate(v, h, MM_FILENAME_EMPTY);
}
MMI32 mm_filename_clean_type(MMI32 type, const MMFilenameValueHost *h, const MMStringValue *extension)
{
    MMU32 result = 0, bits = (MMU32)type;
    int zero = 0, i;
    unsigned ch;
    NameBuffer character;
    if (extension) mm_string_value_clear(extension);
    if (type == MM_FILENAME_NIL || type == MM_FILENAME_DIRECTORY) return type;
    for (i = 0; i < 4; ++i) {
        ch = (unsigned)((bits >> (i * 8)) & 255U);
        if (!ch) zero = 1;
        if (!zero) {
            ch = h->upper(h->context, ch) & 255U;
            result |= (MMU32)ch << (i * 8);
            if (extension) { put(&character, extension->unit, ch); mm_string_value_insert(extension, 0, &character, 1); }
        }
    }
    return mm_signed_word(result);
}
MMI32 mm_filename_extension_length(const MMFilenameValue *v, const MMFilenameValueHost *h)
{
    MMI32 count, n = length(&v->path);
    unsigned ch;
    validate(v, h, MM_FILENAME_BASE);
    for (count = 1; count <= 5 && n > 0; ++count, --n) {
        ch = unit(&v->path, n);
        if (v->path.unit == 2 && !(ch < 128 || ch >= 0xff80U)) return 0;
        if (ch == '.') return count;
        if (h->separator(h->context, ch)) return 0;
    }
    return 0;
}
void mm_filename_derive_type(const MMFilenameValue *v, const MMFilenameValueHost *h)
{
    MMI32 count, n = length(&v->path), i;
    MMU32 type = 0;
    validate(v, h, MM_FILENAME_BASE); assertion(h, n > 0, MM_FILENAME_EMPTY_PATH);
    if (h->separator(h->context, unit(&v->path, n))) v->state->type = MM_FILENAME_DIRECTORY;
    else {
        count = mm_filename_extension_length(v, h) - 1;
        for (i = n - count + 1; i <= n; ++i)
            type = (type << 8) | (MMU32)(h->upper(h->context, unit(&v->path, i) & 255U) & 255U);
        v->state->type = mm_signed_word(type);
    }
    validate(v, h, MM_FILENAME_FILE | MM_FILENAME_DIR);
}
int mm_filename_change_type(const MMFilenameValue *v, const MMFilenameValueHost *h, MMI32 type)
{
    NameBuffer buffer, dot;
    MMStringValue extension;
    MMI32 base;
    validate(v, h, MM_FILENAME_FILE);
    assertion(h, type != MM_FILENAME_NIL && type != MM_FILENAME_DIRECTORY, MM_FILENAME_BAD_TYPE);
    extension = v->path; extension.buffer = &buffer;
    type = mm_filename_clean_type(type, h, &extension);
    if (v->state->type == type) return 1;
    base = length(&v->path) - mm_filename_extension_length(v, h);
    if (base + length(&extension) >= 255) return 0;
    mm_string_value_delete(&v->path, base, 255); v->state->type = type;
    if (length(&extension) > 0) {
        put(&dot, v->path.unit, '.');
        mm_string_value_append(&v->path, &dot, 1);
        mm_string_value_append(&v->path, characters(&extension), length(&extension));
    }
    return 1;
}
static MMI32 leaf_start(const MMFilenameValue *v, const MMFilenameValueHost *h)
{
    MMI32 n = length(&v->path);
    while (n > 0 && !h->separator(h->context, unit(&v->path, n))) --n;
    assertion(h, n > 0, MM_FILENAME_BAD_PATH); return n;
}
int mm_filename_change_leaf(const MMFilenameValue *v, const MMFilenameValueHost *h, const void *p, MMI32 count)
{
    MMI32 base;
    int result;
    validate(v, h, MM_FILENAME_FILE | MM_FILENAME_DIR); base = leaf_start(v, h);
    mm_string_value_delete(&v->path, base, 255); v->state->type = MM_FILENAME_DIRECTORY;
    if (p && count > 0) {
        if (base + count > 255) return 0;
        result = mm_string_value_append(&v->path, p, count); assertion(h, result, MM_FILENAME_APPEND);
        mm_filename_derive_type(v, h);
    }
    validate(v, h, MM_FILENAME_FILE | MM_FILENAME_DIR); return 1;
}
int mm_filename_set_leaf(const MMFilenameValue *v, const MMFilenameValueHost *h, const void *p, MMI32 count, MMI32 type)
{
    validate(v, h, MM_FILENAME_FILE | MM_FILENAME_DIR); type = mm_filename_clean_type(type, h, NULL);
    assertion(h, (type == MM_FILENAME_DIRECTORY) == (!p || count == 0), MM_FILENAME_LEAF_TYPE);
    if (!mm_filename_change_leaf(v, h, p, count)) goto fail;
    if (type != MM_FILENAME_DIRECTORY && type != MM_FILENAME_NIL && !mm_filename_change_type(v, h, type)) goto fail;
    validate(v, h, MM_FILENAME_FILE | MM_FILENAME_DIR); return 1;
fail:
    mm_filename_value_clear(v, h); if (h->failed) h->failed(h->context); return 0;
}
void mm_filename_get_leaf(const MMFilenameValue *v, const MMFilenameValueHost *h, const MMStringValue *output)
{
    MMI32 base, count;
    validate(v, h, 0); base = leaf_start(v, h);
    count = 0;
    while (base + count < length(&v->path) && unit(&v->path, base + count + 1)) ++count;
    mm_string_value_set(output, (const unsigned char *)v->path.buffer + (size_t)(base + 1) * v->path.unit,
                        count);
}
