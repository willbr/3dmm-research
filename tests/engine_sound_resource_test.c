#include "mm/sound_resource.h"
#include "mm/movie_session.h"
#include "mm/text_store.h"
#include "mm/midi.h"
#include "engine_resource_block_fixture.h"
#include "chunk_writer_fixture.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
enum { CREATE = 1, FIND, UNPACK, READ, NAME, CHILD, ADD, ADD_CHILD, LINK_CHILD, READ_SOURCE, WRITE_CHILD, SET_NAME, PUT };
typedef struct Context
{
    MMSoundResourceServices services;
    MMSoundResourceHost host;
    MMStorageAllocator objects;
    MMArchiveWriter writer;
    MMSoundState object;
    ChunkWriterFixture archive;
    MMResourceCache cache;
    MMResourceObject binding;
    ResourceTestBlock block;
    unsigned char bytes[16], output[32];
    MMI32 length;
    int fail,events[32],count,created,released,invalidations,deleted,deleted_child,order,packed,file_refs,editing,locates;
    MMU32 child_type, payload_bytes;
    MMMemoryStream source;
    int playing,stopped,plays,queries,children; MMI32 queue,volume,repeat,priority,sound_class; MMU32 start,lookup_type;
    void *object_slot,*size_slot; int release_after_invalidation;
} Context;
static int event(Context *c,int n) { assert(c->count<32); c->events[c->count++]=n; return c->fail!=n; }
static void *allocate_owner(void *p, void *old, MMU32 old_bytes, MMU32 bytes, int priority)
{
    Context *c = p;
    void *object;
    assert(!old && !old_bytes && bytes && priority == MM_STORAGE_NORMAL);
    if (!event(c, CREATE))
        return NULL;
    object = malloc(bytes);
    assert(object);
    ++c->created;
    return object;
}
static void free_owner(void *p, void *object, MMU32 bytes)
{
    Context *c = p;
    assert(object && bytes && c->created > c->released);
    if (c->object_slot)
    {
        assert(c->invalidations == 2);
        c->release_after_invalidation = 1;
    }
    ++c->released;
    free(object);
}
static void invalidate_output(void *p,void *out,size_t bytes)
{ Context *c=(Context *)p; assert(out==c->object_slot || out==c->size_slot); memset(out,0xcd,bytes); ++c->invalidations; }
static int read_block(void *p, MMU32 offset, void *out, size_t bytes)
{
    Context *c = (Context *)p;
    assert(!offset && bytes == 16);
    if (!event(c, READ))
        return 0;
    memcpy(out, c->bytes, bytes);
    return 1;
}
static int graph_entry(void *p, MMU32 position, MMChunkEntry *entry)
{
    const Context *c = (const Context *)p;
    assert(position < 2);
    if (c->fail == FIND)
        return 0;
    memset(entry, 0, sizeof(*entry));
    entry->type = position ? MM_SOUND_TYPE : 1;
    entry->number = 37;
    entry->child_count = c->children;
    return 1;
}
static int graph_child(void *p, MMU32 parent, MMU32 i, MMChunkChild *out)
{
    Context *c = (Context *)p;
    assert(i < (MMU32)c->children);
    c->lookup_type = parent ? MM_SOUND_TYPE : 1;
    out->slot = i ? 4 : 0;
    out->type = c->children == 2 && !i ? MM_TEXT_ROOT : MM_SOUND_WAVE;
    out->number = 19 + i;
    return event(c, CHILD);
}
static MMChunkAccess cache_chunks(void *p)
{
    MMChunkAccess access = {NULL, 2, graph_entry, graph_child};
    access.context = p;
    return access;
}
static void graph_access(void *p, MMChunkAccess *out) { *out = cache_chunks(p); }
static int graph_payload(void *p, const MMChunkEntry *entry, MMMovieReceiveBytes receive, void *out)
{
    Context *c = (Context *)p;
    MMReadStream stream = {NULL, 0, read_block};
    assert(entry->number == 37);
    if (!event(c, UNPACK))
        return MM_MOVIE_BAD_FILE;
    stream.context = p;
    stream.length = c->length;
    return receive(out, &stream);
}
static void graph(void *p, void *file, MMMovieSessionGraph *out)
{
    MMMovieSessionGraph source = {NULL, graph_access, NULL, graph_payload, NULL};
    assert(p == file);
    source.context = p;
    *out = source;
}
static int read_name(void *p,void *f,MMResourceCache *cache,MMU32 type,MMU32 n,MMSoundOwner *o)
{
    Context *c = p;
    MMStringValue name = mm_sound_owner_name(o);
    (void)type;
    assert(n == 37 && (cache ? cache == &c->cache && !f : p == f));
    mm_string_value_set(&name, "test", 4);
    return event(c,NAME);
}
static int locate(void *p, MMU32 type, MMU32 number, MMBlockFile *out, int *packed)
{
    Context *c = p;
    assert((type == 1 || type == MM_SOUND_TYPE) && number == 37);
    ++c->locates;
    out->file = p;
    out->offset = 29;
    out->length = c->packed ? 12 : (MMU32)c->length;
    *packed = c->packed;
    return 1;
}
static void retain_block(void *p, void *file)
{
    Context *c = p;
    assert(p == file && !c->file_refs);
    ++c->file_refs;
}
static void release_block(void *p, void *file)
{
    Context *c = p;
    assert(p == file && c->file_refs == 1);
    --c->file_refs;
}
static int read_cached(void *p, void *file, MMU32 offset, void *out, size_t bytes)
{
    Context *c = p;
    assert(p == file && c->file_refs == 1 && offset == 29);
    if (!c->packed)
        return read_block(p, 0, out, bytes);
    assert(bytes == 12);
    memcpy(out, c->bytes, bytes);
    return 1;
}
static int write_cached(void *p, void *file, MMU32 offset, const void *data, size_t bytes)
{
    (void)p; (void)file; (void)offset; (void)data; (void)bytes;
    return 0;
}
static int decode(void *p, void **handle)
{
    Context *c = p;
    assert(c->packed && resource_handle_size(NULL, *handle) == 12);
    if (!event(c, UNPACK))
        return 0;
    resource_block_decode(handle, c->bytes, sizeof(c->bytes));
    return 1;
}
static void publish_invalid(void *p,void *out,int invalid) { (void)p; *(int *)out=invalid; }
static int sound_extent(void *p, MMU32 bytes, MMChunkLocation *extent)
{
    Context *c = ((ChunkIndexFixture *)p)->observer;
    int child = !c->editing && c->archive.index.count == 2;
    if (child && !event(c, ADD_CHILD))
        return 0;
    assert(child ? bytes <= sizeof(c->output) : bytes == sizeof(c->bytes));
    extent->file = &c->archive;
    extent->offset = child ? 256 : 128;
    extent->bytes = bytes;
    extent->flags = 0;
    if (child)
        c->payload_bytes = bytes;
    return 1;
}
static int write_sound(void *p, void *file, MMU32 offset, const void *bytes, size_t count)
{
    ChunkWriterFixture *f = p;
    Context *c = f->context;
    assert(file == p && !c->object.invalid);
    if (offset == 128)
    {
        assert(count == sizeof(c->bytes));
        memcpy(c->bytes, bytes, count);
        assert(c->bytes[12] == c->editing);
        return event(c, c->editing ? PUT : ADD);
    }
    assert(offset == 256 && count == c->payload_bytes && f->file_refs == 1);
    if (!event(c, WRITE_CHILD))
        return 0;
    memcpy(c->output, bytes, count);
    return 1;
}
static int link_sound(void *p, MMU32 parent, const MMChunkChild *child)
{
    Context *c = p;
    assert(!parent && !child->slot && !child->number);
    c->child_type = child->type;
    return event(c, LINK_CHILD);
}
static int set_name(void *p) { return event(p, SET_NAME); }
static void deleted_sound(void *p, const MMChunkEntry *entry)
{
    Context *c = p;
    if (entry->type == MM_SOUND_TYPE)
        ++c->deleted;
    else if (entry->type)
    {
        assert(!c->object.invalid);
        ++c->deleted_child;
    }
}
static int read_source(void *p, MMU32 offset, void *bytes, size_t count)
{
    Context *c = (Context *)p;
    return event(c, READ_SOURCE) && mm_read_exact(&c->source.stream, offset, bytes, count);
}
static void bad_order(void *p) { ++((Context *)p)->order; }
static int playing(void *p,MMI32 instance) { Context *c=(Context *)p; assert(instance==8); ++c->queries; return c->playing; }
static void stop(void *p,MMI32 queue) { Context *c=(Context *)p; ++c->stopped; c->queue=queue; }
static MMI32 play(void *p,const MMSoundResource *r,MMI32 queue,MMI32 volume,MMI32 repeat,MMU32 start,MMI32 priority,MMI32 sound_class)
{ Context *c=(Context *)p; assert(r==&c->object.resource); ++c->plays; c->queue=queue; c->volume=volume; c->repeat=repeat; c->start=start; c->priority=priority; c->sound_class=sound_class; return 9; }
static void initialize(Context *c)
{
    MMSoundResourceServices *s;
    memset(c,0,sizeof(*c)); c->length=16; c->children=1; c->bytes[0]=1; c->bytes[4]=2; c->bytes[10]=1;
    resource_block_init(&c->block, c, read_cached, write_cached);
    c->block.files.retain = retain_block;
    c->block.files.release = release_block;
    c->cache.file = c;
    c->cache.services.handles = c->block.owner.handles;
    c->cache.services.block_files = &c->block.files;
    c->cache.services.live_chunks = cache_chunks;
    c->cache.services.locate = locate;
    c->binding.cache = &c->cache;
    c->binding.type = 0x1234;
    c->binding.number = 37;
    c->object.resource.type=17; c->object.resource.number=18; c->object.resource.style=1; c->object.resource.volume=123; c->object.silent=2;
    c->objects.context = c;
    c->objects.resize = allocate_owner;
    c->objects.release = free_owner;
    c->host.services = &c->services;
    c->host.objects = &c->objects;
    c->host.cache_bytes = 333;
    c->host.name_unit = 1;
    s=&c->services; s->context=c; s->invalidate_output=invalidate_output;
    s->decode=decode; s->graph=graph; s->read_name=read_name; s->publish_invalid=publish_invalid;
    c->writer = chunk_writer_fixture(&c->archive, c, sound_extent, write_sound);
    c->archive.grow_name = set_name;
    c->archive.index.before_insert = link_sound;
    c->archive.index.before_delete = deleted_sound;
    s->bad_order=bad_order; s->playing=playing; s->stop=stop; s->play=play;
}
static MMSoundOwner *direct_owner(Context *c)
{
    MMSoundOwner *owner = mm_sound_owner_create(&c->host);
    assert(owner);
    *mm_sound_owner_state(owner) = c->object;
    return owner;
}

static void loads(void)
{
    Context c; int failure,big,i,invalid; MMI32 size,style,volume; void *out;
    MMSoundOwner *owner;
    for (big=0;big<2;++big) for (failure=0;failure<10;++failure)
    {
        initialize(&c); c.services.host_big_endian=big; if (big) { c.bytes[0]=0; c.bytes[1]=1; c.bytes[4]=0; c.bytes[7]=2; c.bytes[10]=0; c.bytes[9]=1; }
        c.fail=failure<=NAME?failure:0; if (failure==6) c.length=15; if (failure==7) c.length=17;
        c.packed=failure==UNPACK;
        if (failure==8) { c.bytes[0]^=1; c.bytes[1]^=1; } /* Foreign markers, native scalars. */
        if (failure==9) c.bytes[12]=1;
        size=-1; assert(mm_sound_fetch(&c.cache,1,37,NULL,NULL,&size,&c.host) && size==333 && !c.count);
        out=NULL; c.object_slot=&out; c.size_slot=&size;
        i=mm_sound_fetch(&c.cache,1,37,NULL,&out,&size,&c.host);
        assert(i==(failure==0 || failure>=8) && !c.file_refs);
        if (i)
        {
            const MMSoundState *state = mm_sound_owner_state(out);
            MMStringValue name = mm_sound_owner_name(out);
            assert(size == 333 && state->resource.cache == &c.cache && state->resource.style == 2 &&
                   state->resource.volume == 65536L && mm_string_value_equal(&name, "test", 4));
            assert(state->invalid == (failure == 9) && !state->silent);
            assert(state->resource.number == (failure == 9 ? 0U : 19U));
            c.object_slot = NULL;
            mm_sound_owner_release(out);
        }
        else { assert(c.invalidations==2 && (failure==CREATE || c.release_after_invalidation)); for (i=0;i<4;++i) assert(((unsigned char *)&size)[i]==0xcd); }
        assert(c.created == c.released);
    }
    initialize(&c); c.packed=1;
    assert(mm_sound_fetch(&c.cache, MM_SOUND_TYPE, 37, NULL, &out, &size, &c.host) && !c.file_refs);
    assert(mm_sound_owner_state(out)->resource.cache == &c.cache &&
           mm_sound_owner_state(out)->resource.number == 19 && c.events[1] == UNPACK);
    mm_sound_owner_release(out);
    initialize(&c); c.bytes[0]=0; c.bytes[1]=1; c.bytes[4]=0; c.bytes[7]=2; c.bytes[8]=0x12; c.bytes[9]=0x34; c.bytes[10]=0x56; c.bytes[11]=0x78;
    owner = direct_owner(&c);
    assert(mm_sound_read(owner,&c,1,37));
    assert(mm_sound_owner_state(owner)->resource.style == 33554432L &&
           mm_sound_owner_state(owner)->resource.volume == 2018915346L);
    mm_sound_owner_release(owner);
    initialize(&c); c.fail=CHILD;
    owner = direct_owner(&c);
    assert(mm_sound_read(owner,&c,1,37) && mm_sound_owner_state(owner)->silent == 1 &&
           mm_sound_owner_state(owner)->resource.number == 18);
    c.bytes[12] = 1;
    assert(mm_sound_read(owner,&c,1,37) && mm_sound_owner_state(owner)->invalid &&
           mm_sound_owner_state(owner)->silent == 1 && mm_sound_owner_state(owner)->resource.number == 18);
    mm_sound_owner_release(owner);
    for (failure=0;failure<=NAME;++failure) for (i=0;i<8;++i)
    {
        initialize(&c); c.fail=failure; invalid=17; style=18; volume=19;
        assert(mm_sound_info(&c,1,37,i&1?&invalid:NULL,i&2?&style:NULL,i&4?&volume:NULL,&c.host)==!failure);
        assert(invalid==(!failure && (i&1)?0:17) && style==(!failure && (i&2)?2:18) && volume==(!failure && (i&4)?65536L:19));
    }
    initialize(&c); c.bytes[0]=c.bytes[1]=0x7f;
    owner = direct_owner(&c);
    assert(mm_sound_read(owner,&c,1,37) && c.order == 1);
    mm_sound_owner_release(owner);
}

static void supplied_block(void)
{
    Context c;
    void *out;
    MMI32 size;
    int packed;
    for (packed = 0; packed <= 1; ++packed)
    {
        initialize(&c);
        c.packed = packed;
        assert(mm_resource_cache_read_block(&c.cache, MM_SOUND_TYPE, 37, &c.block.owner) == MM_CHUNK_OK);
        assert(c.locates == 1 && c.file_refs == 1);
        assert(mm_sound_fetch(&c.cache, MM_SOUND_TYPE, 37, &c.block.owner, NULL, &size, &c.host));
        assert(size == 333 && !c.count && c.block.owner.view.packed == packed);
        assert(mm_sound_fetch(&c.cache, MM_SOUND_TYPE, 37, &c.block.owner, &out, &size, &c.host));
        assert(size == 333 && c.locates == 1 && mm_sound_owner_state(out)->resource.number == 19);
        mm_sound_owner_release(out);
        assert(c.block.owner.view.kind == (packed ? MM_BLOCK_MEMORY : MM_BLOCK_FILE));
        assert(c.file_refs == !packed);
        assert(mm_owned_block_clear(&c.block.owner) == MM_DATA_OK && !c.file_refs);
    }
}
static void catalog(void)
{
    Context c;
    MMMovieSessionGraph source;
    MMI32 style = -1;
    initialize(&c);
    graph(&c, &c, &source);
    c.children = 2;
    c.bytes[0] = 0x7f; /* Catalog ignores marker and reserved bytes. */
    c.bytes[13] = 1;
    assert(mm_sound_catalog_style(&source, 37, 0, &style) && style == 2);
    c.count = 0;
    assert(mm_sound_read_graph(&c.object, &source, MM_SOUND_TYPE, 37, 0) && !c.object.silent &&
           c.object.resource.type == MM_TEXT_ROOT && c.object.resource.number == 19 && !c.order);
    c.count = 0;
    c.children = 0;
    style = -1;
    assert(!mm_sound_catalog_style(&source, 37, 0, &style) && style == -1);
    c.children = 1;
    c.count = 0;
    c.bytes[12] = 1;
    assert(!mm_sound_catalog_style(&source, 37, 0, &style) && style == -1);
    c.bytes[12] = 0;
    c.bytes[4] = 1;
    assert(!mm_sound_catalog_style(&source, 37, 0, &style) && style == -1);
}
static int import_wave(void *context, const MMReadStream *stream, MMI32 style, const MMChunkNameValue *name,
                       MMU32 *number, const MMArchiveWriter *writer)
{
    Context *c = context;
    assert(writer->big_endian == 0 && c->writer.big_endian == 1);
    return mm_sound_write(stream, 1, style, name, number, writer);
}

static void writes(void)
{
    const unsigned char payload[] = {4, 1, 2, 3, 4, 0x80, 0x90};
    const unsigned char smf[] = {'M','T','h','d',0,0,0,6,0,0,0,1,0,96,
                                 'M','T','r','k',0,0,0,12,0,0x90,60,64,96,0x80,60,0,0,0xff,0x2f,0};
    const int points[] = {0, ADD, ADD_CHILD, LINK_CHILD, READ_SOURCE, WRITE_CHILD, SET_NAME};
    const MMChunkNameValue name = {"sound", 5, 5};
    Context c;
    MMReadStream stream = {NULL, 0, read_source};
    MMSoundImportSource import = {0};
    MMMidiParser parser;
    MMMidiEvent note;
    MMU32 number;
    int wave, failure, big, i, result;
    stream.context = &c;
    stream.length = sizeof(payload);
    for (big = 0; big < 2; ++big)
        for (wave = 0; wave < 2; ++wave)
            for (failure = 0; failure < (int)(sizeof(points) / sizeof(*points)); ++failure)
            {
                initialize(&c);
                assert(mm_memory_stream_init(&c.source, payload, sizeof(payload)));
                c.writer.big_endian = big;
                c.writer.system = 0x1234;
                c.fail = points[failure];
                number = 99;
                result = mm_sound_write(&stream, wave, 3, &name, &number, &c.writer);
                assert(result == !failure && number == (c.fail == ADD ? 99U : 0U) && c.deleted == !!failure);
                assert(c.archive.index.count == (failure ? 0U : 2U) && !c.archive.file_refs);
                assert(c.deleted_child == (failure >= 3));
                assert(c.bytes[big ? 1 : 0] == 1 && c.bytes[2] == (big ? 0x12 : 0x34) &&
                       c.bytes[3] == (big ? 0x34 : 0x12));
                assert(c.bytes[big ? 7 : 4] == (wave ? 3 : 4));
                for (i = 12; i < 16; ++i)
                    assert(!c.bytes[i]);
                assert(c.events[0] == ADD);
                if (!failure)
                    assert(c.child_type == (wave ? MM_SOUND_WAVE : MM_SOUND_MIDI) &&
                           c.payload_bytes == sizeof(payload) && !memcmp(payload, c.output, sizeof(payload)) &&
                           !memcmp(c.archive.name + 12, name.wire, name.bytes));
            }
    for (failure = 0; failure < 6; ++failure)
    {
        initialize(&c);
        assert(mm_memory_stream_init(&c.source, smf, sizeof(smf)));
        c.writer.big_endian = 1;
        stream.length = failure == 1 ? 3 : sizeof(smf);
        c.fail = failure == 2 ? READ_SOURCE : failure == 3 ? ADD : failure == 4 ? WRITE_CHILD :
                 failure == 5 ? SET_NAME : 0;
        number = 99;
        import.stream = stream;
        import.name = name;
        import.midi = 1;
        result = mm_sound_import(&import, 3, &number, &c.writer);
        assert(c.writer.big_endian == 1);
        assert(result == (!failure ? 1 : failure <= 2 ? MM_SOUND_BAD_MIDI : 0));
        assert(number == (failure >= 1 && failure <= 3 ? 99U : 0U) && c.deleted == (failure >= 3));
        assert(c.archive.index.count == (failure ? 0U : 2U) && !c.archive.file_refs);
        if (!failure)
        {
            assert(c.child_type == MM_SOUND_MIDI && c.bytes[0] == 1 && c.bytes[4] == MM_SOUND_STYLE_MUSIC);
            mm_midi_parser_init(&parser, c.output, c.payload_bytes, 0, 500000);
            assert(mm_midi_next(&parser, &note, 1) && note.timestamp == 0 && note.send[0] == 0x90);
            assert(mm_midi_next(&parser, &note, 1) && note.timestamp == 500 && note.send[0] == 0x80);
        }
    }
    initialize(&c);
    assert(mm_memory_stream_init(&c.source, payload, sizeof(payload)));
    c.writer.big_endian = 1;
    import.stream.length = sizeof(payload);
    import.midi = 0;
    import.context = &c;
    import.wave = import_wave;
    assert(mm_sound_import(&import, MM_SOUND_STYLE_SPEECH, &number, &c.writer));
    assert(c.child_type == MM_SOUND_WAVE && c.bytes[0] == 1 && c.bytes[4] == MM_SOUND_STYLE_SPEECH);
    assert(c.payload_bytes == sizeof(payload) && !memcmp(c.output, payload, sizeof(payload)));
    for (failure = 0; failure < 3; ++failure)
    {
        MMChunkChild child = {MM_SOUND_WAVE, 19, 0};
        initialize(&c);
        c.editing = 1;
        c.cache.services.block_files = &c.archive.block.files;
        c.archive.index.before_insert = NULL;
        fixture_chunk_add(&c.archive.index, c.binding.type, 37)->entry.offset = 64;
        fixture_chunk_add(&c.archive.index, child.type, child.number);
        assert(mm_chunk_adopt(&c.writer.edit, c.binding.type, 37, &child, 1) == MM_CHUNK_OK);
        c.fail = failure == 1 ? CHILD : failure == 2 ? PUT : 0;
        assert(mm_sound_invalidate(&c.object, &c.binding, &c.writer.edit, &c.services) == !failure &&
               c.object.invalid == !failure && c.deleted_child == !failure);
        assert(c.lookup_type == MM_SOUND_TYPE);
        assert(c.archive.index.count == (failure ? 2U : 1U));
        assert(c.archive.index.rows[0].entry.offset == (failure ? 64U : 128U));
    }
    initialize(&c); c.cache.file=NULL;
    assert(!mm_sound_invalidate(&c.object, &c.binding, NULL, &c.services) && !c.object.invalid && !c.count);
}
static void playback(void)
{
    Context c; MMSoundLastMidi last; int style,loop,queue,silent,invalid,actor;
    for (style=2;style<=4;++style) for (loop=0;loop<2;++loop) for (queue=0;queue<2;++queue)
    for (silent=0;silent<2;++silent) for (invalid=0;invalid<2;++invalid) for (actor=0;actor<2;++actor)
    {
        initialize(&c); c.object.resource.style=style; c.object.silent=silent; c.object.invalid=(unsigned char)invalid;
        last.instance=8; last.type=33; last.number=34;
        mm_sound_play(&c.object,&last,37,loop,queue,123,6,actor,0xffffffffU,&c.services);
        assert(c.stopped==(!invalid && (!queue || silent)) && c.plays==(!invalid && !silent) && !c.queries);
        if (c.stopped || c.plays)
            assert(c.queue == (actor ? 0x10000000L : 0x20000000L) + style * 0x10000L + 37);
        if (c.plays) assert(c.volume==123 && c.repeat==(loop?2147483647L:1) && c.priority==6 && c.start==0xffffffffU && c.sound_class==(loop?(style==4?2:1):0));
        if (style==4 && !invalid) assert(last.instance==(silent?0:9) && last.type==17 && last.number==18); else assert(last.instance==8 && last.type==33 && last.number==34);
    }
    for (queue=0;queue<2;++queue)
    {
        initialize(&c); c.object.resource.style=4; c.object.silent=0; last.instance=8; last.type=17; last.number=18; c.playing=queue;
        mm_sound_play(&c.object,&last,1,1,0,1,2,1,0,&c.services);
        assert(c.queries==1 && c.plays==!queue && c.stopped==!queue && last.instance==(queue?8:9));
    }
    assert(mm_sound_priority(2,0)==2 && mm_sound_priority(2,1)==4 && mm_sound_priority(2,2)==6);
    assert(mm_sound_priority(3,0)==1 && mm_sound_priority(3,1)==3 && mm_sound_priority(3,2)==5 && mm_sound_priority(4,0)==1 && !mm_sound_priority(0,0));
}
int main(void) { loads(); supplied_block(); catalog(); writes(); playback(); return 0; }
