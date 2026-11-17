/* Filename path control converted from Kauai filename.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/filename_path.h"
#include <stddef.h>
typedef union PathBuffer { unsigned char bytes[257]; unsigned short words[257]; } PathBuffer;
static unsigned unit(const MMStringValue *s, MMI32 i)
{ return s->unit == 1 ? ((const unsigned char *)s->buffer)[i] : ((const unsigned short *)s->buffer)[i]; }
static MMI32 length(const MMStringValue *s) { return (MMI32)unit(s, 0); }
static const void *at(const MMStringValue *s, MMI32 i)
{ return (const unsigned char *)s->buffer + (size_t)i * s->unit; }
static const void *characters(const MMStringValue *s) { return at(s, 1); }
static void put(PathBuffer *b, unsigned size, int i, unsigned ch)
{ if (size == 1) b->bytes[i] = (unsigned char)ch; else b->words[i] = (unsigned short)ch; }
static void validate(const MMFilenameValue *v, const MMFilenamePathHost *h, int flags)
{ if (h->value.validate) h->value.validate(h->value.context, v, flags); }
static void assertion(const MMFilenamePathHost *h, int valid, int code)
{ if (!valid && h->assertion) h->assertion(h->context, code); }
static void issue(const MMFilenameValue *v, const MMFilenamePathHost *h, int code)
{ if (h->issue) h->issue(h->context, v, code); }
static void copy(const MMFilenameValue *dst, const MMFilenameValue *src, const MMFilenamePathHost *h)
{
    if (h->copy_name) h->copy_name(h->context, dst, src);
    else { dst->state->type = src->state->type; mm_string_value_copy(&dst->path, src->path.buffer); }
}
int mm_filename_build(const MMFilenameValue *v, const MMFilenamePathHost *h, const MMStringValue *input, MMI32 type)
{
    PathBuffer buffer, slash;
    MMI32 n;
    validate(v, h, 0);
    if (type != MM_FILENAME_DIRECTORY) {
        n = length(input);
        for (;;) {
            if (!n || h->value.separator(h->value.context, unit(input, n))) { type = MM_FILENAME_DIRECTORY; break; }
            if (unit(input, n--) != '.') break;
        }
    }
    n = h->full_path(h->context, characters(input), &buffer, 255);
    if (!n || n > 255) goto fail;
    mm_string_value_set(&v->path, &buffer, n);
    if (type == MM_FILENAME_DIRECTORY) {
        if (!h->value.separator(h->value.context, unit(&v->path, length(&v->path)))) {
            put(&slash, v->path.unit, 0, h->separator(h->context));
            if (!mm_string_value_append(&v->path, &slash, 1)) goto fail;
        }
        v->state->type = MM_FILENAME_DIRECTORY;
    } else {
        mm_filename_derive_type(v, &h->value);
        if (!v->state->type && type != MM_FILENAME_NIL && unit(input, length(input)) != '.' &&
            !mm_filename_change_type(v, &h->value, type)) goto fail;
    }
    validate(v, h, MM_FILENAME_FILE | MM_FILENAME_DIR); return 1;
fail:
    mm_filename_value_clear(v, &h->value); issue(v, h, MM_FILENAME_GENERAL); return 0;
}
int mm_filename_search(const MMFilenameValue *v, const MMFilenamePathHost *h,
                       const MMStringValue *input, const MMStringValue *search)
{
    PathBuffer buffer;
    MMI32 n;
    validate(v, h, 0);
    n = h->find_path(h->context, characters(input), search ? characters(search) : NULL, &buffer, 255);
    if (!n || n > 255) {
        mm_filename_value_clear(v, &h->value); issue(v, h, MM_FILENAME_GENERAL); return 0;
    }
    mm_string_value_set(&v->path, &buffer, n); mm_filename_derive_type(v, &h->value);
    validate(v, h, MM_FILENAME_FILE | MM_FILENAME_DIR); return 1;
}
int mm_filename_exists(const MMFilenameValue *v, const MMFilenamePathHost *h)
{
    PathBuffer buffer;
    MMStringValue path;
    MMFilenameInfo info;
    MMI32 n;
    int status;
    validate(v, h, MM_FILENAME_FILE | MM_FILENAME_DIR); path = v->path;
    if (v->state->type == MM_FILENAME_DIRECTORY) {
        path.buffer = &buffer; mm_string_value_copy(&path, v->path.buffer); n = length(&path);
        assertion(h, n > 0 && h->value.separator(h->value.context, unit(&path, n)), MM_FILENAME_PATH_DIRECTORY);
        if (n > 1) mm_string_value_delete(&path, n - 1, 255);
    }
    status = h->info(h->context, characters(&path), &info);
    if (status != MM_FILENAME_FOUND) {
        if (status == MM_FILENAME_MISSING) return MM_FILENAME_NO;
        issue(v, h, MM_FILENAME_GENERAL); return MM_FILENAME_MAYBE;
    }
    if ((v->state->type == MM_FILENAME_DIRECTORY) != !!info.directory) {
        issue(v, h, MM_FILENAME_MISMATCH); return MM_FILENAME_MAYBE;
    }
    if (info.hidden || info.system) { issue(v, h, MM_FILENAME_HIDDEN); return MM_FILENAME_MAYBE; }
    return MM_FILENAME_YES;
}
int mm_filename_delete(const MMFilenameValue *v, const MMFilenamePathHost *h)
{
    validate(v, h, MM_FILENAME_FILE);
    if (h->assertion) assertion(h, !h->is_open(h->context, v), MM_FILENAME_PATH_OPEN);
    if (h->remove(h->context, characters(&v->path))) return 1;
    issue(v, h, MM_FILENAME_DELETE); return 0;
}
int mm_filename_rename(const MMFilenameValue *v, const MMFilenamePathHost *h, const MMFilenameValue *destination)
{
    validate(v, h, MM_FILENAME_FILE); validate(destination, h, MM_FILENAME_FILE);
    if (h->rename(h->context, characters(&v->path), characters(&destination->path))) return 1;
    issue(v, h, MM_FILENAME_RENAME); return 0;
}
int mm_filename_equal(const MMFilenameValue *v, const MMFilenamePathHost *h, const MMFilenameValue *other)
{
    validate(v, h, MM_FILENAME_FILE | MM_FILENAME_DIR); validate(other, h, MM_FILENAME_FILE | MM_FILENAME_DIR);
    return h->equal(h->context, characters(&other->path), characters(&v->path));
}
int mm_filename_same_directory(const MMFilenameValue *v, const MMFilenamePathHost *h, const MMFilenameValue *other,
                                const MMFilenameValue *a, const MMFilenameValue *b)
{
    validate(v, h, MM_FILENAME_FILE | MM_FILENAME_DIR); validate(other, h, MM_FILENAME_FILE | MM_FILENAME_DIR);
    copy(a, v, h); copy(b, other, h);
    mm_filename_change_leaf(a, &h->value, NULL, 0); mm_filename_change_leaf(b, &h->value, NULL, 0);
    return mm_filename_equal(a, h, b);
}
int mm_filename_down(const MMFilenameValue *v, const MMFilenamePathHost *h, const MMStringValue *leaf,
                     MMU32 flags, const MMFilenameValue *scratch)
{
    PathBuffer slash;
    int result;
    validate(v, h, MM_FILENAME_DIR); copy(scratch, v, h);
    if (length(&scratch->path) + length(leaf) + 1 > 255) { issue(v, h, MM_FILENAME_GENERAL); return 0; }
    result = mm_string_value_append(&scratch->path, characters(leaf), length(leaf));
    assertion(h, result, MM_FILENAME_PATH_APPEND); put(&slash, v->path.unit, 0, h->separator(h->context));
    result = mm_string_value_append(&scratch->path, &slash, 1); assertion(h, result, MM_FILENAME_PATH_APPEND);
    scratch->state->type = MM_FILENAME_DIRECTORY; validate(scratch, h, MM_FILENAME_DIR);
    if (mm_filename_exists(scratch, h) != MM_FILENAME_YES) {
        if (!(flags & MM_FILENAME_CREATE_DIR)) return 0;
        if (!h->make_directory(h->context, characters(&scratch->path))) { issue(v, h, MM_FILENAME_DIR_CREATE); return 0; }
    }
    if (flags & MM_FILENAME_MOVE_TO_DIR) copy(v, scratch, h);
    return 1;
}
int mm_filename_up(const MMFilenameValue *v, const MMFilenamePathHost *h, const MMStringValue *leaf, MMU32 flags)
{
    PathBuffer buffer, raw, dots, slash;
    MMStringValue path;
    MMI32 n, count;
    int result;
    validate(v, h, MM_FILENAME_DIR); path = v->path; path.buffer = &buffer;
    mm_string_value_copy(&path, v->path.buffer); put(&dots, path.unit, 0, '.'); put(&dots, path.unit, 1, '.');
    if (!mm_string_value_append(&path, &dots, 2)) return 0;
    n = h->full_path(h->context, characters(&path), &raw, 255);
    if (!n || n >= length(&v->path) - 1) return 0;
    mm_string_value_set(&path, &raw, n);
    if (!h->value.separator(h->value.context, unit(&path, n))) {
        put(&slash, path.unit, 0, h->separator(h->context));
        result = mm_string_value_append(&path, &slash, 1); assertion(h, result, MM_FILENAME_PATH_APPEND); ++n;
    }
    if (leaf) {
        count = 0; while (n + count < length(&v->path) && unit(&v->path, n + count + 1)) ++count;
        mm_string_value_set(leaf, at(&v->path, n + 1), count);
        mm_string_value_delete(leaf, length(leaf) - 1, 255);
    }
    if (flags & MM_FILENAME_MOVE_TO_DIR) { mm_string_value_copy(&v->path, path.buffer); validate(v, h, MM_FILENAME_DIR); }
    return 1;
}
MMU32 mm_filename_volume(const MMFilenameValue *v, const MMFilenamePathHost *h)
{
    validate(v, h, MM_FILENAME_FILE | MM_FILENAME_DIR);
    return h->volume_kinds(h->context, characters(&v->path));
}
