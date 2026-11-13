#include "three_mm/actor_undo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"undo check %d: %s\n",__LINE__,#c); exit(1); } } while (0)
typedef struct Actor { int refs,id,reset; } Actor;
typedef struct Fixture
{
    ThreeMMActorUndoState state;
    ThreeMMActorUndoServices services;
    Actor old,incoming;
    Actor *live;
    char trace[128]; int count,fail,empty,text,no_view,missing,rename_failure,dead;
    int name,captured,assigned,selected,invalidated,cleared,flushes,plays,retained;
    int create_refs,registered,created,named,reset,create_sound,create_id;
    ThreeMMI32 sought;
} Fixture;
static void mark(Fixture *f,char c) { CHECK(!f->dead && f->count<126); f->trace[f->count++]=c; f->trace[f->count]=0; }
static void read_state(void *p,ThreeMMActorUndoState *s) { Fixture *f=(Fixture *)p; CHECK(!f->dead); *s=f->state; }
static void publish(void *p,const ThreeMMActorUndoState *s) { Fixture *f=(Fixture *)p; CHECK(!f->dead); f->state=*s; }
static int switch_scene(void *p,void *movie,ThreeMMI32 scene)
{ Fixture *f=(Fixture *)p; CHECK(movie==f && scene==3); mark(f,'S'); return f->fail!=1; }
static void *current(void *p,void *movie) { CHECK(p==movie); return p; }
static int seek(void *p,void *scene,ThreeMMI32 frame)
{ Fixture *f=(Fixture *)p; CHECK(scene==f && frame==f->state.frame); f->sought=frame; mark(f,'G'); return f->fail!=2; }
static void clear(void *p,void *movie)
{ Fixture *f=(Fixture *)p; CHECK(movie==f); mark(f,'C'); ++f->cleared; f->dead=!f->retained; }
static void flush(void *p,void *movie) { Fixture *f=(Fixture *)p; CHECK(movie==f); mark(f,'F'); ++f->flushes; }
static void play(void *p,void *movie) { Fixture *f=(Fixture *)p; CHECK(movie==f); mark(f,'P'); ++f->plays; }
static void *find(void *p,void *scene,ThreeMMI32 id)
{ Fixture *f=(Fixture *)p; CHECK(scene==f && id==f->state.actor_id); mark(f,'L'); return f->missing?NULL:f->live; }
static void retain(void *p,void *actor)
{ Fixture *f=(Fixture *)p; mark(f,'R'); CHECK(actor); if (actor==&f->incoming) CHECK(f->state.actor==actor); ++((Actor *)actor)->refs; }
static void release(void *p,void *actor)
{ Fixture *f=(Fixture *)p; Actor *a=(Actor *)actor; mark(f,'D'); CHECK(a && a->refs>0); if (a==&f->incoming) CHECK(f->state.actor==a); --a->refs; }
static ThreeMMI32 id(void *p,void *actor) { (void)p; CHECK(actor); return ((Actor *)actor)->id; }
static void remove_actor(void *p,void *scene,ThreeMMI32 actor)
{
    Fixture *f=(Fixture *)p; CHECK(scene==f && actor==f->state.actor_id); mark(f,'X');
    if (f->live) { --f->live->refs; f->live=NULL; }
}
static int add(void *p,void *scene,void *actor)
{
    Fixture *f=(Fixture *)p; CHECK(scene==f && actor==f->state.actor); mark(f,'A');
    CHECK(f->state.actor_id==((Actor *)actor)->id);
    if (f->fail==3) return 0;
    ++((Actor *)actor)->refs; if (f->live) --f->live->refs; f->live=(Actor *)actor; return 1;
}
static int has_name(void *p) { return ((Fixture *)p)->name!=0; }
static int capture(void *p,void *movie,ThreeMMI32 actor)
{ Fixture *f=(Fixture *)p; CHECK(movie==f && actor==f->state.actor_id); mark(f,'N'); f->captured=42; return f->fail!=4; }
static void assign(void *p,void *movie,ThreeMMI32 actor)
{ Fixture *f=(Fixture *)p; CHECK(movie==f && actor==f->incoming.id); mark(f,'W'); f->assigned=f->rename_failure?0:f->name; }
static void store(void *p) { Fixture *f=(Fixture *)p; mark(f,'B'); f->name=f->captured; }
static void *view(void *p,void *movie) { Fixture *f=(Fixture *)p; CHECK(movie==f); mark(f,'V'); return f->no_view?NULL:f; }
static int text(void *p,void *v) { CHECK(p==v); return ((Fixture *)p)->text; }
static void select_actor(void *p,void *scene,void *actor)
{ Fixture *f=(Fixture *)p; CHECK(scene==f && actor==f->live); mark(f,'Q'); ++f->selected; }
static void invalidate(void *p,void *scene)
{ Fixture *f=(Fixture *)p; CHECK(scene==f && !f->state.actor); mark(f,'I'); ++f->invalidated; }
static void reset_actor(void *p,void *actor)
{ Fixture *f=(Fixture *)p; CHECK(actor==f->state.actor); mark(f,'Z'); ++((Actor *)actor)->reset; }
static int invoke(void *p)
{
    Fixture *f=(Fixture *)p; ThreeMMActorUndoState state=f->state;
    return three_mm_actor_undo_toggle(&state,&f->services);
}
static void init(Fixture *f)
{
    ThreeMMActorUndoServices *s;
    memset(f,0,sizeof(*f)); s=&f->services; s->context=f;
    s->read=read_state; s->publish=publish; s->invoke_undo=invoke; s->switch_scene=switch_scene; s->current_scene=current;
    s->seek=seek; s->clear_undo=clear; s->flush=flush; s->play=play; s->find_actor=find; s->retain=retain; s->release=release;
    s->actor_id=id; s->remove_actor=remove_actor; s->add_actor=add; s->has_name=has_name; s->capture_name=capture;
    s->assign_name=assign; s->store_name=store; s->view=view; s->text_mode=text; s->select_actor=select_actor;
    s->invalidate_range=invalidate; s->reset_actor=reset_actor;
    f->old.refs=f->incoming.refs=1; f->old.id=f->incoming.id=7; f->live=&f->old;
    f->state.movie=f; f->state.actor=&f->incoming; f->state.actor_id=7; f->state.scene=3;
    f->state.frame=11; f->state.last_frame=22;
}
static void *create_undo(void *p) { Fixture *f=(Fixture *)p; mark(f,'U'); ++f->created; if (f->fail==1) return NULL; f->create_refs=1; return f; }
static void set_actor(void *p,void *undo,void *actor)
{ Fixture *f=(Fixture *)p; CHECK(undo==f && actor==&f->incoming); mark(f,'a'); ++f->incoming.refs; }
static void set_id(void *p,void *undo,ThreeMMI32 actor) { Fixture *f=(Fixture *)p; CHECK(undo==f); mark(f,'i'); f->create_id=actor; }
static void set_sound(void *p,void *undo,int sound) { Fixture *f=(Fixture *)p; CHECK(undo==f); mark(f,'s'); f->create_sound=sound; }
static void set_name(void *p,void *undo,const void *name) { Fixture *f=(Fixture *)p; CHECK(undo==f && name==&f->name); mark(f,'n'); ++f->named; }
static int register_undo(void *p,void *undo)
{ Fixture *f=(Fixture *)p; CHECK(undo==f); mark(f,'j'); if (f->fail==2) return 0; ++f->create_refs; ++f->registered; return 1; }
static void clear_create(void *p) { Fixture *f=(Fixture *)p; mark(f,'c'); ++f->cleared; }
static void release_create(void *p,void *undo)
{ Fixture *f=(Fixture *)p; CHECK(undo==f && f->create_refs>0); mark(f,'d'); if (!--f->create_refs) --f->incoming.refs; }
static void reset_create(void *p,void *actor)
{ Fixture *f=(Fixture *)p; CHECK(actor==&f->incoming && f->create_refs==1 && f->registered); mark(f,'z'); ++f->reset; }
int main(void)
{
    Fixture f; ThreeMMActorUndoState state; ThreeMMActorUndoCreateServices c; int mode,fail,result;
    init(&f); state=f.state;
    CHECK(three_mm_actor_undo_toggle(&state,&f.services));
    CHECK(!strcmp(f.trace,"SGFLRAVQDIZF") && f.state.actor==&f.old && f.live==&f.incoming);
    CHECK(f.old.refs==1 && f.incoming.refs==1 && f.old.reset==1);
    for (mode=0;mode<64;++mode)
    {
        init(&f); f.name=(mode&1)?17:0; f.state.sound=(mode&2)!=0;
        f.text=(mode&4)!=0; f.no_view=(mode&8)!=0; f.rename_failure=(mode&16)!=0;
        if (mode&32) f.incoming.id=9;
        state=f.state; CHECK(three_mm_actor_undo_toggle(&state,&f.services));
        CHECK(f.state.actor==&f.old && f.old.reset==1 && f.live==&f.incoming);
        CHECK(f.state.actor_id==f.incoming.id && f.old.refs==1 && f.incoming.refs==1);
        CHECK(f.selected==(!f.text && !f.no_view) && f.plays==f.state.sound && f.flushes==2-f.state.sound);
        CHECK(!f.name || f.name==42); CHECK(f.assigned==((mode&1) && !f.rename_failure?17:0));
    }
    for (fail=1;fail<=4;++fail)
    {
        init(&f); f.fail=fail; f.name=17; f.incoming.id=9; state=f.state;
        result=three_mm_actor_undo_toggle(&state,&f.services); CHECK(result==(fail==4));
        if (fail==1) CHECK(!strcmp(f.trace,"S") && !f.cleared);
        if (fail==2) CHECK(!strcmp(f.trace,"SGC") && f.dead && f.cleared==1);
        if (fail==3) CHECK(f.state.actor==&f.incoming && f.state.actor_id==9 && f.old.refs==0 && !f.live && f.name==42 && !f.invalidated);
        if (fail==4) CHECK(f.name==17 && !f.assigned && !f.cleared);
    }
    init(&f); f.state.actor=NULL; state=f.state; CHECK(three_mm_actor_undo_toggle(&state,&f.services));
    CHECK(!strcmp(f.trace,"SGFLRXIZF") && !f.live && f.state.actor==&f.old && f.old.refs==1);
    init(&f); f.missing=1; f.live=NULL; f.old.refs=0; state=f.state;
    CHECK(three_mm_actor_undo_toggle(&state,&f.services) && !f.state.actor && f.live==&f.incoming && !f.old.reset);
    for (fail=0;fail<4;++fail)
    {
        init(&f); f.state.sooner_later=1; f.fail=fail; f.retained=1; state=f.state;
        CHECK(three_mm_actor_undo_redo(&state,&f.services)==!fail);
        CHECK(f.sought==(fail==1?0:22) && f.state.frame==11 && f.state.actor==(fail?&f.incoming:&f.old));
    }
    init(&f); state=f.state; CHECK(three_mm_actor_undo_redo(&state,&f.services) && f.sought==11);
    init(&f); f.state.actor=NULL; state=f.state; three_mm_actor_undo_set_actor(&state,&f.incoming,&f.services);
    CHECK(f.state.actor==&f.incoming && f.incoming.refs==2); three_mm_actor_undo_dispose(&state,&f.services);
    CHECK(!f.state.actor && f.incoming.refs==1); three_mm_actor_undo_dispose(&state,&f.services); CHECK(f.incoming.refs==1);
    for (mode=0;mode<2;++mode) for (fail=0;fail<3;++fail)
    {
        init(&f); f.fail=fail; memset(&c,0,sizeof(c)); c.context=&f;
        c.create=create_undo; c.set_actor=set_actor; c.set_id=set_id; c.set_sound=set_sound; c.set_name=set_name;
        c.add=register_undo; c.clear=clear_create; c.release=release_create; c.reset_actor=reset_create;
        CHECK(three_mm_actor_undo_create(&f.incoming,29,1,mode?&f.name:NULL,&c)==!fail);
        CHECK(f.created==1 && f.reset==!fail && f.incoming.refs==(fail?1:2));
        CHECK(!strcmp(f.trace,fail==1?"U":fail==2?(mode?"Uaisnjcd":"Uaisjcd"):(mode?"Uaisnjdz":"Uaisjdz")));
        if (!fail) CHECK(f.create_id==29 && f.create_sound==1 && f.named==mode && f.create_refs==1);
    }
    puts("actor undo C89 checks passed"); return 0;
}
