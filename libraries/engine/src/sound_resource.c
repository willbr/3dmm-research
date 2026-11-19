/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/sound_resource.h"
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

static void header(unsigned char *b,MMI32 style,MMI32 volume,int invalid,const MMSoundResourceServices *s)
{
    memset(b,0,16); put(b,2,1U,s->host_big_endian); put(b+2,2,s->system,s->host_big_endian);
    put(b+4,4,(MMU32)style,s->host_big_endian); put(b+8,4,(MMU32)volume,s->host_big_endian); b[12]=(unsigned char)(invalid!=0);
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
int mm_sound_write(void *source,int wave,void *file,MMI32 style,const void *name,void *number_out,const MMSoundResourceServices *s)
{
    unsigned char b[16]; MMU32 number,child; MMI32 bytes;
    bytes=wave?s->wave_size(s->context,source):0; header(b,wave?style:4,65536L,0,s);
    if (!s->add(s->context,file,b,number_out)) return 0;
    memcpy(&number,number_out,4); if (!wave) bytes=s->midi_size(s->context,source);
    if (!s->add_child(s->context,file,number,wave?MM_SOUND_WAVE:MM_SOUND_MIDI,bytes,&child)) goto fail;
    if (wave) { if (!s->find_wave(s->context,file,child) || !s->copy_wave(s->context)) goto fail; }
    else if (!s->write_midi(s->context,source)) goto fail;
    if (!s->set_name(s->context,file,number,name)) goto fail;
    return 1;
fail:
    s->delete_parent(s->context,file,number); return 0;
}
int mm_sound_copy_midi(void *source,void *file,const void *name,void *number_out,const MMSoundResourceServices *s)
{
    void *prepared,*stream; int result;
    prepared=s->prepare_name(s->context,source,name); stream=s->read_midi(s->context);
    if (!stream) { s->bad_sound(s->context); return 0; }
    result=mm_sound_write(stream,0,file,4,prepared,number_out,s); s->release(s->context,stream); return result;
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
    header(b, owner.resource->style, owner.resource->volume, 1, s);
    if (!s->put(s->context, file, type, number, b))
        return 0;
    s->delete_child(s->context, file, type, number, &child);
    *owner.invalid = 1;
    return 1;
}
MMI32 mm_sound_class(MMI32 style,int loop) { return loop?(style==4?2:1):0; }
MMI32 mm_sound_priority(MMI32 style,int tool)
{ if (style==4) return 1; if (style==3) return tool==0?1:tool==2?5:3; if (style==2) return tool==0?2:tool==2?6:4; return 0; }
void mm_sound_play(void *object,MMSoundLastMidi *last,MMI32 object_id,int loop,int queue,MMI32 volume,MMI32 priority,int actor,MMU32 start,const MMSoundResourceServices *s)
{
    MMSoundOwner o; MMI32 number,sound_class,instance=0;
    s->owner(s->context,object,&o); if (*o.invalid) return;
    number=s->queue_number(s->context,o.resource->style,object_id,actor); sound_class=mm_sound_class(o.resource->style,loop);
    if (o.resource->style==4 && o.resource->type==last->type && o.resource->number==last->number && s->playing(s->context,last->instance)) return;
    if (!queue || *o.silent) s->stop(s->context,number);
    if (!*o.silent) instance=s->play(s->context,o.resource,number,volume,loop?2147483647L:1,start,priority,sound_class);
    if (o.resource->style==4) { last->instance=instance; last->type=o.resource->type; last->number=o.resource->number; }
}
