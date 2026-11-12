#include "three_mm/chunk_save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int value, int line)
{
    if (!value)
    {
        fprintf(stderr, "C89 chunk save failed at line %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct File
{
    int refs, temporary;
    ThreeMMU32 size;
    unsigned char bytes[1024];
} File;
typedef struct Fixture
{
    ThreeMMChunkSaveState state, mirror;
    ThreeMMChunkSaveIndex index;
    ThreeMMChunkSaveServices io;
    ThreeMMChunkEntry entries[4], original[4];
    File main, extra, output[4];
    int main_map, extra_map, fail, created, copies, indexes, renames, swaps, places, published;
} Fixture;
static void entry(void *context, ThreeMMU32 position, ThreeMMChunkEntry *out)
{
    Fixture *f = (Fixture *)context;
    CHECK(position < f->index.count);
    *out = f->entries[position];
}
static void place(void *context, ThreeMMU32 position, ThreeMMU32 offset, ThreeMMU32 flags)
{
    Fixture *f = (Fixture *)context;
    CHECK(f->copies == 3); /* No index edits before every nonempty copy succeeds. */
    ++f->places;
    f->entries[position].offset = offset;
    f->entries[position].flags = flags;
}
static void *create(void *context)
{
    Fixture *f = (Fixture *)context;
    File *out;
    if (f->fail == 1)
        return NULL;
    CHECK(f->created < 4);
    out = f->output + f->created++;
    out->refs = 1;
    out->temporary = 1;
    return out;
}
static int resize(void *context, void *file, ThreeMMU32 bytes)
{
    Fixture *f = (Fixture *)context;
    File *out = (File *)file;
    CHECK(bytes == 128 && out->refs == 1);
    if (f->fail == 2)
        return 0;
    out->size = bytes;
    return 1;
}
static int copy(void *context, void *source, ThreeMMU32 from, void *destination, ThreeMMU32 to, ThreeMMU32 bytes)
{
    Fixture *f = (Fixture *)context;
    File *in = (File *)source, *out = (File *)destination;
    CHECK(in->refs == 1 && out->refs == 1 && from + bytes <= in->size && to + bytes <= sizeof(out->bytes));
    CHECK(!f->places);
    ++f->copies;
    memcpy(out->bytes + to, in->bytes + from, bytes); /* Failure may have written payload. */
    out->size = to + bytes;
    return f->fail != 2 + f->copies;
}
static void release_file(void *context, void *file)
{
    Fixture *f = (Fixture *)context;
    File *native = (File *)file;
    CHECK(f->mirror.main_file != file && f->mirror.extra_file != file);
    CHECK(native->refs == 1);
    --native->refs;
}
static void release_map(void *context, void *map)
{
    Fixture *f = (Fixture *)context;
    CHECK(f->mirror.main_map != map && f->mirror.extra_map != map);
    CHECK(*(int *)map == 1);
    --*(int *)map;
}
static int is_temp(void *context, void *file)
{
    (void)context;
    CHECK(file && ((File *)file)->refs == 1);
    return ((File *)file)->temporary;
}
static void set_temp(void *context, void *file, int temporary)
{
    (void)context;
    CHECK(file && ((File *)file)->refs == 1);
    ((File *)file)->temporary = temporary;
}
static void publish(void *context, const ThreeMMChunkSaveState *state)
{
    Fixture *f = (Fixture *)context;
    f->mirror = *state;
    ++f->published;
}
static int write_index(void *context, const ThreeMMChunkSaveState *state, ThreeMMU32 creator)
{
    Fixture *f = (Fixture *)context;
    unsigned i;
    ThreeMMU32 offset = 128;
    CHECK(creator == 0x53415645UL);
    CHECK(!memcmp(&f->mirror, state, sizeof(*state)));
    ++f->indexes;
    if (f->created)
    {
        for (i = 0; i < f->index.count; ++i)
            offset += f->entries[i].bytes;
        CHECK(state->main_file == f->output + f->created - 1 && state->main_end == offset);
        offset = 128;
        CHECK(!state->extra_file && !state->main_map && !state->extra_map && !state->free_map_unread);
        for (i = 0; i < f->index.count; ++i)
        {
            CHECK(f->entries[i].offset == (f->entries[i].bytes ? offset : 0));
            CHECK(f->entries[i].flags == (f->original[i].flags & ~1U));
            offset += f->entries[i].bytes;
        }
    }
    return f->fail != 6;
}
static int rename_file(void *context, void *file)
{
    Fixture *f = (Fixture *)context;
    CHECK(f->indexes == 1 && f->mirror.main_file == file);
    ++f->renames;
    return f->fail != 7;
}
static int swap(void *context, void *file, void *old)
{
    Fixture *f = (Fixture *)context;
    CHECK(f->indexes == 1 && f->mirror.main_file == file && old == &f->main);
    ++f->swaps;
    return f->fail != 8;
}
static void init(Fixture *f, int invalid, int temporary)
{
    unsigned i;
    memset(f, 0, sizeof(*f));
    f->main.refs = f->extra.refs = f->main_map = f->extra_map = 1;
    f->main.temporary = temporary;
    f->main.size = f->extra.size = 256;
    for (i = 0; i < 256; ++i)
    {
        f->main.bytes[i] = (unsigned char)(i * 7);
        f->extra.bytes[i] = (unsigned char)(i * 13);
    }
    f->state.main_file = &f->main;
    f->state.extra_file = &f->extra;
    f->state.main_map = &f->main_map;
    f->state.extra_map = &f->extra_map;
    f->state.main_end = f->state.extra_end = 256;
    f->state.invalid_main = invalid;
    f->state.add_to_extra = f->state.free_map_unread = 1;
    f->mirror = f->state;
    f->index.context = f;
    f->index.count = 4;
    f->index.entry = entry;
    f->index.place = place;
    f->io.context = f;
    f->io.create_temp = create;
    f->io.resize = resize;
    f->io.copy = copy;
    f->io.release_file = release_file;
    f->io.release_map = release_map;
    f->io.is_temp = is_temp;
    f->io.set_temp = set_temp;
    f->io.write_index = write_index;
    f->io.rename = rename_file;
    f->io.swap_names = swap;
    f->io.publish = publish;
    f->entries[0].offset = 40;
    f->entries[0].bytes = 3;
    f->entries[0].flags = 6;
    f->entries[1].offset = 777;
    f->entries[1].flags = 19; /* Empty on-extra becomes offset zero. */
    f->entries[2].offset = 10;
    f->entries[2].bytes = 5;
    f->entries[2].flags = 5;
    f->entries[3].offset = 90;
    f->entries[3].bytes = 2;
    f->entries[3].flags = 24;
    memcpy(f->original, f->entries, sizeof(f->entries));
}
static void matrix(void)
{
    Fixture f;
    ThreeMMChunkSaveState before;
    int named, invalid, temporary, fail, result;
    unsigned i;
    for (named = 0; named < 2; ++named)
        for (invalid = 0; invalid < 2; ++invalid)
            for (temporary = 0; temporary < 2; ++temporary)
                for (fail = 0; fail < 9; ++fail)
                {
                    if ((invalid && !named) || (named && fail == 8) || (!named && fail == 7))
                        continue;
                    init(&f, invalid, temporary);
                    f.fail = fail;
                    before = f.state;
                    result = three_mm_chunk_save(&f.state, &f.index, &f.io, 0x53415645UL, named);
                    CHECK(result == (!fail       ? THREE_MM_CHUNK_OK
                                     : fail == 1 ? THREE_MM_CHUNK_MEMORY
                                                 : THREE_MM_CHUNK_IO));
                    if (fail && fail <= 5)
                    {
                        CHECK(!memcmp(&before, &f.state, sizeof(before)) &&
                              !memcmp(f.entries, f.original, sizeof(f.entries)));
                        CHECK(f.main.refs == 1 && f.extra.refs == 1 && f.main_map == 1 && f.extra_map == 1);
                        CHECK(!f.created || f.output[0].refs == 0);
                        CHECK(!f.indexes && !f.renames && !f.swaps && !f.places && !f.published);
                        continue;
                    }
                    CHECK(f.extra.refs == 0 && !f.main_map && !f.extra_map && !f.state.free_map_unread);
                    CHECK(f.state.add_to_extra == 1 && f.output[0].refs == 1);
                    CHECK(!memcmp(f.output[0].bytes + 128, f.main.bytes + 40, 3));
                    CHECK(!memcmp(f.output[0].bytes + 131, f.extra.bytes + 10, 5));
                    CHECK(!memcmp(f.output[0].bytes + 136, f.main.bytes + 90, 2));
                    if (fail && !invalid)
                    {
                        CHECK(f.state.main_file == &f.main && f.state.main_end == 128 && f.main.refs == 1);
                        CHECK(f.state.extra_file == f.output && f.state.extra_end == 138 && f.output[0].temporary);
                        for (i = 0; i < 4; ++i)
                            CHECK(f.entries[i].flags == ((f.original[i].flags & ~1U) | (f.entries[i].bytes ? 1U : 0U)));
                    }
                    else
                    {
                        CHECK(f.state.main_file == f.output && f.state.main_end == 138 && !f.state.extra_file);
                        CHECK(!f.main.refs && f.state.invalid_main == (fail ? invalid : 0));
                        CHECK(f.output[0].temporary == (invalid || temporary));
                        if (!named)
                            CHECK(f.main.temporary);
                    }
                    CHECK(f.renames == (named && fail != 6) && f.swaps == (!named && fail != 6));
                    if (fail)
                    {
                        /* Retry against recovered extra storage, or the retained
                         * temporary for a main file without a valid CHN2 header. */
                        f.fail = f.copies = f.places = f.indexes = f.renames = f.swaps = 0;
                        CHECK(three_mm_chunk_save(&f.state, &f.index, &f.io, 0x53415645UL, named) == THREE_MM_CHUNK_OK);
                        CHECK(f.created == 2 && f.state.main_file == f.output + 1 && !f.state.invalid_main);
                        CHECK(!f.output[0].refs && f.output[1].refs == 1 && !f.main.refs && !f.extra.refs);
                        CHECK(!memcmp(f.output[0].bytes + 128, f.output[1].bytes + 128, 10));
                    }
                }
}
static void validation(void)
{
    Fixture f;
    ThreeMMChunkSaveState before;
    int test;
    for (test = 0; test < 6; ++test)
    {
        init(&f, 0, 0);
        if (test == 0)
            f.entries[0].offset = 0x80000000UL;
        if (test == 1)
            f.entries[0].bytes = 0x7fffffffUL;
        if (test == 2)
        {
            f.entries[0].offset = 0;
            f.entries[0].bytes = 0x7fffffffUL - 128;
        }
        if (test == 3)
            f.io.copy = NULL;
        if (test == 4)
            f.io.rename = NULL;
        if (test == 5)
        {
            f.state.invalid_main = 1;
        }
        before = f.state;
        CHECK(three_mm_chunk_save(&f.state, &f.index, &f.io, 0x53415645UL, test != 5) == THREE_MM_CHUNK_BAD_FILE);
        CHECK(!f.created && !f.places && !memcmp(&before, &f.state, sizeof(before)));
    }
    init(&f, 0, 0);
    f.state.extra_file = NULL;
    f.state.add_to_extra = 0;
    f.state.free_map_unread = 0;
    f.mirror = f.state;
    memset(&f.io, 0, sizeof(f.io));
    f.io.context = &f;
    f.io.write_index = write_index;
    CHECK(three_mm_chunk_save(&f.state, &f.index, &f.io, 0x53415645UL, 0) == THREE_MM_CHUNK_OK);
    CHECK(f.indexes == 1 && !f.created && !f.places && f.main_map == 1);
    f.fail = 6;
    CHECK(three_mm_chunk_save(&f.state, &f.index, &f.io, 0x53415645UL, 0) == THREE_MM_CHUNK_IO);
    CHECK(f.indexes == 2 && f.main_map == 1);
    CHECK(three_mm_chunk_save(NULL, &f.index, &f.io, 0, 0) == THREE_MM_CHUNK_BAD_FILE);
    CHECK(three_mm_chunk_save(&f.state, NULL, &f.io, 0, 0) == THREE_MM_CHUNK_BAD_FILE);
    CHECK(three_mm_chunk_save(&f.state, &f.index, NULL, 0, 0) == THREE_MM_CHUNK_BAD_FILE);
    init(&f, 1, 0);
    f.index.count = 0; /* Empty forest, with no valid old main reference. */
    f.state.main_file = NULL;
    f.main.refs = 0;
    f.mirror = f.state;
    CHECK(three_mm_chunk_save(&f.state, &f.index, &f.io, 0x53415645UL, 1) == THREE_MM_CHUNK_OK);
    CHECK(f.state.main_end == 128 && f.state.main_file == f.output && !f.state.invalid_main);
    CHECK(!f.copies && !f.places && f.output[0].temporary && !f.extra.refs);
}
int main(void)
{
    matrix();
    validation();
    return 0;
}
