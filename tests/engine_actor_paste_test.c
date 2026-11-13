#include "three_mm/actor_paste.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"paste check at %d: %s\n",__LINE__,#c); exit(1); } } while (0)
typedef union Payload { ThreeMMActorEntrance entrance; ThreeMMActorSound sound; } Payload;
typedef struct Fixture
{
    ThreeMMActorRuntime r;
    ThreeMMActorEvent events[8]; Payload payloads[8]; ThreeMMActorRoutePoint point;
    ThreeMMActorMotionServices motion; ThreeMMActorPasteServices services;
    int count,old_template,new_template,source_cache,target_cache,scene;
    int fail,same_cache,text,new_text,tags,old_refs,new_refs,duplicated,closed,released;
    int ensured,saved,fetched,bound,placed,dirty,writes,payload_writes,removed,exists,sounds,children,yaw;
    int diagnostics[4];
} Fixture;
static void read_runtime(void *p,ThreeMMActorRuntime *r) { *r=((Fixture *)p)->r; }
static void write_runtime(void *p,const ThreeMMActorRuntime *r) { ((Fixture *)p)->r=*r; }
static int is_text(void *p,void *object)
{ Fixture *f=(Fixture *)p; CHECK(object==&f->old_template || object==&f->new_template); return object==&f->old_template ? f->text : f->new_text; }
static int ensure(void *p,void *scene,void **cache)
{
    Fixture *f=(Fixture *)p; CHECK(scene==&f->scene && !f->bound); ++f->ensured;
    CHECK(f->r.first==10); *cache=f->same_cache ? &f->source_cache : &f->target_cache; return f->fail!=1;
}
static void duplicate(void *p,const ThreeMMTag *tag)
{ Fixture *f=(Fixture *)p; CHECK(tag->cache==&f->source_cache); ++f->tags; ++f->duplicated; }
static void close_tag(void *p,ThreeMMTag *tag)
{
    Fixture *f=(Fixture *)p; CHECK(f->tags>0); --f->tags; ++f->closed;
    CHECK(!f->bound); tag->cache=NULL;
}
static int save_template(void *p,ThreeMMTag *tag,void *cache)
{
    Fixture *f=(Fixture *)p; CHECK(f->duplicated==1 && f->tags==2 && cache==&f->target_cache);
    ++f->saved; if (f->fail==2) return 0; tag->cache=cache; tag->number=77; return 1;
}
static void *fetch_template(void *p,const ThreeMMTag *tag)
{
    Fixture *f=(Fixture *)p; CHECK(tag->cache==&f->target_cache && tag->number==77); ++f->fetched;
    if (f->fail==3) return NULL;
    ++f->new_refs; return &f->new_template;
}
static void release_template(void *p,void *object)
{
    Fixture *f=(Fixture *)p; CHECK(object==&f->old_template && f->old_refs==1);
    CHECK(f->r.template_object==object && f->r.template_tag.cache==&f->target_cache);
    CHECK(f->closed==1 && f->new_refs==1); --f->old_refs; ++f->released;
}
static void bind_scene(void *p,void *scene)
{
    Fixture *f=(Fixture *)p; CHECK(scene==&f->scene && f->r.life_dirty);
    CHECK(f->r.state.frame==three_mm_fixed_sub(f->r.first,1));
    f->r.scene=scene; f->r.state.cel=91; ++f->bound;
}
static void placement(void *p,ThreeMMActorPoint *point)
{
    Fixture *f=(Fixture *)p; CHECK(f->bound==1 && f->r.state.cel==91); ++f->placed;
    point->dxr=100; point->dyr=200; point->dzr=-300;
}
static ThreeMMI32 event_count(void *p) { return ((Fixture *)p)->count; }
static void event(void *p,ThreeMMI32 i,ThreeMMActorEvent *e)
{ Fixture *f=(Fixture *)p; CHECK(i>=0 && i<f->count); *e=f->events[i]; }
static void route_point(void *p,ThreeMMI32 i,ThreeMMActorRoutePoint *point)
{ CHECK(i==0); *point=((Fixture *)p)->point; }
static void payload(void *p,ThreeMMI32 i,void *data)
{
    Fixture *f=(Fixture *)p; CHECK(i>=0 && i<f->count);
    if (f->events[i].aet==THREE_MM_ACTOR_ADD) memcpy(data,&f->payloads[i].entrance,sizeof(ThreeMMActorEntrance));
    else { CHECK(f->events[i].aet==THREE_MM_ACTOR_SOUND); memcpy(data,&f->payloads[i].sound,sizeof(ThreeMMActorSound)); }
}
static void write_event(void *p,ThreeMMI32 i,const ThreeMMActorEvent *e)
{ Fixture *f=(Fixture *)p; CHECK(i>=0 && i<f->count); f->events[i]=*e; ++f->writes; }
static void write_payload(void *p,ThreeMMI32 i,const void *data)
{
    Fixture *f=(Fixture *)p; CHECK(i>=0 && i<f->count); ++f->payload_writes;
    if (f->events[i].aet==THREE_MM_ACTOR_ADD) memcpy(&f->payloads[i].entrance,data,sizeof(ThreeMMActorEntrance));
    else memcpy(&f->payloads[i].sound,data,sizeof(ThreeMMActorSound));
}
static int sound_exists(void *p,const ThreeMMTag *tag)
{ Fixture *f=(Fixture *)p; CHECK(tag->cache==&f->source_cache); ++f->exists; return tag->number!=2; }
static int save_sound(void *p,void *scene,ThreeMMTag *tag)
{
    Fixture *f=(Fixture *)p; CHECK(scene==&f->scene); ++f->sounds;
    if (tag->number==3) return 0;
    /* The native tag rewrite is local until the child lookup succeeds. */
    tag->cache=&f->target_cache; tag->number+=100; return 1;
}
static int sound_child(void *p,void *scene,ThreeMMU32 number,ThreeMMU32 *child)
{
    Fixture *f=(Fixture *)p; CHECK(scene==&f->scene && number>=100); ++f->children;
    *child=number+1000; return number!=104;
}
static void remove_event(void *p,ThreeMMI32 i)
{
    Fixture *f=(Fixture *)p; CHECK(i>0 && i<f->count); ++f->removed; --f->count;
    memmove(f->events+i,f->events+i+1,(size_t)(f->count-i)*sizeof(f->events[0]));
    memmove(f->payloads+i,f->payloads+i+1,(size_t)(f->count-i)*sizeof(f->payloads[0]));
    --f->r.state.nextEvent; f->r.state.cel+=10;
}
static unsigned short camera_yaw(void *p,void *scene)
{ Fixture *f=(Fixture *)p; CHECK(scene==&f->scene); ++f->yaw; return 0x1234; }
static void dirty(void *p,void *scene)
{ Fixture *f=(Fixture *)p; CHECK(scene==&f->scene && f->placed); ++f->dirty; }
static void diagnostic(void *p,int which)
{ Fixture *f=(Fixture *)p; CHECK(which>=0 && which<4); ++f->diagnostics[which]; }
static void init(Fixture *f)
{
    ThreeMMActorPasteServices *s;
    memset(f,0,sizeof(*f)); s=&f->services; s->context=f;
    s->read=read_runtime; s->write=write_runtime; s->is_text=is_text; s->ensure_autosave=ensure;
    s->duplicate_tag=duplicate; s->close_tag=close_tag; s->save_template=save_template;
    s->fetch_template=fetch_template; s->release_template=release_template;
    s->bind_scene=bind_scene; s->placement=placement; s->sound_exists=sound_exists;
    s->save_sound=save_sound; s->sound_child=sound_child; s->write_event=write_event;
    s->write_payload=write_payload; s->remove_event=remove_event; s->camera_yaw=camera_yaw;
    s->dirty=dirty; s->diagnostic=diagnostic;
    f->motion.event_count=event_count; f->motion.event=event; f->motion.route_point=route_point; f->motion.payload=payload;
    f->tags=1; f->old_refs=1; f->r.template_object=&f->old_template;
    f->r.template_tag.sid=0; f->r.template_tag.cache=&f->source_cache;
    f->r.first=10; f->r.last=99; f->r.state.frame=33; f->r.state.nextEvent=7;
    f->r.state.routeTranslation.dxr=999; f->r.state.subrouteTranslation.dxr=555;
    f->r.origin.dxr=777; f->r.origin.dyr=888; f->r.origin.dzr=999;
    f->point.xyz.dxr=1; f->point.xyz.dyr=2; f->point.xyz.dzr=3;
    f->count=1; f->events[0].aet=THREE_MM_ACTOR_ADD; f->events[0].nfrm=10;
    f->payloads[0].entrance.translation.dxr=4; f->payloads[0].entrance.translation.dyr=5;
    f->payloads[0].entrance.translation.dzr=6; f->payloads[0].entrance.y=321;
    f->payloads[0].entrance.reserved=0xabcd;
}
static void add_sound(Fixture *f,int number,int sid)
{
    int i=f->count++; CHECK(i<8); f->events[i].aet=THREE_MM_ACTOR_SOUND; f->events[i].nfrm=10+i;
    f->payloads[i].sound.tag.sid=sid; f->payloads[i].sound.tag.cache=&f->source_cache;
    f->payloads[i].sound.tag.number=(ThreeMMU32)number; f->payloads[i].sound.child=55;
}
static int paste(Fixture *f,ThreeMMI32 frame,int migrate)
{ return three_mm_actor_paste(f,f,frame,&f->scene,migrate,&f->motion,&f->services); }
static void positioned(Fixture *f)
{
    CHECK(f->r.origin.dxr==95 && f->r.origin.dyr==193 && f->r.origin.dzr==-309);
    CHECK(!f->r.state.subrouteTranslation.dxr && !f->r.state.subrouteTranslation.dyr && !f->r.state.subrouteTranslation.dzr);
    CHECK(f->r.last==99 && f->r.state.cel>=91);
}
int main(void)
{
    Fixture f; int fail,migrate,same,text,new_text;
    for (migrate=0;migrate<2;++migrate) for (same=0;same<2;++same)
    for (text=0;text<2;++text) for (new_text=0;new_text<2;++new_text)
    {
        int transfer=migrate && text && !same;
        init(&f); f.text=text; f.new_text=new_text; f.same_cache=same;
        CHECK(paste(&f,20,migrate)); positioned(&f);
        CHECK(f.r.first==20 && f.r.state.frame==19 && f.events[0].nfrm==20);
        CHECK(f.r.state.routeTranslation.dxr==95 && f.r.state.routeTranslation.dyr==193 && f.r.state.routeTranslation.dzr==-309);
        CHECK(f.ensured==(migrate && text) && f.saved==transfer && f.fetched==transfer && f.released==transfer);
        CHECK(f.tags==1 && f.old_refs==!transfer && f.new_refs==transfer);
        CHECK(f.yaw==(transfer ? new_text : text));
        CHECK(f.payloads[0].entrance.y==(f.yaw ? 0x1234 : 321));
        CHECK(f.payloads[0].entrance.reserved==0xabcd && f.dirty==1);
    }
    for (fail=1;fail<=3;++fail)
    {
        init(&f); f.text=1; f.fail=fail;
        CHECK(!paste(&f,20,1)); CHECK(!f.bound && !f.placed && !f.dirty);
        CHECK(f.r.first==10 && f.r.state.frame==33 && f.tags==1 && f.old_refs==1 && !f.new_refs);
        CHECK(f.closed==(fail!=1) && f.duplicated==(fail!=1));
        CHECK(f.r.template_object==&f.old_template && f.r.template_tag.cache==&f.source_cache);
    }
    init(&f); f.events[0].aet=THREE_MM_ACTOR_REMOVE;
    CHECK(!paste(&f,20,1)); CHECK(f.bound==1 && f.placed==1 && !f.dirty);
    CHECK(f.r.first==20 && f.r.state.frame==19 && f.r.origin.dxr==777 && !f.writes);
    init(&f); CHECK(paste(&f,10,1)); CHECK(!f.writes && f.dirty==1);
    init(&f); f.events[0].nfrm=(ThreeMMI32)(-2147483647L-1L);
    CHECK(paste(&f,2147483647L,1)); CHECK(f.events[0].nfrm==2147483647L);
    init(&f); CHECK(paste(&f,(ThreeMMI32)(-2147483647L-1L),1)); CHECK(f.r.state.frame==2147483647L);
    /* Consecutive deletions revisit the shifted row, preserve removal cursor
     * changes, retime the tail once, and leave media sounds unadopted. */
    init(&f); add_sound(&f,2,0); add_sound(&f,3,0); add_sound(&f,1,0); add_sound(&f,8,7);
    CHECK(paste(&f,20,1)); positioned(&f);
    CHECK(f.count==3 && f.removed==2 && f.writes==5 && f.exists==3 && f.sounds==2 && f.children==1);
    CHECK(f.diagnostics[THREE_MM_PASTE_MISSING_SOUND]==1 && f.diagnostics[THREE_MM_PASTE_SAVE_SOUND_FAILED]==1);
    CHECK(f.r.state.cel==111 && f.r.state.nextEvent==5 && f.r.state.routeTranslation.dxr==95);
    CHECK(f.events[1].nfrm==23 && f.events[2].nfrm==24);
    CHECK(f.payloads[1].sound.tag.number==101 && f.payloads[1].sound.tag.cache==&f.target_cache);
    CHECK(f.payloads[1].sound.child==1101 && f.payloads[2].sound.tag.number==8);
    /* Child failure retains earlier writes, a retimed but original failing row,
     * and an untouched tail. It skips final yaw/origin update and dirty mark. */
    init(&f); f.text=1; f.same_cache=1; add_sound(&f,1,0); add_sound(&f,4,0); add_sound(&f,1,0);
    CHECK(!paste(&f,20,1)); positioned(&f);
    CHECK(f.writes==3 && f.payload_writes==1 && !f.dirty && !f.yaw && !f.removed);
    CHECK(f.events[1].nfrm==21 && f.events[2].nfrm==22 && f.events[3].nfrm==13);
    CHECK(f.payloads[1].sound.tag.number==101 && f.payloads[2].sound.tag.number==4);
    CHECK(f.payloads[2].sound.tag.cache==&f.source_cache && f.payloads[2].sound.child==55);
    CHECK(f.r.state.routeTranslation.dxr==999 && f.payloads[0].entrance.y==321);
    puts("C89 actor paste policy checks passed"); return 0;
}
