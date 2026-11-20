#include "mm/actor_undo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"undo check %d: %s\n",__LINE__,#c); exit(1); } } while (0)
typedef struct Actor { int refs,id,reset; } Actor;
static const char before[] = {'b',0,'f'};
static const char after[] = {'a',0,'t'};
typedef struct Fixture
{
    MMMovieUndoContext base;
    MMActorUndoState state;
    MMActorUndoServices services;
    MMActorOwner create_snapshot;
    Actor old,incoming;
    Actor *live;
    char trace[128]; int count,fail,text,no_view,missing,rename_failure,dead;
    MMMovieTitle rollcall;
    int assigned,selected,invalidated,cleared,flushes,plays,retained;
    int create_refs,registered,created,reset;
    MMI32 sought;
} Fixture;
static void mark(Fixture *f,char c) { CHECK(!f->dead && f->count<126); f->trace[f->count++]=c; f->trace[f->count]=0; }
static int matches(const MMMovieTitle *name,const void *text,MMU32 count)
{ return name->count==count && (!count || !memcmp(mm_movie_title_text(name),text,count*name->unit)); }
static int switch_scene(void *p,void *movie,MMI32 scene)
{ Fixture *f=(Fixture *)p; CHECK(movie==f && scene==3); mark(f,'S'); return f->fail!=1; }
static void *current(void *p,void *movie) { CHECK(p==movie); return p; }
static int seek(void *p,void *scene,MMI32 frame)
{ Fixture *f=(Fixture *)p; CHECK(scene==f && frame==f->base.frame); f->sought=frame; mark(f,'G'); return f->fail!=2; }
static void clear(void *p,void *movie)
{ Fixture *f=(Fixture *)p; CHECK(movie==f); mark(f,'C'); ++f->cleared; f->dead=!f->retained; }
static void flush(void *p,void *movie) { Fixture *f=(Fixture *)p; CHECK(movie==f); mark(f,'F'); ++f->flushes; }
static void play(void *p,void *movie) { Fixture *f=(Fixture *)p; CHECK(movie==f); mark(f,'P'); ++f->plays; }
static void *find(void *p,void *scene,MMI32 id)
{ Fixture *f=(Fixture *)p; CHECK(scene==f && id==f->state.actor_id); mark(f,'L'); return f->missing?NULL:f->live; }
static void retain(void *p,void *actor)
{ Fixture *f=(Fixture *)p; mark(f,'R'); CHECK(actor && (actor==f->state.actor || actor==f->live)); ++((Actor *)actor)->refs; }
static void release(void *p,void *actor)
{ Fixture *f=(Fixture *)p; Actor *a=(Actor *)actor; mark(f,'D'); CHECK(a && a->refs>0); if (a==&f->incoming) CHECK(f->state.actor==a); --a->refs; }
static MMI32 id(void *p,void *actor) { (void)p; CHECK(actor); return ((Actor *)actor)->id; }
static void remove_actor(void *p,void *scene,MMI32 actor)
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
static int find_name(void *p,void *movie,MMI32 actor,const void **text,MMU32 *count)
{
    Fixture *f=(Fixture *)p; CHECK(movie==f && actor==f->state.actor_id); mark(f,'N');
    if (f->fail==4) return 0;
    *text=mm_movie_title_text(&f->rollcall); *count=f->rollcall.count;
    return 1;
}
static void rename_actor(void *p,void *movie,MMI32 actor,const void *text,MMU32 count)
{
    Fixture *f=(Fixture *)p; CHECK(movie==f && actor==f->incoming.id); mark(f,'W');
    CHECK(matches(&f->state.name,text,count));
    if (!f->rename_failure)
    {
        CHECK(mm_movie_title_set(&f->rollcall,text,count));
        ++f->assigned;
    }
}
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
    Fixture *f=(Fixture *)p;
    return mm_actor_undo_toggle(&f->state,&f->services);
}
static void init(Fixture *f)
{
    MMActorUndoServices *s;
    memset(f,0,sizeof(*f)); s=&f->services; s->context=f;
    s->invoke_undo=invoke; s->switch_scene=switch_scene; s->current_scene=current;
    s->seek=seek; s->clear_undo=clear; s->flush=flush; s->play=play; s->find_actor=find; s->retain=retain; s->release=release;
    s->actor_id=id; s->remove_actor=remove_actor; s->add_actor=add;
    s->find_name=find_name; s->rename=rename_actor; s->view=view; s->text_mode=text; s->select_actor=select_actor;
    s->invalidate_range=invalidate; s->reset_actor=reset_actor;
    mm_movie_undo_context_init(&f->base); CHECK(mm_actor_undo_init(&f->state,&f->base,1));
    CHECK(mm_movie_title_init(&f->rollcall,1) && mm_movie_title_set(&f->rollcall,after,sizeof(after)));
    f->old.refs=f->incoming.refs=1; f->old.id=f->incoming.id=7; f->live=&f->old;
    f->base.movie=f; f->state.actor=&f->incoming; f->state.actor_id=7; f->base.scene=3;
    f->base.frame=11; f->state.last_frame=22;
}
static MMActorUndoState *create_undo(void *p,void **undo)
{
    Fixture *f=(Fixture *)p; mark(f,'U'); ++f->created;
    if (f->fail==1) return NULL;
    CHECK(mm_actor_undo_init(&f->state,&f->base,1));
    f->create_refs=1; *undo=f;
    return &f->state;
}
static void retain_create(void *p,void *actor)
{
    Fixture *f=(Fixture *)p;
    CHECK(actor==&f->incoming && f->state.actor==actor && !f->state.actor_id && !f->state.sound && !f->state.name.count);
    mark(f,'a'); ++f->incoming.refs;
}
static int register_undo(void *p,void *undo)
{
    Fixture *f=(Fixture *)p; CHECK(undo==f && f->state.actor==&f->incoming && f->state.actor_id==29 && f->state.sound);
    CHECK(!f->state.name.count || matches(&f->state.name,before,sizeof(before)));
    mark(f,'j'); if (f->fail==2) return 0; ++f->create_refs; ++f->registered; return 1;
}
static void clear_create(void *p) { Fixture *f=(Fixture *)p; mark(f,'c'); ++f->cleared; }
static void release_create(void *p,void *undo)
{ Fixture *f=(Fixture *)p; CHECK(undo==f && f->create_refs>0); mark(f,'d'); if (!--f->create_refs) --f->incoming.refs; }
static MMActorOwner *create_owner(void *p,void *actor)
{
    Fixture *f=(Fixture *)p;
    CHECK(actor==&f->incoming);
    return &f->create_snapshot;
}
static void reset_create(void *p)
{
    Fixture *f=(Fixture *)p;
    CHECK(f->create_refs==1 && f->registered && !f->create_snapshot.runtime.scene);
    mark(f,'z');
    ++f->reset;
}
static void clear_recording(void *p,int recording)
{
    ((Fixture *)p)->create_snapshot.runtime.recording.recording=recording;
}
static void default_costume(void *p) { (void)p; }
static void route_services(void *p,void *actor,MMActorRouteEditServices *route)
{
    Fixture *f=(Fixture *)p;
    CHECK(actor==&f->incoming);
    memset(route,0,sizeof(*route));
    route->edits.context=f;
    route->life_dirty=reset_create;
    route->recording=clear_recording;
    route->default_costume=default_costume;
}
int main(void)
{
    Fixture f; MMActorUndoCreateServices c; MMActorOwnerServices actors; int mode,fail,result;
    unsigned unit,i;
    unsigned short text_units[255];
    unsigned char text_bytes[255];
    init(&f);
    CHECK(mm_actor_undo_toggle(&f.state,&f.services));
    CHECK(!strcmp(f.trace,"SGFLRAVQDIZF") && f.state.actor==&f.old && f.live==&f.incoming);
    CHECK(f.old.refs==1 && f.incoming.refs==1 && f.old.reset==1);
    for (mode=0;mode<64;++mode)
    {
        init(&f); f.state.sound=(mode&2)!=0;
        if (mode&1) CHECK(mm_actor_undo_set_name(&f.state,before,sizeof(before)));
        f.text=(mode&4)!=0; f.no_view=(mode&8)!=0; f.rename_failure=(mode&16)!=0;
        if (mode&32) f.incoming.id=9;
        CHECK(mm_actor_undo_toggle(&f.state,&f.services));
        CHECK(f.state.actor==&f.old && f.old.reset==1 && f.live==&f.incoming);
        CHECK(f.state.actor_id==f.incoming.id && f.old.refs==1 && f.incoming.refs==1);
        CHECK(f.selected==(!f.text && !f.no_view) && f.plays==f.state.sound && f.flushes==2-f.state.sound);
        CHECK(matches(&f.state.name,after,(mode&1)?sizeof(after):0));
        CHECK(f.assigned==((mode&1) && !f.rename_failure));
        CHECK(matches(&f.rollcall,f.assigned?before:after,sizeof(after)));
    }
    for (fail=1;fail<=4;++fail)
    {
        init(&f); f.fail=fail; f.incoming.id=9;
        CHECK(mm_actor_undo_set_name(&f.state,before,sizeof(before)));
        result=mm_actor_undo_toggle(&f.state,&f.services); CHECK(result==(fail==4));
        if (fail==1) CHECK(!strcmp(f.trace,"S") && !f.cleared);
        if (fail==2) CHECK(!strcmp(f.trace,"SGC") && f.dead && f.cleared==1);
        if (fail==3) CHECK(f.state.actor==&f.incoming && f.state.actor_id==9 && f.old.refs==0 && !f.live && matches(&f.state.name,after,sizeof(after)) && !f.invalidated);
        if (fail==4) CHECK(matches(&f.state.name,before,sizeof(before)) && !f.assigned && !f.cleared);
    }
    init(&f); f.state.actor=NULL; CHECK(mm_actor_undo_toggle(&f.state,&f.services));
    CHECK(!strcmp(f.trace,"SGFLRXIZF") && !f.live && f.state.actor==&f.old && f.old.refs==1);
    init(&f); f.missing=1; f.live=NULL; f.old.refs=0;
    CHECK(mm_actor_undo_toggle(&f.state,&f.services) && !f.state.actor && f.live==&f.incoming && !f.old.reset);
    for (fail=0;fail<4;++fail)
    {
        init(&f); f.state.sooner_later=1; f.fail=fail; f.retained=1;
        CHECK(mm_actor_undo_redo(&f.state,&f.services)==!fail);
        CHECK(f.sought==(fail==1?0:22) && f.base.frame==11 && f.state.actor==(fail?&f.incoming:&f.old));
    }
    init(&f); CHECK(mm_actor_undo_redo(&f.state,&f.services) && f.sought==11);
    init(&f); f.state.actor=NULL; mm_actor_undo_set_actor(&f.state,&f.incoming,&f.services);
    CHECK(f.state.actor==&f.incoming && f.incoming.refs==2); mm_actor_undo_dispose(&f.state,&f.services);
    CHECK(!f.state.actor && f.incoming.refs==1); mm_actor_undo_dispose(&f.state,&f.services); CHECK(f.incoming.refs==1);
    for (mode=0;mode<2;++mode) for (fail=0;fail<3;++fail)
    {
        init(&f); f.fail=fail; memset(&c,0,sizeof(c)); c.context=&f;
        c.create=create_undo; c.add=register_undo; c.clear=clear_create; c.release=release_create;
        memset(&actors,0,sizeof(actors));
        actors.context=&f; actors.owner=create_owner; actors.retain=retain_create; actors.route_services=route_services;
        mm_actor_runtime_init(&f.create_snapshot.runtime);
        f.create_snapshot.runtime.state.frame=11;
        f.create_snapshot.runtime.scene=&f;
        f.create_snapshot.runtime.recording.recording=1;
        CHECK(mm_actor_undo_create(&f.incoming,29,1,mode?before:NULL,mode?sizeof(before):0,&actors,&c)==!fail);
        CHECK(f.created==1 && f.reset==!fail && f.incoming.refs==(fail?1:2));
        CHECK(!strcmp(f.trace,fail==1?"U":fail==2?"Uajcd":"Uajdz"));
        if (fail!=1) CHECK(matches(&f.state.name,before,mode?sizeof(before):0));
        if (!fail)
            CHECK(f.create_refs==1 && !f.create_snapshot.runtime.scene &&
                  !f.create_snapshot.runtime.recording.recording && f.create_snapshot.runtime.state.frame==2147483647L);
        else
            CHECK(f.create_snapshot.runtime.scene==&f && f.create_snapshot.runtime.recording.recording &&
                  f.create_snapshot.runtime.state.frame==11);
    }
    for (i=0;i<255;++i) { text_units[i]=(unsigned short)(i==3?0:0x3a9+i); text_bytes[i]=(unsigned char)(i==3?0:128+i); }
    for (unit=1;unit<=2;++unit)
    {
        init(&f); CHECK(mm_actor_undo_init(&f.state,&f.base,unit));
        CHECK(f.state.base==&f.base && !f.state.actor && !f.state.name.count && f.state.name.unit==unit);
        CHECK(mm_actor_undo_set_name(&f.state,unit==1?(const void *)text_bytes:(const void *)text_units,255));
        CHECK(f.state.name.count==255 && !memcmp(mm_movie_title_text(&f.state.name),unit==1?(const void *)text_bytes:(const void *)text_units,255*unit));
        CHECK(!mm_actor_undo_set_name(&f.state,text_units,256) && f.state.name.count==255);
        CHECK(!mm_actor_undo_init(&f.state,&f.base,4) && f.state.name.count==255);
    }
    init(&f);
    CHECK(mm_actor_undo_init(&f.state,&f.base,2) && mm_movie_title_init(&f.rollcall,2));
    f.state.actor=&f.incoming; f.state.actor_id=f.incoming.id;
    CHECK(mm_actor_undo_set_name(&f.state,text_units,255) && mm_movie_title_set(&f.rollcall,text_units+2,5));
    CHECK(mm_actor_undo_toggle(&f.state,&f.services));
    CHECK(matches(&f.state.name,text_units+2,5) && matches(&f.rollcall,text_units,255));
    CHECK(mm_actor_undo_redo(&f.state,&f.services));
    CHECK(matches(&f.state.name,text_units,255) && matches(&f.rollcall,text_units+2,5));
    init(&f); f.state.sooner_later=1;
    CHECK(mm_actor_undo_set_name(&f.state,before,sizeof(before)));
    CHECK(mm_actor_undo_redo(&f.state,&f.services) && f.base.frame==11 && f.sought==22);
    CHECK(f.state.actor==&f.old && matches(&f.state.name,after,sizeof(after)) && f.old.refs==1 && f.incoming.refs==1);
    mm_actor_undo_dispose(&f.state,&f.services); CHECK(!f.state.actor && !f.old.refs && matches(&f.state.name,after,sizeof(after)));
    puts("actor undo C89 checks passed"); return 0;
}
