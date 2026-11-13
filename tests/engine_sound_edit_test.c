#include "three_mm/actor_sound_edit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line) { if (!ok) { fprintf(stderr,"sound edit failed at %d\n",line); exit(1); } }
#define CHECK(x) check((x),__LINE__)
typedef union Payload { ThreeMMActorSound sound; ThreeMMI32 action[2]; } Payload;
typedef struct Fixture
{
    ThreeMMActorState state, view;
    ThreeMMActorSounds list;
    ThreeMMActorEvent event[20]; Payload data[20]; int count;
    int calls, fail_count, dirty, closed, deleted, indicator, query_live, query_count, query_fail, new_fail;
    int resolves, resolve_fail, published, refs, releases, fetch_fail, reserve_fail, insert_fail, suppressed, set_frame, midi;
    ThreeMMI32 volume_seen, loop_seen;
    ThreeMMActorSound inserted;
    ThreeMMActorSoundInfo info;
    ThreeMMTag tags[10];
} Fixture;
static ThreeMMU32 width(void *p) { (void)p; return 20; }
static ThreeMMI32 count(void *p) { return ((Fixture *)p)->count; }
static void event(void *p, ThreeMMI32 i, ThreeMMActorEvent *out)
{ Fixture *f=(Fixture *)p; CHECK(i>=0 && i<f->count); *out=f->event[i]; }
static void payload(void *p, ThreeMMI32 i, void *out)
{
    Fixture *f=(Fixture *)p; CHECK(i>=0 && i<f->count);
    memcpy(out,&f->data[i],f->event[i].aet==THREE_MM_ACTOR_ACTION ? 8 : sizeof(ThreeMMActorSound));
}
static const ThreeMMActorMotionServices tables={0,0,0,width,count,event,0,payload};
static void close_tag(void *p, ThreeMMI32 i) { Fixture *f=(Fixture *)p; CHECK(i>=0 && i<f->count); ++f->closed; }
static int scene(void *p, ThreeMMI32 *out) { (void)p; *out=4; return 1; }
static void delete_event(void *p, ThreeMMI32 i)
{
    Fixture *f=(Fixture *)p; ++f->deleted;
    CHECK(i>=0 && i<f->count);
    memmove(f->event+i,f->event+i+1,(size_t)(f->count-i-1)*sizeof(f->event[0]));
    memmove(f->data+i,f->data+i+1,(size_t)(f->count-i-1)*sizeof(f->data[0])); --f->count;
}
static void publish(void *p, const ThreeMMActorState *state) { ((Fixture *)p)->view=*state; }
static void indicator(void *p)
{ Fixture *f=(Fixture *)p; ++f->indicator; CHECK(f->view.nextEvent==f->state.nextEvent); }
static int cel_count(void *p, ThreeMMI32 action, ThreeMMI32 *out)
{ Fixture *f=(Fixture *)p; (void)action; *out=4; return ++f->calls!=f->fail_count; }
static void write_sound(void *p, ThreeMMI32 i, const ThreeMMActorSound *sound) { ((Fixture *)p)->data[i].sound=*sound; }
static void dirty(void *p) { ++((Fixture *)p)->dirty; }
static void *query_new(void *p)
{ Fixture *f=(Fixture *)p; if (f->new_fail) return NULL; ++f->query_live; f->query_count=0; return f; }
static int query_add(void *p, void *query, const ThreeMMTag *tag)
{
    Fixture *f=(Fixture *)p; CHECK(query==f && f->query_live==1);
    CHECK(f->volume_seen==(f->query_count ? 100 : -7));
    if (f->query_count+1==f->query_fail) return 0;
    f->tags[f->query_count++]=*tag; return 1;
}
static void query_release(void *p, void *query) { Fixture *f=(Fixture *)p; CHECK(query==f && f->query_live==1); --f->query_live; }
static void outputs(void *p, ThreeMMI32 volume, int loop)
{ Fixture *f=(Fixture *)p; f->volume_seen=volume; f->loop_seen=loop; ++f->published; }
static int resolve(void *p, ThreeMMTag *tag, ThreeMMU32 child)
{ Fixture *f=(Fixture *)p; (void)child; ++f->resolves; tag->number+=100; return f->resolves!=f->resolve_fail; }
static int resolve_scene(void *p, ThreeMMTag *tag, ThreeMMU32 child, ThreeMMU32 current)
{ CHECK(current==88); return resolve(p,tag,child); }
static void *fetch(void *p, const ThreeMMTag *tag)
{ Fixture *f=(Fixture *)p; (void)tag; if (f->fetch_fail) return NULL; ++f->refs; return &f->info; }
static void release(void *p, void *resource)
{ Fixture *f=(Fixture *)p; CHECK(resource==&f->info && f->refs>0); --f->refs; ++f->releases; }
static void info(void *p, void *resource, ThreeMMActorSoundInfo *out)
{ Fixture *f=(Fixture *)p; CHECK(resource==&f->info); *out=f->info; }
static int reserve(void *p, ThreeMMU32 entries, ThreeMMU32 bytes)
{ CHECK(entries==2 && bytes==4+sizeof(ThreeMMActorSound)); return !((Fixture *)p)->reserve_fail; }
static int child(void *p, ThreeMMU32 number, ThreeMMU32 *out) { (void)p; (void)number; *out=3; return 1; }
static int insert_event(void *p, ThreeMMI32 index, ThreeMMU32 bytes, const void *payload, const ThreeMMActorEvent *event)
{
    Fixture *f=(Fixture *)p;
    if (f->insert_fail) return 0;
    CHECK(index>=0 && index<=f->count && bytes<=sizeof(Payload));
    memmove(f->event+index+1,f->event+index,(size_t)(f->count-index)*sizeof(f->event[0]));
    memmove(f->data+index+1,f->data+index,(size_t)(f->count-index)*sizeof(f->data[0]));
    f->event[index]=*event; memset(&f->data[index],0,sizeof(Payload));
    if (payload) memcpy(&f->data[index],payload,bytes);
    ++f->count; return 1;
}
static void duplicate_tag(void *p, ThreeMMI32 index) { (void)p; (void)index; }
static void write_payload(void *p, ThreeMMI32 index, const void *value)
{ ((Fixture *)p)->data[index].sound=*(const ThreeMMActorSound *)value; }
static void copy_payload(void *p, ThreeMMI32 destination, ThreeMMI32 source)
{ Fixture *f=(Fixture *)p; f->data[destination]=f->data[source]; }
static int apply_event(void *p, ThreeMMActorState *state, ThreeMMI32 index)
{ Fixture *f=(Fixture *)p; (void)state; f->inserted=f->data[index].sound; return 1; }
static void get_range(void *p, ThreeMMActorEditRange *range) { (void)p; range->first=1; range->last=30; }
static void invalidate(void *p) { (void)p; }
static int suppressed(void *p) { return ((Fixture *)p)->suppressed; }
static void set_frame(void *p) { ++((Fixture *)p)->set_frame; }
static void midi(void *p) { ++((Fixture *)p)->midi; }
static int default_sound(void *p, ThreeMMI32 action, ThreeMMI32 cel, int *exists, ThreeMMTag *tag)
{ (void)p; (void)action; memset(tag,0,sizeof(*tag)); tag->sid=2; *exists=cel==1; return 1; }
static ThreeMMActorSoundEditServices init(Fixture *f)
{
    ThreeMMActorSoundEditServices s;
    memset(f,0,sizeof(*f)); memset(&s,0,sizeof(s));
    three_mm_actor_state_init(&f->state); CHECK(three_mm_actor_sounds_init(&f->list,NULL));
    f->state.action=0; f->state.cel=5; f->state.frame=4; f->state.actionEvent=0;
    f->volume_seen=-7; f->info.style=2; f->info.volume=222; f->suppressed=1;
    s.context=f; s.runtime.context=f; s.runtime.cel_count=cel_count; s.runtime.resolve=resolve;
    s.runtime.fetch=fetch; s.runtime.release=release;
    s.edits.context=f; s.edits.close_event_tag=close_tag; s.edits.delete_event=delete_event;
    s.edits.scene_frame=scene; s.edits.publish=publish; s.edits.update_sound_frame=indicator;
    s.edits.insert_event=insert_event; s.edits.duplicate_event_tag=duplicate_tag;
    s.edits.write_payload=write_payload; s.edits.copy_payload=copy_payload; s.edits.apply_event=apply_event;
    s.edits.dirty=dirty; s.edits.get_range=get_range; s.edits.invalidate_range=invalidate;
    s.write_sound=write_sound; s.dirty=dirty; s.query_new=query_new; s.query_add=query_add; s.query_release=query_release;
    s.resolve_scene=resolve_scene; s.sound_info=info; s.reserve_events=reserve; s.sound_child=child;
    s.sounds_suppressed=suppressed; s.set_sound_frame=set_frame; s.midi_error=midi;
    s.default_sound=default_sound;
    return s;
}
static void append(Fixture *f, int kind, int frame, int cel, int style)
{
    int i=f->count++;
    f->event[i].aet=kind; f->event[i].nfrm=frame;
    f->data[i].sound.cel=cel; f->data[i].sound.style=style;
    f->data[i].sound.volume=100+i; f->data[i].sound.loop=i;
    f->data[i].sound.tag.sid=2; f->data[i].sound.tag.number=10+i;
}
static void query_tests(void)
{
    Fixture f; ThreeMMActorSoundEditServices s; ThreeMMActorSoundQuery out; int fail;
    for (fail=0; fail<3; ++fail)
    {
        s=init(&f); append(&f,THREE_MM_ACTOR_SOUND,4,-1,2); append(&f,THREE_MM_ACTOR_SOUND,4,-1,2);
        f.state.firstEventInFrame=0; f.state.nextEvent=2; f.query_fail=fail;
        memset(&out,0,sizeof(out)); out.volume=-7; out.tags=&s; out.context=&f; out.publish=outputs;
        CHECK(three_mm_actor_sound_query(&f.state,&f.list,&f,2,0,&out,&tables,&s)==(fail==0));
        CHECK(f.calls==0 && out.volume==(fail==1 ? -7 : fail==2 ? 100 : 101));
        if (fail) CHECK(out.tags==&s && f.query_live==0); else { CHECK(out.tags==&f); query_release(&f,out.tags); }
        three_mm_actor_sounds_dispose(&f.list);
    }
    s=init(&f); f.new_fail=1; memset(&out,0,sizeof(out)); out.tags=&s;
    CHECK(!three_mm_actor_sound_query(&f.state,&f.list,&f,2,1,&out,&tables,&s) && f.calls==0 && out.tags==&s);
    f.new_fail=0; f.fail_count=1;
    CHECK(!three_mm_actor_sound_query(&f.state,&f.list,&f,2,1,&out,&tables,&s) && f.query_live==0);
    f.fail_count=0;
    CHECK(three_mm_actor_sound_query(&f.state,&f.list,&f,2,1,&out,&tables,&s) && out.tags==NULL && f.query_live==0);
    three_mm_actor_sounds_dispose(&f.list);
    s=init(&f); append(&f,THREE_MM_ACTOR_SOUND,4,-1,2); append(&f,THREE_MM_ACTOR_SOUND,4,-1,2);
    f.state.nextEvent=2; f.data[0].sound.tag.sid=f.data[1].sound.tag.sid=0; f.resolve_fail=2;
    memset(&out,0,sizeof(out)); out.tags=&s; out.volume=-7; out.context=&f; out.publish=outputs;
    CHECK(!three_mm_actor_sound_query(&f.state,&f.list,&f,2,0,&out,&tables,&s));
    CHECK(f.resolves==2 && f.query_live==0 && out.tags==&s && out.volume==100 && f.volume_seen==100);
    CHECK(f.data[0].sound.tag.number==10 && f.data[1].sound.tag.number==11);
    three_mm_actor_sounds_dispose(&f.list);
}
static void edit_tests(void)
{
    Fixture f; ThreeMMActorSoundEditServices s=init(&f); ThreeMMActorMotionSound record;
    append(&f,THREE_MM_ACTOR_ACTION,1,0,0); append(&f,THREE_MM_ACTOR_SOUND,2,1,2);
    append(&f,THREE_MM_ACTOR_SOUND,3,1,2); append(&f,THREE_MM_ACTOR_SOUND,4,-1,2);
    f.state.firstEventInFrame=3; f.state.nextEvent=4;
    CHECK(three_mm_actor_sounds_insert(&f.list,&f.event[1],&f.data[1].sound));
    CHECK(three_mm_actor_sound_volume(&f.state,&f.list,&f,2,1,333,&tables,&s));
    CHECK(f.data[1].sound.volume==101 && f.data[2].sound.volume==333 && f.data[3].sound.volume==103);
    CHECK(three_mm_actor_sounds_get(&f.list,0,&record) && record.sound.volume==333);
    f.fail_count=f.calls+2;
    CHECK(!three_mm_actor_sound_delete(&f.state,&f.list,&f,2,1,&tables,&s));
    CHECK(f.count==3 && f.deleted==1 && f.list.count==1 && f.dirty==1 && f.state.nextEvent==3);
    f.state.frame=4; f.state.firstEventInFrame=2;
    CHECK(three_mm_actor_sound_in_frame(&f.state,&f,4,&tables));
    CHECK(!three_mm_actor_sound_in_frame(&f.state,&f,5,&tables));
    three_mm_actor_edit_remove(&f.state,&f,2,1,&tables,&s.edits);
    CHECK(f.indicator==1 && f.state.nextEvent==2 && f.state.firstEventInFrame==2);
    three_mm_actor_sounds_dispose(&f.list);
    s=init(&f); append(&f,THREE_MM_ACTOR_SOUND,1,-1,2); append(&f,THREE_MM_ACTOR_SOUND,2,-1,2);
    f.data[0].sound.tag.sid=f.data[1].sound.tag.sid=0; f.resolve_fail=1;
    CHECK(!three_mm_actor_sound_resolve_all(&f,88,&tables,&s));
    CHECK(f.resolves==2 && f.data[0].sound.tag.number==10 && f.data[1].sound.tag.number==111);
    three_mm_actor_sounds_dispose(&f.list);
}
static void set_tests(void)
{
    Fixture f; ThreeMMActorSoundEditServices s; ThreeMMTag tag; int fail,ok;
    memset(&tag,0,sizeof(tag)); tag.sid=2;
    for (fail=0; fail<5; ++fail)
    {
        s=init(&f); f.fetch_fail=fail==1; f.reserve_fail=fail==2; f.insert_fail=fail==3;
        if (fail==4) f.info.style=4;
        ok=three_mm_actor_sound_set(&f.state,&f.list,&f,7,&tag,1,0,1,-1,0,0,&tables,&s);
        CHECK(ok==(fail==0) && f.refs==0 && f.releases==(fail==1 ? 0 : 1));
        CHECK(f.midi==(fail==4) && f.set_frame==(fail==0));
        if (ok) CHECK(f.inserted.cel==1 && f.inserted.style==2 && f.inserted.volume==222);
        three_mm_actor_sounds_dispose(&f.list);
    }
    s=init(&f);
    CHECK(three_mm_actor_sound_defaults(&f.state,&f.list,&f,7,0,0,&tables,&s));
    CHECK(f.inserted.queue==222 && f.inserted.cel==-1 && f.inserted.volume==2 && f.inserted.style==1);
    CHECK(f.refs==0 && f.releases==2);
    three_mm_actor_sounds_dispose(&f.list);
    s=init(&f); append(&f,THREE_MM_ACTOR_SOUND,4,-1,3); append(&f,THREE_MM_ACTOR_SOUND,4,-1,2);
    append(&f,THREE_MM_ACTOR_SOUND,5,-1,2); append(&f,THREE_MM_ACTOR_SOUND,6,-1,2);
    f.data[0].sound.no_sound=f.data[1].sound.no_sound=f.data[3].sound.no_sound=1;
    CHECK(three_mm_actor_sound_set(&f.state,&f.list,&f,32768,&tag,0,0,0,100,2,0,&tables,&s));
    CHECK(f.deleted==2 && f.count==3 && f.data[1].sound.no_sound==0 && f.data[2].sound.no_sound==1);
    three_mm_actor_sounds_dispose(&f.list);
    CHECK(three_mm_sound_queue_number(2,32768,1)==three_mm_sound_queue_number(3,32768,1));
    CHECK(three_mm_sound_queue_number(2,7,1)!=three_mm_sound_queue_number(3,7,1));
}
int main(void) { query_tests(); edit_tests(); set_tests(); puts("actor sound editing passed"); return 0; }
