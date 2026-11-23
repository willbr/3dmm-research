/* Movie title policy converted from engine movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/movie_title.h"
#include "mm/chunk_owner.h"
#include "mm/resource_cache.h"
#include <string.h>

static int read_live_title(void *cache, MMU32 type, MMU32 number, void *wire, MMU32 capacity, MMU32 *written)
{
    return mm_chunk_owner_title_read(mm_resource_cache_reader(cache), type, number, wire, capacity, written);
}

static int write_live_title(void *cache, MMU32 type, MMU32 number, const void *wire, MMU32 bytes)
{
    return mm_chunk_owner_title_write(mm_resource_cache_reader(cache), type, number, wire, bytes);
}

MMChunkTitleIO mm_resource_cache_title_io(MMChunkTitleIO io)
{
    io.read = read_live_title;
    io.write = write_live_title;
    return io;
}

int mm_movie_title_init(MMMovieTitle *t, unsigned unit)
{
    if (!t || (unit != 1 && unit != 2))
        return 0;
    memset(t, 0, sizeof(*t));
    t->unit = unit;
    return 1;
}
int mm_movie_title_set(MMMovieTitle *t, const void *text, MMU32 count)
{
    if (!t || (t->unit != 1 && t->unit != 2) || count > 255 || (!text && count))
        return 0;
    if (count)
        memmove(t->text, text, count * t->unit);
    memset((unsigned char *)t->text + count * t->unit, 0, t->unit);
    t->count = count;
    return 1;
}
const void *mm_movie_title_text(const MMMovieTitle *t)
{
    return t->text;
}
static MMStringWire title_wire(const MMMovieTitle *t, const MMChunkTitleIO *io, void *buffer)
{
    const unsigned short one = 1;
    MMStringWire wire;
    wire.buffer = buffer;
    wire.unit = t->unit;
    wire.current_encoding = io->encoding;
    wire.swapped_unicode_encoding = io->swapped_unicode_encoding;
    wire.native_big_endian = *(const unsigned char *)&one == 0;
    wire.context = io->context;
    wire.convert = io->convert;
    return wire;
}
int mm_movie_title_read_wire(MMMovieTitle *t, const void *bytes, MMU32 size, const MMChunkTitleIO *io)
{
    unsigned short counted[MM_STRING_MAX + 2];
    MMStringWire wire = title_wire(t, io, counted);
    MMMemoryStream stream;
    MMU32 count;
    if (size <= 2 + sizeof(counted) && mm_memory_stream_init(&stream, bytes, size) &&
        mm_string_wire_read(&wire, &stream.stream, 0, NULL))
    {
        count = t->unit == 1 ? *(const unsigned char *)counted : counted[0];
        return mm_movie_title_set(t, (const unsigned char *)counted + t->unit, count);
    }
    if (io->bad_string)
        io->bad_string(io->context);
    mm_movie_title_set(t, NULL, 0);
    return 0;
}
void mm_chunk_title_read(MMMovieTitle *t, void *file, MMU32 type, MMU32 number, const MMChunkTitleIO *io)
{
    unsigned char bytes[2 + (MM_STRING_MAX + 2) * sizeof(unsigned short)];
    MMU32 size;
    if (io->read(file, type, number, bytes, sizeof(bytes), &size))
        mm_movie_title_read_wire(t, bytes, size, io);
    else
        mm_movie_title_set(t, NULL, 0);
}
int mm_chunk_title_write(const MMMovieTitle *t, void *file, MMU32 type, MMU32 number, const MMChunkTitleIO *io)
{
    unsigned short counted[MM_STRING_MAX + 2];
    unsigned char bytes[2 + sizeof(counted)];
    unsigned char *text;
    MMStringWire wire = title_wire(t, io, counted);
    MMU32 size;
    if ((t->unit != 1 && t->unit != 2) || t->count > MM_STRING_MAX)
        return 0;
    if (!t->count)
        return io->write(file, type, number, NULL, 0);
    text = (unsigned char *)counted + t->unit;
    if (t->unit == 1)
        *(unsigned char *)counted = (unsigned char)t->count;
    else
        counted[0] = (unsigned short)t->count;
    memcpy(text, t->text, t->count * t->unit);
    memset(text + t->count * t->unit, 0, t->unit);
    size = mm_string_wire_size(&wire);
    return mm_string_wire_get(&wire, bytes, sizeof(bytes)) && io->write(file, type, number, bytes, size);
}
static unsigned character(const MMMovieTitle *t, MMU32 i)
{
    unsigned short value;
    if (t->unit == 1)
        return ((const unsigned char *)t->text)[i];
    memcpy(&value, (const unsigned char *)t->text + i * 2, 2);
    return value;
}
void mm_movie_title_strip_extension(MMMovieTitle *t)
{
    MMU32 i = t->count;
    while (i && character(t, i) != '.')
        --i;
    if (character(t, i) == '.')
    {
        t->count = i;
        memset((unsigned char *)t->text + i * t->unit, 0, t->unit);
    }
}
void mm_movie_title_choose(MMMovieTitle *t, int file, const MMMovieTitleServices *s)
{
    if (!file)
    {
        if (!t->count)
        {
            s->default_name(s->context, t);
            s->notify(s->context, t);
        }
        return;
    }
    s->leaf(s->context, t);
    mm_movie_title_strip_extension(t);
    if (!t->count)
        s->default_name(s->context, t);
    s->notify(s->context, t);
}
