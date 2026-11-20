#include "mm/sound_resource.h"
#include "mm/movie_session.h"
#include "mm/text_store.h"
#include "mm/midi.h"
#include <assert.h>
#include <string.h>
enum { CREATE = 1, FIND, UNPACK, READ, NAME, CHILD, ADD, ADD_CHILD, READ_SOURCE, WRITE_CHILD, SET_NAME, PUT };
typedef struct Object { MMSoundResource resource; MMI32 silent; unsigned char invalid; int named; } Object;
typedef struct Context
{
    MMSoundResourceServices services; MMSoundWriteServices writer; Object object;
    unsigned char bytes[16], output[32];
    MMI32 length;
    int fail,events[32],count,created,released,invalidations,deleted,deleted_child,order;
    MMU32 child_type, payload_bytes;
    MMMemoryStream source;
    int playing,stopped,plays,queries,children; MMI32 queue,volume,repeat,priority,sound_class; MMU32 start,lookup_type;
    void *object_slot,*size_slot; const void *name; int release_after_invalidation;
} Context;
static int event(Context *c,int n) { assert(c->count<32); c->events[c->count++]=n; return c->fail!=n; }
static void owner(void *p,void *object,MMSoundOwner *out)
{ Object *o=(Object *)object; (void)p; out->resource=&o->resource; out->silent=&o->silent; out->invalid=&o->invalid; }
static void *create(void *p) { Context *c=(Context *)p; if (!event(c,CREATE)) return NULL; ++c->created; return &c->object; }
static void release(void *p,void *object)
{ Context *c=(Context *)p; assert(object==&c->object); ++c->released; if (c->object_slot) { assert(c->invalidations==2); c->release_after_invalidation=1; } }
static MMI32 object_size(void *p) { (void)p; return 333; }
static void invalidate_output(void *p,void *out,size_t bytes)
{ Context *c=(Context *)p; assert(out==c->object_slot || out==c->size_slot); memset(out,0xcd,bytes); ++c->invalidations; }
static void *file(void *p,void *cache) { assert(cache==p); return p; }
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
static void graph_access(void *p, MMChunkAccess *out)
{
    MMChunkAccess access = {NULL, 2, graph_entry, graph_child};
    access.context = p;
    *out = access;
}
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
static int read_name(void *p,void *f,MMU32 type,MMU32 n,void *o)
{ (void)type; assert(p==f && n==37); ((Object *)o)->named=1; return event((Context *)p,NAME); }
static void publish_invalid(void *p,void *out,int invalid) { (void)p; *(int *)out=invalid; }
static int add(void *p, const unsigned char *header, MMU32 *out)
{
    Context *c = (Context *)p;
    memcpy(c->bytes, header, sizeof(c->bytes));
    *out = 37;
    return event(c, ADD);
}
static int write_child(void *p, MMU32 offset, const void *bytes, size_t count)
{
    Context *c = (Context *)p;
    assert(!offset && count == c->payload_bytes && count <= sizeof(c->output));
    if (!event(c, WRITE_CHILD))
        return 0;
    memcpy(c->output, bytes, count);
    return 1;
}
static int add_child(void *p, MMU32 parent, MMU32 slot, MMU32 type, MMU32 bytes, MMStorageWriter *out)
{
    Context *c = (Context *)p;
    MMStorageWriter writer = {NULL, 0, write_child};
    assert(parent == 37 && !slot && bytes <= sizeof(c->output));
    c->child_type = type;
    c->payload_bytes = bytes;
    writer.context = p;
    writer.length = bytes;
    *out = writer;
    return event(c, ADD_CHILD);
}
static int set_name(void *p, MMU32 n, const void *name)
{
    Context *c = (Context *)p;
    assert(n == 37 && name == c->name);
    return event(c, SET_NAME);
}
static void delete_parent(void *p, MMU32 n) { assert(n == 37); ++((Context *)p)->deleted; }
static int read_source(void *p, MMU32 offset, void *bytes, size_t count)
{
    Context *c = (Context *)p;
    return event(c, READ_SOURCE) && mm_read_exact(&c->source.stream, offset, bytes, count);
}
static void binding(void *p,void *o,void **f,MMU32 *type,MMU32 *n) { assert(o==&((Context *)p)->object); *f=p; *type=0x1234; *n=37; }
static int put(void *p,void *f,MMU32 type,MMU32 n,const void *header)
{ Context *c=(Context *)p; assert(p==f && type==0x1234 && n==37 && !c->object.invalid); memcpy(c->bytes,header,16); assert(c->bytes[12]==1); return event(c,PUT); }
static void delete_child(void *p,void *f,MMU32 type,MMU32 n,const MMChunkChild *kid)
{ Context *c=(Context *)p; assert(p==f && type==0x1234 && n==37 && kid->number==19 && !c->object.invalid); ++c->deleted_child; }
static void bad_order(void *p) { ++((Context *)p)->order; }
static int playing(void *p,MMI32 instance) { Context *c=(Context *)p; assert(instance==8); ++c->queries; return c->playing; }
static void stop(void *p,MMI32 queue) { Context *c=(Context *)p; ++c->stopped; c->queue=queue; }
static MMI32 play(void *p,const MMSoundResource *r,MMI32 queue,MMI32 volume,MMI32 repeat,MMU32 start,MMI32 priority,MMI32 sound_class)
{ Context *c=(Context *)p; assert(r==&c->object.resource); ++c->plays; c->queue=queue; c->volume=volume; c->repeat=repeat; c->start=start; c->priority=priority; c->sound_class=sound_class; return 9; }
static void initialize(Context *c)
{
    MMSoundResourceServices *s;
    memset(c,0,sizeof(*c)); c->length=16; c->children=1; c->bytes[0]=1; c->bytes[4]=2; c->bytes[10]=1;
    c->object.resource.type=17; c->object.resource.number=18; c->object.resource.style=1; c->object.resource.volume=123; c->object.silent=2;
    s=&c->services; s->context=c; s->owner=owner; s->create=create; s->release=release; s->object_size=object_size; s->invalidate_output=invalidate_output;
    s->file=file; s->graph=graph; s->read_name=read_name; s->publish_invalid=publish_invalid;
    c->writer.context = c;
    c->writer.add_parent = add;
    c->writer.add_child = add_child;
    c->writer.set_name = set_name;
    c->writer.delete_parent = delete_parent;
    s->binding=binding; s->put=put; s->delete_child=delete_child; s->bad_order=bad_order; s->playing=playing; s->stop=stop; s->play=play;
}
static void loads(void)
{
    Context c; int failure,big,i,invalid; MMI32 size,style,volume; void *out;
    for (big=0;big<2;++big) for (failure=0;failure<10;++failure)
    {
        initialize(&c); c.services.host_big_endian=big; if (big) { c.bytes[0]=0; c.bytes[1]=1; c.bytes[4]=0; c.bytes[7]=2; c.bytes[10]=0; c.bytes[9]=1; }
        c.fail=failure<=NAME?failure:0; if (failure==6) c.length=15; if (failure==7) c.length=17;
        if (failure==8) { c.bytes[0]^=1; c.bytes[1]^=1; } /* Foreign markers, native scalars. */
        if (failure==9) c.bytes[12]=1;
        size=-1; assert(mm_sound_fetch(&c,1,37,NULL,&size,&c.services) && size==333 && !c.count);
        out=NULL; c.object_slot=&out; c.size_slot=&size;
        i=mm_sound_fetch(&c,1,37,&out,&size,&c.services);
        assert(i==(failure==0 || failure>=8));
        if (i) { assert(out==&c.object && size==333 && c.object.resource.cache==&c && c.object.resource.style==2 && c.object.resource.volume==65536L);
            assert(c.object.invalid==(failure==9)); if (failure==9) assert(c.object.silent==2 && c.object.resource.number==18); else assert(!c.object.silent && c.object.resource.number==19); }
        else { assert(c.invalidations==2 && (failure==CREATE || c.release_after_invalidation)); for (i=0;i<4;++i) assert(((unsigned char *)&size)[i]==0xcd); }
    }
    initialize(&c); c.bytes[0]=0; c.bytes[1]=1; c.bytes[4]=0; c.bytes[7]=2; c.bytes[8]=0x12; c.bytes[9]=0x34; c.bytes[10]=0x56; c.bytes[11]=0x78;
    assert(mm_sound_read(&c.object,&c,1,37,&c.services)); assert(c.object.resource.style==33554432L && c.object.resource.volume==2018915346L);
    initialize(&c); c.fail=CHILD; assert(mm_sound_read(&c.object,&c,1,37,&c.services) && c.object.silent==1 && c.object.resource.number==18);
    for (failure=0;failure<=NAME;++failure) for (i=0;i<8;++i)
    {
        initialize(&c); c.fail=failure; invalid=17; style=18; volume=19;
        assert(mm_sound_info(&c,1,37,i&1?&invalid:NULL,i&2?&style:NULL,i&4?&volume:NULL,&c.services)==!failure);
        assert(invalid==(!failure && (i&1)?0:17) && style==(!failure && (i&2)?2:18) && volume==(!failure && (i&4)?65536L:19));
    }
    initialize(&c); c.bytes[0]=c.bytes[1]=0x7f; assert(mm_sound_read(&c.object,&c,1,37,&c.services) && c.order==1);
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
    assert(mm_sound_read(&c.object, &c, MM_SOUND_TYPE, 37, &c.services) && !c.object.silent &&
           c.object.resource.type == MM_TEXT_ROOT && c.object.resource.number == 19 && c.order == 1);
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
static void writes(void)
{
    const unsigned char payload[] = {4, 1, 2, 3, 4, 0x80, 0x90};
    const unsigned char smf[] = {'M','T','h','d',0,0,0,6,0,0,0,1,0,96,
                                 'M','T','r','k',0,0,0,12,0,0x90,60,64,96,0x80,60,0,0,0xff,0x2f,0};
    const int points[] = {0, ADD, ADD_CHILD, READ_SOURCE, WRITE_CHILD, SET_NAME};
    Context c;
    MMReadStream stream = {NULL, 0, read_source};
    MMMidiParser parser;
    MMMidiEvent note;
    MMU32 number;
    int wave, failure, big, i, result;
    stream.context = &c;
    stream.length = sizeof(payload);
    for (big = 0; big < 2; ++big)
        for (wave = 0; wave < 2; ++wave)
            for (failure = 0; failure < 6; ++failure)
            {
                initialize(&c);
                assert(mm_memory_stream_init(&c.source, payload, sizeof(payload)));
                c.writer.host_big_endian = big;
                c.writer.system = 0x1234;
                c.fail = points[failure];
                c.name = &c;
                number = 99;
                result = mm_sound_write(&stream, wave, 3, &c, &number, &c.writer);
                assert(result == !failure && number == 37 && c.deleted == (failure >= 2));
                assert(c.bytes[big ? 1 : 0] == 1 && c.bytes[2] == (big ? 0x12 : 0x34) &&
                       c.bytes[3] == (big ? 0x34 : 0x12));
                assert(c.bytes[big ? 7 : 4] == (wave ? 3 : 4));
                for (i = 12; i < 16; ++i)
                    assert(!c.bytes[i]);
                assert(c.events[0] == ADD);
                if (!failure)
                    assert(c.child_type == (wave ? MM_SOUND_WAVE : MM_SOUND_MIDI) &&
                           c.payload_bytes == sizeof(payload) && !memcmp(payload, c.output, sizeof(payload)));
            }
    for (failure = 0; failure < 6; ++failure)
    {
        initialize(&c);
        assert(mm_memory_stream_init(&c.source, smf, sizeof(smf)));
        stream.length = failure == 1 ? 3 : sizeof(smf);
        c.fail = failure == 2 ? READ_SOURCE : failure == 3 ? ADD : failure == 4 ? WRITE_CHILD :
                 failure == 5 ? SET_NAME : 0;
        c.name = &c;
        number = 99;
        result = mm_sound_import_midi(&stream, &c, &number, &c.writer);
        assert(result == (!failure ? 1 : failure <= 2 ? MM_SOUND_BAD_MIDI : 0));
        assert(number == (failure == 1 || failure == 2 ? 99U : 37U) && c.deleted == (failure >= 4));
        if (!failure)
        {
            assert(c.child_type == MM_SOUND_MIDI);
            mm_midi_parser_init(&parser, c.output, c.payload_bytes, 0, 500000);
            assert(mm_midi_next(&parser, &note, 1) && note.timestamp == 0 && note.send[0] == 0x90);
            assert(mm_midi_next(&parser, &note, 1) && note.timestamp == 500 && note.send[0] == 0x80);
        }
    }
    for (failure = 0; failure < 3; ++failure)
    {
        initialize(&c);
        c.fail = failure == 1 ? CHILD : failure == 2 ? PUT : 0;
        assert(mm_sound_invalidate(&c.object, &c.services) == !failure &&
               c.object.invalid == !failure && c.deleted_child == !failure);
        assert(c.lookup_type == MM_SOUND_TYPE);
    }
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
int main(void) { loads(); catalog(); writes(); playback(); return 0; }
