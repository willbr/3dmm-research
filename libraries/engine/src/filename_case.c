/* Filename case resolution converted from Kauai filename.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/filename_case.h"
#include "mm/string_ops.h"

typedef union CaseBuffer { unsigned char bytes[257]; unsigned short words[257]; } CaseBuffer;
typedef union EntryBuffer { unsigned char bytes[1024]; unsigned short words[1024]; } EntryBuffer;
static unsigned unit(const void *p, unsigned size, MMI32 i)
{ return size == 1 ? ((const unsigned char *)p)[i] : ((const unsigned short *)p)[i]; }
static void put(CaseBuffer *b, unsigned size, unsigned ch)
{ if (size == 1) b->bytes[0] = (unsigned char)ch; else b->words[0] = (unsigned short)ch; }
static MMI32 length(const MMStringValue *s) { return (MMI32)unit(s->buffer, s->unit, 0); }
static const void *at(const MMStringValue *s, MMI32 i) { return (const unsigned char *)s->buffer + (size_t)i * s->unit; }
static unsigned fold(unsigned ch) { return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch; }
int mm_filename_resolve_case(const MMFilenameValue *v, const MMFilenameCaseHost *h)
{
    CaseBuffer resolved_buffer, component_buffer, candidate_buffer, pattern_buffer, match_buffer, character;
    EntryBuffer entry;
    MMStringValue resolved, component, candidate, pattern, match;
    MMFilenameInfo info;
    MMI32 start, end, count, i;
    int status, iteration, matches, equal;
    void *directory;
    if (h->path.value.validate)
        h->path.value.validate(h->path.value.context, v, MM_FILENAME_FILE | MM_FILENAME_DIR);
    status = h->path.info(h->path.context, at(&v->path, 1), &info);
    if (status == MM_FILENAME_FOUND) return 1;
    if (status != MM_FILENAME_MISSING || !h->sensitive || unit(v->path.buffer, v->path.unit, 1) != '/') return 0;
    resolved = component = candidate = pattern = match = v->path;
    resolved.buffer = &resolved_buffer; component.buffer = &component_buffer; candidate.buffer = &candidate_buffer;
    pattern.buffer = &pattern_buffer; match.buffer = &match_buffer;
    put(&character, v->path.unit, '/'); mm_string_value_set(&resolved, &character, 1);
    for (start = 1; start < length(&v->path); start = end + 1) {
        end = start;
        while (end < length(&v->path) && unit(v->path.buffer, v->path.unit, end + 1) != '/') ++end;
        mm_string_value_set(&component, at(&v->path, start + 1), end - start);
        mm_string_value_copy(&candidate, resolved.buffer);
        if (!mm_string_value_append(&candidate, at(&component, 1), length(&component))) return 0;
        status = h->path.info(h->path.context, at(&candidate, 1), &info);
        if (status == MM_FILENAME_INFO_ERROR) return 0;
        if (status == MM_FILENAME_MISSING) {
            mm_string_value_copy(&pattern, resolved.buffer); put(&character, v->path.unit, '*');
            if (!mm_string_value_append(&pattern, &character, 1)) return 0;
            directory = 0; matches = 0;
            iteration = h->first(h->path.context, at(&pattern, 1), &directory, &entry, MM_FILENAME_CASE_NAME_CAPACITY);
            while (iteration == MM_FILENAME_CASE_ENTRY) {
                count = h->name_length ? h->name_length(h->path.context, &entry) :
                                        (MMI32)mm_string_zero_length(&entry, v->path.unit);
                equal = count == length(&component);
                for (i = 0; equal && i < count; ++i)
                    equal = fold(unit(&entry, v->path.unit, i)) == fold(unit(component.buffer, v->path.unit, i + 1));
                if (equal) {
                    mm_string_value_set(&match, &entry, count);
                    if (++matches > 1) break;
                }
                iteration = h->next(h->path.context, directory, &entry, MM_FILENAME_CASE_NAME_CAPACITY);
            }
            h->close(h->path.context, directory);
            if (matches != 1 || iteration == MM_FILENAME_CASE_ERROR) return 0;
            mm_string_value_copy(&candidate, resolved.buffer);
            if (!mm_string_value_append(&candidate, at(&match, 1), length(&match))) return 0;
        }
        mm_string_value_copy(&resolved, candidate.buffer);
        if (end < length(&v->path)) {
            put(&character, v->path.unit, '/'); if (!mm_string_value_append(&resolved, &character, 1)) return 0;
        }
    }
    mm_string_value_copy(&v->path, resolved.buffer); mm_filename_derive_type(v, &h->path.value); return 1;
}
