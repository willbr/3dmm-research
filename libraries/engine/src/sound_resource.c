/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/sound_resource.h"
#include "mm/actor_sound.h"
#include "mm/block_io.h"
#include "mm/midi.h"
#include "mm/movie_session.h"
#include <string.h>
static MMU32 word(const unsigned char *p,int n,int big)
{ MMU32 v=0; int i; for (i=0;i<n;++i) v=(v<<8)|p[big?i:n-1-i]; return v; }
static void put(unsigned char *p,int n,MMU32 v,int big)
{ int i; for (i=0;i<n;++i) { p[big?n-1-i:i]=(unsigned char)(v&255U); v>>=8; } }
typedef struct SoundHeaderRead
{
    MMSoundHeader header;
    int host_big_endian, loaded;
} SoundHeaderRead;

static int read_header(void *context, const MMReadStream *stream)
{
    SoundHeaderRead *read = (SoundHeaderRead *)context;
    MMSoundHeader *out = &read->header;
    unsigned char b[MM_SOUND_HEADER_BYTES];
    MMU32 value;
    int status = mm_movie_read_fixed(stream, b, sizeof(b));
    if (status != MM_MOVIE_OK)
        return status;
    value = word(b + 4, 4, read->host_big_endian);
    memcpy(&out->style, &value, 4);
    value = word(b + 8, 4, read->host_big_endian);
    memcpy(&out->volume, &value, 4);
    out->valid_order = (b[0] == 1 && b[1] == 0) || (b[0] == 0 && b[1] == 1);
    out->invalid = b[12] != 0;
    out->reserved_nonzero = (b[13] | b[14] | b[15]) != 0;
    read->loaded = 1;
    return MM_MOVIE_OK;
}

int mm_sound_read_header(const MMMovieSessionGraph *graph, const MMChunkEntry *entry, int host_big_endian,
                         MMSoundHeader *out)
{
    SoundHeaderRead read;
    int status;
    read.host_big_endian = host_big_endian;
    read.loaded = 0;
    status = graph->payload(graph->context, entry, read_header, &read);
    if (status != MM_MOVIE_OK)
        return status;
    if (!read.loaded)
        return MM_MOVIE_BAD_FILE;
    *out = read.header;
    return MM_MOVIE_OK;
}

static int find_sound(const MMMovieSessionGraph *graph, MMU32 type, MMU32 number,
                      MMChunkAccess *chunks, MMU32 *position, MMChunkEntry *entry)
{
    graph->access(graph->context, chunks);
    return mm_chunk_find(chunks, type, number, position) == MM_CHUNK_OK &&
           mm_chunk_at(chunks, *position, entry) == MM_CHUNK_OK;
}

int mm_sound_catalog_style(const MMMovieSessionGraph *graph, MMU32 number, int host_big_endian, MMI32 *style)
{
    MMChunkAccess chunks;
    MMChunkEntry entry;
    MMChunkChild child;
    MMSoundHeader header;
    MMU32 parent, i;
    if (!find_sound(graph, MM_SOUND_TYPE, number, &chunks, &parent, &entry) ||
        mm_sound_read_header(graph, &entry, host_big_endian, &header) != MM_MOVIE_OK ||
        header.invalid || header.style < MM_SOUND_STYLE_EFFECT || header.style > MM_SOUND_STYLE_MUSIC)
        return 0;
    for (i = 0; i < entry.child_count; ++i)
    {
        if (mm_chunk_child_at(&chunks, parent, i, &child) != MM_CHUNK_OK)
            return 0;
        if (child.type == MM_SOUND_WAVE || child.type == MM_SOUND_MIDI)
        {
            *style = header.style;
            return 1;
        }
    }
    return 0;
}

static void encode_header(unsigned char b[MM_SOUND_HEADER_BYTES], MMI32 style, MMI32 volume,
                          int invalid, int host_big_endian, MMU32 system)
{
    memset(b, 0, MM_SOUND_HEADER_BYTES);
    put(b, 2, 1, host_big_endian);
    put(b + 2, 2, system, host_big_endian);
    put(b + 4, 4, (MMU32)style, host_big_endian);
    put(b + 8, 4, (MMU32)volume, host_big_endian);
    b[12] = (unsigned char)(invalid != 0);
}
int mm_sound_read(void *object, void *file, MMU32 type, MMU32 number, const MMSoundResourceServices *s)
{
    MMMovieSessionGraph graph;
    MMChunkAccess chunks;
    MMChunkEntry entry;
    MMSoundHeader decoded;
    MMSoundOwner owner;
    MMChunkChild child;
    MMU32 position;
    s->graph(s->context, file, &graph);
    if (!find_sound(&graph, type, number, &chunks, &position, &entry) ||
        mm_sound_read_header(&graph, &entry, s->host_big_endian, &decoded) != MM_MOVIE_OK)
        return 0;
    if (!decoded.valid_order && s->bad_order)
        s->bad_order(s->context);
    if (!s->read_name(s->context, file, type, number, object))
        return 0;
    s->owner(s->context, object, &owner);
    owner.resource->style = decoded.style;
    owner.resource->volume = decoded.volume;
    *owner.invalid = (unsigned char)decoded.invalid;
    if (decoded.invalid)
        return 1;
    if (mm_chunk_child_type(&chunks, position, 0, 0, &child) == MM_CHUNK_OK)
    {
        owner.resource->number = child.number;
        owner.resource->type = child.type;
        *owner.silent = 0;
    }
    else
        *owner.silent = 1;
    return 1;
}
int mm_sound_fetch(void *cache,MMU32 type,MMU32 number,void *object_out,void *size_out,const MMSoundResourceServices *s)
{
    void *object; MMI32 bytes; MMSoundOwner o;
    bytes=s->object_size(s->context); memcpy(size_out,&bytes,4); if (!object_out) return 1;
    object=s->create(s->context);
    if (!object || !mm_sound_read(object,s->file(s->context,cache),type,number,s))
    {
        s->invalidate_output(s->context,object_out,sizeof(object)); s->invalidate_output(s->context,size_out,4);
        if (object) s->release(s->context,object); return 0;
    }
    s->owner(s->context,object,&o); o.resource->cache=cache; memcpy(object_out,&object,sizeof(object)); return 1;
}
int mm_sound_info(void *file,MMU32 type,MMU32 number,void *invalid_out,void *style_out,void *volume_out,const MMSoundResourceServices *s)
{
    void *object; MMSoundOwner o;
    object=s->create(s->context); if (!object) return 0;
    if (!mm_sound_read(object,file,type,number,s)) { s->release(s->context,object); return 0; }
    s->owner(s->context,object,&o);
    if (invalid_out) s->publish_invalid(s->context,invalid_out,*o.invalid!=0);
    if (style_out) memcpy(style_out,&o.resource->style,4);
    if (volume_out) memcpy(volume_out,&o.resource->volume,4);
    s->release(s->context,object); return 1;
}
int mm_sound_write(const MMReadStream *payload, int wave, MMI32 style, const void *name,
                   MMU32 *number, const MMSoundWriteServices *s)
{
    unsigned char header[MM_SOUND_HEADER_BYTES];
    MMStorageWriter output;
    if (!payload || !payload->read_at || payload->length >= MM_STORAGE_LIMIT)
        return 0;
    encode_header(header, wave ? style : MM_SOUND_STYLE_MUSIC, 65536L, 0, s->host_big_endian, s->system);
    if (!s->add_parent(s->context, header, number))
        return 0;
    if (s->add_child(s->context, *number, 0, wave ? MM_SOUND_WAVE : MM_SOUND_MIDI, payload->length, &output) &&
        mm_stream_copy(payload, &output, s->transfer_allocator) == MM_DATA_OK &&
        s->set_name(s->context, *number, name))
        return 1;
    s->delete_parent(s->context, *number);
    return 0;
}

int mm_sound_import_midi(const MMReadStream *source, const void *name, MMU32 *number, const MMSoundWriteServices *s)
{
    MMCollectionStorage input, decoded;
    MMMemoryStream payload;
    MMU32 bytes;
    int parsed, result = MM_SOUND_BAD_MIDI;
    if (!source || !source->length || source->length >= MM_STORAGE_LIMIT)
        return result;
    mm_storage_init(&input, NULL);
    mm_storage_init(&decoded, NULL);
    parsed = mm_storage_resize_exact(&input, 0, source->length, MM_STORAGE_NORMAL) == MM_STORAGE_OK &&
             mm_read_exact(source, 0, input.buffers[0].data, source->length) &&
             mm_midi_import(input.buffers[0].data, source->length, &decoded, &bytes) &&
             mm_memory_stream_init(&payload, decoded.buffers[0].data, bytes);
    mm_storage_dispose(&input);
    if (parsed)
        result = mm_sound_write(&payload.stream, 0, MM_SOUND_STYLE_MUSIC, name, number, s);
    mm_storage_dispose(&decoded);
    return result;
}

int mm_sound_invalidate(void *object, const MMSoundResourceServices *s)
{
    MMMovieSessionGraph graph;
    MMChunkAccess chunks;
    MMChunkChild child;
    MMSoundOwner owner;
    void *file;
    MMU32 type, number, position;
    unsigned char b[MM_SOUND_HEADER_BYTES];
    s->binding(s->context, object, &file, &type, &number);
    s->graph(s->context, file, &graph);
    graph.access(graph.context, &chunks);
    if (mm_chunk_find(&chunks, MM_SOUND_TYPE, number, &position) != MM_CHUNK_OK ||
        mm_chunk_child_type(&chunks, position, 0, 0, &child) != MM_CHUNK_OK)
        return 0;
    s->owner(s->context, object, &owner);
    encode_header(b, owner.resource->style, owner.resource->volume, 1, s->host_big_endian, s->system);
    if (!s->put(s->context, file, type, number, b))
        return 0;
    s->delete_child(s->context, file, type, number, &child);
    *owner.invalid = 1;
    return 1;
}
MMI32 mm_sound_class(MMI32 style,int loop)
{
    return loop ? (style == MM_SOUND_STYLE_MUSIC ? MM_SOUND_CLASS_LOOP_MIDI : MM_SOUND_CLASS_LOOP_WAVE)
                : MM_SOUND_CLASS_NONLOOP;
}
MMI32 mm_sound_priority(MMI32 style,int tool)
{ if (style==4) return 1; if (style==3) return tool==0?1:tool==2?5:3; if (style==2) return tool==0?2:tool==2?6:4; return 0; }
void mm_sound_play(void *object,MMSoundLastMidi *last,MMI32 object_id,int loop,int queue,MMI32 volume,MMI32 priority,int actor,MMU32 start,const MMSoundResourceServices *s)
{
    MMSoundOwner o; MMI32 number,sound_class,instance=0;
    s->owner(s->context,object,&o); if (*o.invalid) return;
    number=mm_sound_queue_number(o.resource->style,object_id,actor); sound_class=mm_sound_class(o.resource->style,loop);
    if (o.resource->style==4 && o.resource->type==last->type && o.resource->number==last->number && s->playing(s->context,last->instance)) return;
    if (!queue || *o.silent) s->stop(s->context,number);
    if (!*o.silent) instance=s->play(s->context,o.resource,number,volume,loop?2147483647L:1,start,priority,sound_class);
    if (o.resource->style==4) { last->instance=instance; last->type=o.resource->type; last->number=o.resource->number; }
}
