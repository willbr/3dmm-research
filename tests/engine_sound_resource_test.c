#include "three_mm/sound_resource.h"
#include <assert.h>
#include <string.h>
enum { CREATE=1,FIND,UNPACK,READ,NAME,CHILD,ADD,ADD_CHILD,WRITE_MIDI,FIND_WAVE,COPY_WAVE,SET_NAME,READ_MIDI,PUT };
typedef struct Object { ThreeMMSoundResource resource; ThreeMMI32 silent; unsigned char invalid; int named; } Object;
typedef struct Context
{
    ThreeMMSoundResourceServices services; Object object;
    unsigned char bytes[16]; ThreeMMI32 length; int fail,events[32],count,created,released,invalidations,deleted,deleted_child,bad,order,stream;
    int playing,stopped,plays,queries; ThreeMMI32 queue,volume,repeat,priority,sound_class; ThreeMMU32 start,lookup_type;
    void *object_slot,*size_slot; const void *name; int release_after_invalidation;
} Context;
static int event(Context *c,int n) { assert(c->count<32); c->events[c->count++]=n; return c->fail!=n; }
static void owner(void *p,void *object,ThreeMMSoundOwner *out)
{ Object *o=(Object *)object; (void)p; out->resource=&o->resource; out->silent=&o->silent; out->invalid=&o->invalid; }
static void *create(void *p) { Context *c=(Context *)p; if (!event(c,CREATE)) return NULL; ++c->created; return &c->object; }
static void release(void *p,void *object)
{ Context *c=(Context *)p; assert(object==&c->object || object==&c->stream); ++c->released; if (c->object_slot) { assert(c->invalidations==2); c->release_after_invalidation=1; } }
static ThreeMMI32 object_size(void *p) { (void)p; return 333; }
static void invalidate_output(void *p,void *out,size_t bytes)
{ Context *c=(Context *)p; assert(out==c->object_slot || out==c->size_slot); memset(out,0xcd,bytes); ++c->invalidations; }
static void *file(void *p,void *cache) { assert(cache==p); return p; }
static void *find(void *p,void *f,ThreeMMU32 type,ThreeMMU32 n) { (void)type; assert(p==f && n==37); return event((Context *)p,FIND)?p:NULL; }
static int unpack(void *p,void *b) { assert(p==b); return event((Context *)p,UNPACK); }
static ThreeMMI32 block_size(void *p,void *b) { assert(p==b); return ((Context *)p)->length; }
static int read_block(void *p,void *b,void *out,ThreeMMI32 bytes)
{ Context *c=(Context *)p; assert(p==b && bytes==16); if (!event(c,READ)) return 0; memcpy(out,c->bytes,16); return 1; }
static int read_name(void *p,void *f,ThreeMMU32 type,ThreeMMU32 n,void *o)
{ (void)type; assert(p==f && n==37); ((Object *)o)->named=1; return event((Context *)p,NAME); }
static int child(void *p,void *f,ThreeMMU32 type,ThreeMMU32 n,ThreeMMSoundChild *out)
{ Context *c=(Context *)p; assert(p==f && n==37); c->lookup_type=type; out->type=THREE_MM_SOUND_WAVE; out->number=19; return event(c,CHILD); }
static void publish_invalid(void *p,void *out,int invalid) { (void)p; *(int *)out=invalid; }
static int add(void *p,void *f,const void *header,void *out)
{ Context *c=(Context *)p; ThreeMMU32 n=37; assert(p==f); memcpy(c->bytes,header,16); memcpy(out,&n,4); return event(c,ADD); }
static ThreeMMI32 wave_size(void *p,void *source) { assert(p==source); event((Context *)p,20); return 7; }
static ThreeMMI32 midi_size(void *p,void *source) { Context *c=(Context *)p; assert(source==p || source==&c->stream); event(c,21); return 9; }
static int add_child(void *p,void *f,ThreeMMU32 n,ThreeMMU32 type,ThreeMMI32 bytes,ThreeMMU32 *out)
{ assert(p==f && n==37 && (type==THREE_MM_SOUND_WAVE?bytes==7:bytes==9)); *out=19; return event((Context *)p,ADD_CHILD); }
static int write_midi(void *p,void *source) { assert(source==p || source==&((Context *)p)->stream); return event((Context *)p,WRITE_MIDI); }
static int find_wave(void *p,void *f,ThreeMMU32 n) { assert(p==f && n==19); return event((Context *)p,FIND_WAVE); }
static int copy_wave(void *p) { return event((Context *)p,COPY_WAVE); }
static int set_name(void *p,void *f,ThreeMMU32 n,const void *name) { Context *c=(Context *)p; assert(p==f && n==37 && name==c->name); return event(c,SET_NAME); }
static void delete_parent(void *p,void *f,ThreeMMU32 n) { assert(p==f && n==37); ++((Context *)p)->deleted; }
static void *prepare_name(void *p,void *source,const void *name) { Context *c=(Context *)p; assert(p==source); assert(!name || name==p); c->name=name?name:&c->name; event(c,22); return (void *)c->name; }
static void *read_midi(void *p) { Context *c=(Context *)p; return event(c,READ_MIDI)?&c->stream:NULL; }
static void bad_sound(void *p) { ++((Context *)p)->bad; }
static void binding(void *p,void *o,void **f,ThreeMMU32 *type,ThreeMMU32 *n) { assert(o==&((Context *)p)->object); *f=p; *type=0x1234; *n=37; }
static int put(void *p,void *f,ThreeMMU32 type,ThreeMMU32 n,const void *header)
{ Context *c=(Context *)p; assert(p==f && type==0x1234 && n==37 && !c->object.invalid); memcpy(c->bytes,header,16); assert(c->bytes[12]==1); return event(c,PUT); }
static void delete_child(void *p,void *f,ThreeMMU32 type,ThreeMMU32 n,const ThreeMMSoundChild *kid)
{ Context *c=(Context *)p; assert(p==f && type==0x1234 && n==37 && kid->number==19 && !c->object.invalid); ++c->deleted_child; }
static void bad_order(void *p) { ++((Context *)p)->order; }
static ThreeMMI32 queue_number(void *p,ThreeMMI32 style,ThreeMMI32 object,int actor)
{ Context *c=(Context *)p; assert(style==c->object.resource.style); return object+(actor?100:200); }
static int playing(void *p,ThreeMMI32 instance) { Context *c=(Context *)p; assert(instance==8); ++c->queries; return c->playing; }
static void stop(void *p,ThreeMMI32 queue) { Context *c=(Context *)p; ++c->stopped; c->queue=queue; }
static ThreeMMI32 play(void *p,const ThreeMMSoundResource *r,ThreeMMI32 queue,ThreeMMI32 volume,ThreeMMI32 repeat,ThreeMMU32 start,ThreeMMI32 priority,ThreeMMI32 sound_class)
{ Context *c=(Context *)p; assert(r==&c->object.resource); ++c->plays; c->queue=queue; c->volume=volume; c->repeat=repeat; c->start=start; c->priority=priority; c->sound_class=sound_class; return 9; }
static void initialize(Context *c)
{
    ThreeMMSoundResourceServices *s;
    memset(c,0,sizeof(*c)); c->length=16; c->bytes[0]=1; c->bytes[4]=2; c->bytes[10]=1;
    c->object.resource.type=17; c->object.resource.number=18; c->object.resource.style=1; c->object.resource.volume=123; c->object.silent=2;
    s=&c->services; s->context=c; s->owner=owner; s->create=create; s->release=release; s->object_size=object_size; s->invalidate_output=invalidate_output;
    s->file=file; s->find=find; s->unpack=unpack; s->block_size=block_size; s->read=read_block; s->read_name=read_name; s->child=child; s->publish_invalid=publish_invalid;
    s->add=add; s->wave_size=wave_size; s->midi_size=midi_size; s->add_child=add_child; s->write_midi=write_midi; s->find_wave=find_wave; s->copy_wave=copy_wave;
    s->set_name=set_name; s->delete_parent=delete_parent; s->prepare_name=prepare_name; s->read_midi=read_midi; s->bad_sound=bad_sound;
    s->binding=binding; s->put=put; s->delete_child=delete_child; s->bad_order=bad_order; s->queue_number=queue_number; s->playing=playing; s->stop=stop; s->play=play;
}
static void loads(void)
{
    Context c; int failure,big,i,invalid; ThreeMMI32 size,style,volume; void *out;
    for (big=0;big<2;++big) for (failure=0;failure<10;++failure)
    {
        initialize(&c); c.services.host_big_endian=big; if (big) { c.bytes[0]=0; c.bytes[1]=1; c.bytes[4]=0; c.bytes[7]=2; c.bytes[10]=0; c.bytes[9]=1; }
        c.fail=failure<=NAME?failure:0; if (failure==6) c.length=15; if (failure==7) c.length=17;
        if (failure==8) { c.bytes[0]^=1; c.bytes[1]^=1; } /* Foreign markers, native scalars. */
        if (failure==9) c.bytes[12]=1;
        size=-1; assert(three_mm_sound_fetch(&c,1,37,NULL,&size,&c.services) && size==333 && !c.count);
        out=NULL; c.object_slot=&out; c.size_slot=&size;
        i=three_mm_sound_fetch(&c,1,37,&out,&size,&c.services);
        assert(i==(failure==0 || failure>=8));
        if (i) { assert(out==&c.object && size==333 && c.object.resource.cache==&c && c.object.resource.style==2 && c.object.resource.volume==65536L);
            assert(c.object.invalid==(failure==9)); if (failure==9) assert(c.object.silent==2 && c.object.resource.number==18); else assert(!c.object.silent && c.object.resource.number==19); }
        else { assert(c.invalidations==2 && (failure==CREATE || c.release_after_invalidation)); for (i=0;i<4;++i) assert(((unsigned char *)&size)[i]==0xcd); }
    }
    initialize(&c); c.bytes[0]=0; c.bytes[1]=1; c.bytes[4]=0; c.bytes[7]=2; c.bytes[8]=0x12; c.bytes[9]=0x34; c.bytes[10]=0x56; c.bytes[11]=0x78;
    assert(three_mm_sound_read(&c.object,&c,1,37,&c.services)); assert(c.object.resource.style==33554432L && c.object.resource.volume==2018915346L);
    initialize(&c); c.fail=CHILD; assert(three_mm_sound_read(&c.object,&c,1,37,&c.services) && c.object.silent==1 && c.object.resource.number==18);
    for (failure=0;failure<=NAME;++failure) for (i=0;i<8;++i)
    {
        initialize(&c); c.fail=failure; invalid=17; style=18; volume=19;
        assert(three_mm_sound_info(&c,1,37,i&1?&invalid:NULL,i&2?&style:NULL,i&4?&volume:NULL,&c.services)==!failure);
        assert(invalid==(!failure && (i&1)?0:17) && style==(!failure && (i&2)?2:18) && volume==(!failure && (i&4)?65536L:19));
    }
    initialize(&c); c.bytes[0]=c.bytes[1]=0x7f; assert(three_mm_sound_read(&c.object,&c,1,37,&c.services) && c.order==1);
}
static void writes(void)
{
    Context c; ThreeMMU32 number; int wave,failure,big,i,result;
    for (big=0;big<2;++big) for (wave=0;wave<2;++wave) for (failure=0;failure<5;++failure)
    {
        int points[5]; points[0]=0; points[1]=ADD; points[2]=ADD_CHILD; points[3]=wave?COPY_WAVE:WRITE_MIDI; points[4]=SET_NAME;
        initialize(&c); c.services.host_big_endian=big; c.services.system=0x1234; c.fail=points[failure]; c.name=&c; number=99;
        result=three_mm_sound_write(&c,wave,&c,3,&c,&number,&c.services);
        assert(result==!failure && number==37 && c.deleted==(failure>=2));
        assert(c.bytes[big?1:0]==1 && c.bytes[2]==(big?0x12:0x34) && c.bytes[3]==(big?0x34:0x12));
        assert(c.bytes[big?7:4]==(wave?3:4) && !c.bytes[12]); for (i=13;i<16;++i) assert(!c.bytes[i]);
        assert(c.events[0]==(wave?20:ADD)); if (!wave && failure!=1) assert(c.events[1]==21);
    }
    initialize(&c); c.fail=FIND_WAVE; c.name=&c; assert(!three_mm_sound_write(&c,1,&c,2,&c,&number,&c.services) && c.deleted==1);
    for (failure=0;failure<3;++failure) for (wave=0;wave<2;++wave)
    {
        initialize(&c); c.fail=failure==1?READ_MIDI:failure==2?SET_NAME:0;
        assert(three_mm_sound_copy_midi(&c,&c,wave?&c:NULL,&number,&c.services)==!failure);
        assert(c.bad==(failure==1) && c.released==(failure!=1) && c.deleted==(failure==2));
    }
    for (failure=0;failure<3;++failure)
    {
        initialize(&c); c.fail=failure==1?CHILD:failure==2?PUT:0;
        assert(three_mm_sound_invalidate(&c.object,&c.services)==!failure && c.object.invalid==!failure && c.deleted_child==!failure);
        assert(c.lookup_type==THREE_MM_SOUND_TYPE);
    }
}
static void playback(void)
{
    Context c; ThreeMMSoundLastMidi last; int style,loop,queue,silent,invalid,actor;
    for (style=2;style<=4;++style) for (loop=0;loop<2;++loop) for (queue=0;queue<2;++queue)
    for (silent=0;silent<2;++silent) for (invalid=0;invalid<2;++invalid) for (actor=0;actor<2;++actor)
    {
        initialize(&c); c.object.resource.style=style; c.object.silent=silent; c.object.invalid=(unsigned char)invalid;
        last.instance=8; last.type=33; last.number=34;
        three_mm_sound_play(&c.object,&last,-1,loop,queue,123,6,actor,0xffffffffU,&c.services);
        assert(c.stopped==(!invalid && (!queue || silent)) && c.plays==(!invalid && !silent) && !c.queries);
        if (c.plays) assert(c.queue==(actor?99:199) && c.volume==123 && c.repeat==(loop?2147483647L:1) && c.priority==6 && c.start==0xffffffffU && c.sound_class==(loop?(style==4?2:1):0));
        if (style==4 && !invalid) assert(last.instance==(silent?0:9) && last.type==17 && last.number==18); else assert(last.instance==8 && last.type==33 && last.number==34);
    }
    for (queue=0;queue<2;++queue)
    {
        initialize(&c); c.object.resource.style=4; c.object.silent=0; last.instance=8; last.type=17; last.number=18; c.playing=queue;
        three_mm_sound_play(&c.object,&last,1,1,0,1,2,1,0,&c.services);
        assert(c.queries==1 && c.plays==!queue && c.stopped==!queue && last.instance==(queue?8:9));
    }
    assert(three_mm_sound_priority(2,0)==2 && three_mm_sound_priority(2,1)==4 && three_mm_sound_priority(2,2)==6);
    assert(three_mm_sound_priority(3,0)==1 && three_mm_sound_priority(3,1)==3 && three_mm_sound_priority(3,2)==5 && three_mm_sound_priority(4,0)==1 && !three_mm_sound_priority(0,0));
}
int main(void) { loads(); writes(); playback(); return 0; }
