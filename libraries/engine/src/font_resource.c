/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/font_resource.h"
#include "mm/movie_data.h"
#include <stdlib.h>
#include <string.h>
static MMU32 word(const unsigned char *p,int big)
{ MMU32 value=0; int i; for (i=0;i<4;++i) value=(value<<8)|p[big?i:3-i]; return value; }
static MMI32 signed_word(const unsigned char *p,int big)
{ MMU32 bits=word(p,big); MMI32 value; memcpy(&value,&bits,4); return value; }
static void put(unsigned char *p,int bytes,MMU32 value,int big)
{ int i; for (i=0;i<bytes;++i) { p[big?bytes-1-i:i]=(unsigned char)value; value>>=8; } }

static int allocate_metrics(const MMStorageAllocator *allocator, MMU32 bytes, int clear, void **out)
{
    void *metrics = NULL;
    if (allocator)
        metrics = allocator->resize(allocator->context, NULL, 0, bytes, MM_STORAGE_NORMAL);
    else if (bytes)
        metrics = malloc(bytes);
    if (!metrics && (allocator || bytes))
        return 0;
    if (clear && bytes)
        memset(metrics, 0, bytes);
    *out = metrics;
    return 1;
}

static void release_metrics(const MMStorageAllocator *allocator, void **metrics, MMU32 bytes)
{
    if (!*metrics)
        return;
    if (allocator)
        allocator->release(allocator->context, *metrics, bytes);
    else
        free(*metrics);
    *metrics = NULL;
}

int mm_font_fetch(MMOwnedBlock *block, void *out, void *size, const MMFontResourceServices *s)
{
    MMI32 base = s->object_bytes;
    MMI32 bytes = block->view.packed ? base + 2048 : (MMI32)block->view.length;
    void *object;
    MMFontResource *font;
    memcpy(size, &bytes, sizeof(bytes));
    if (!out)
        return 1;
    object = s->create(s->context);
    if (!object)
        goto fail;
    font = s->state(s->context, object);
    if (!mm_font_initialize(font, block, s))
        goto fail;
    if (base < 0 || font->count > (0x7fffffffL - base) / 8)
    {
        s->bad_font(s->context);
        goto fail;
    }
    bytes = base + font->count * 8;
    memcpy(size, &bytes, sizeof(bytes));
    memcpy(out, &object, sizeof(object));
    return 1;
fail:
    s->invalidate(s->context, out, sizeof(object));
    s->invalidate(s->context, size, sizeof(bytes));
    if (object)
        s->release(s->context, object);
    return 0;
}
int mm_font_initialize(MMFontResource *font, MMOwnedBlock *block, const MMFontResourceServices *s)
{
    MMBlockIO io;
    MMReadStream stream;
    return mm_owned_block_code(block, 0, s->context, s->decode, NULL) == MM_DATA_OK &&
           mm_owned_block_io(block, &io) == MM_DATA_OK && mm_block_io_stream(&io, &stream) == MM_DATA_OK &&
           mm_font_read_stream(font, &stream, s) == MM_MOVIE_OK;
}
int mm_font_read_stream(MMFontResource *font, const MMReadStream *stream, const MMFontResourceServices *s)
{
    unsigned char header[12];
    MMI32 bytes;
    int big, current;
    if (stream->length < sizeof(header))
        goto bad_font;
    if (!mm_read_exact(stream, 0, header, sizeof(header)))
        return MM_MOVIE_IO;
    current=s->host_big_endian?(!header[0] && header[1]==1):(header[0]==1 && !header[1]);
    big=current?s->host_big_endian:!s->host_big_endian;
    if ((!big && (header[0]!=1 || header[1])) || (big && (header[0] || header[1]!=1)))
        if (s->bad_order) s->bad_order(s->context);
    font->count=signed_word(header+4,big);
    if (font->count<0 || font->count>0x0ffffffeL)
        goto bad_font;
    bytes=font->count*4;
    if (stream->length != sizeof(header) + (MMU32)bytes * 2)
        goto bad_font;
    font->maximum_height=signed_word(header+8,big);
    if (!allocate_metrics(s->allocator, bytes, 0, &font->widths))
        return MM_MOVIE_MEMORY;
    if (!mm_read_exact(stream, sizeof(header), font->widths, (size_t)bytes))
        return MM_MOVIE_IO;
    /* The old code tested the already normalized bo here, so foreign arrays
     * were NOT swapped. Preserve that defined legacy result explicitly. */
    if (!allocate_metrics(s->allocator, bytes, 0, &font->heights))
        return MM_MOVIE_MEMORY;
    return mm_read_exact(stream, sizeof(header) + (MMU32)bytes, font->heights, (size_t)bytes) ? MM_MOVIE_OK : MM_MOVIE_IO;
bad_font:
    s->bad_font(s->context);
    return MM_MOVIE_BAD_FILE;
}
void mm_font_dispose(MMFontResource *font,const MMFontResourceServices *s)
{
    MMU32 bytes = (MMU32)font->count * 4;
    release_metrics(s->allocator, &font->widths, bytes);
    release_metrics(s->allocator, &font->heights, bytes);
}
MMI32 mm_font_width(const MMFontResource *font,MMI32 i)
{ MMI32 value; memcpy(&value,(const unsigned char *)font->widths+i*4,4); return value; }
MMI32 mm_font_height(const MMFontResource *font,MMI32 i)
{ MMI32 value; memcpy(&value,(const unsigned char *)font->heights+i*4,4); return value; }
int mm_font_author(MMResourceCache *cache, const MMChunkEdit *edit, void *children, void *out,
                   const MMFontResourceServices *s)
{
    MMI32 count,i,letter=-1,n,width,height,maximum=0;
    MMU32 largest=0,position,number=0,bytes=0;
    void *widths=NULL,*heights=NULL,*model=NULL;
    MMChunkChild child,replacement;
    MMChunkLocation location;
    MMBlockFile extent;
    MMFontChunk chunk;
    MMOwnedBlock block;
    MMBlockIO io;
    unsigned char header[12]; int big=s->host_big_endian;
    int ok = 0, initialized = mm_owned_block_init(&block, cache->services.handles, cache->services.block_files) == MM_DATA_OK;
    if (!cache->file || !initialized)
        goto done;
    count=s->child_count(s->context,children);
    for (i=0;i<count;++i)
    {
        s->child_at(s->context,children,i,&child); if (child.slot>largest) largest=child.slot;
        if (child.slot==105) letter=i;
    }
    if (largest>=0x0ffffffeUL) goto done;
    n=(MMI32)largest+1; bytes=n*4;
    if (!allocate_metrics(s->allocator,bytes,1,&widths) || !allocate_metrics(s->allocator,bytes,1,&heights))
        goto done;
    if (mm_chunk_unique(edit, MM_FONT_TYPE, &position, &number) != MM_CHUNK_OK ||
        mm_chunk_add(edit, MM_FONT_TYPE, number, position, 12 + bytes + bytes, &location) != MM_CHUNK_OK)
        goto done;
    extent.file = location.file;
    extent.offset = location.offset;
    extent.length = location.bytes;
    if (mm_owned_block_set_file(&block, &extent, 0) != MM_DATA_OK)
        goto done;
    for (i=0;i<count;++i)
    {
        s->child_at(s->context,children,i,&child);
        model = mm_resource_cache_fetch_chunk(cache, child.type, child.number, s->model_reader);
        if (!model) goto done;
        if (mm_chunk_adopt(edit, MM_FONT_TYPE, number, &child, 1) != MM_CHUNK_OK) goto done;
        if (!s->model_width(s->context,model) && child.slot==32 && letter!=-1)
        {
            s->release(s->context,model); model=NULL;
            s->child_at(s->context,children,letter,&replacement);
            model = mm_resource_cache_fetch_chunk(cache, replacement.type, replacement.number, s->model_reader);
            if (!model) goto done;
        }
        width=s->model_width(s->context,model)+s->spacing; height=s->model_height(s->context,model)+s->leading;
        memcpy((unsigned char *)widths+child.slot*4,&width,4); memcpy((unsigned char *)heights+child.slot*4,&height,4);
        if (height>maximum) maximum=height;
        s->release(s->context,model); model=NULL;
    }
    put(header,2,1,big); put(header+2,2,s->system,big); put(header+4,4,(MMU32)n,big); put(header+8,4,(MMU32)maximum,big);
    ok = mm_owned_block_io(&block, &io) == MM_DATA_OK &&
         mm_block_write(&io, 0, header, sizeof(header), 0) == MM_DATA_OK &&
         mm_block_write(&io, sizeof(header), widths, (MMU32)bytes, 0) == MM_DATA_OK &&
         mm_block_write(&io, sizeof(header) + (MMU32)bytes, heights, (MMU32)bytes, 0) == MM_DATA_OK;
done:
    if (model) s->release(s->context,model);
    release_metrics(s->allocator, &widths, bytes);
    release_metrics(s->allocator, &heights, bytes);
    if (out)
    {
        if (ok)
        {
            chunk.type = MM_FONT_TYPE;
            chunk.number = number;
            memcpy(out, &chunk, sizeof(chunk));
        }
        else
            s->invalidate(s->context, out, sizeof(chunk));
    }
    if (initialized)
        mm_owned_block_clear(&block);
    return ok;
}
void *mm_font_model(const MMResourceObject *font, MMU32 id, const MMFontResourceServices *s)
{
    MMResourceCache *cache = font->cache;
    const MMChunkAccess chunks = mm_resource_cache_chunks(cache);
    MMU32 parent;
    MMChunkChild child;
    if (!cache || mm_chunk_find(&chunks, font->type, font->number, &parent) != MM_CHUNK_OK ||
        mm_chunk_child_type(&chunks, parent, id, 0, &child) != MM_CHUNK_OK)
    {
        s->missing_model(s->context,id);
        return NULL;
    }
    return mm_resource_cache_fetch_chunk(cache, child.type, child.number, s->model_reader);
}
