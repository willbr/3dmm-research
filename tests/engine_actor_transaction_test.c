#include "mm/actor_transaction.h"
#include "mm/actor_effect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"transaction check %d: %s\n",__LINE__,#c); exit(1); } } while (0)

typedef struct Actor
{
    MMActorOwner owner;
    int refs;
} Actor;

typedef struct Fixture
{
    MMActorTransactionServices transaction;
    MMActorDeleteServices deletion;
    MMActorTransactionResult result;
    MMActorDeleteResult alive;
    Actor live, copy;
    MMActorUndoState undo;
    MMMovieUndoContext undo_context;
    int body_refs, template_refs, track_blocks, undo_refs;
    int edited, registered, restored, detached, publications, trash, alive_writes;
    int fail_duplicate, fail_apply, fail_undo_create, fail_undo, fail_subroute, fail_seek, fail_lifetime;
    int has_output, seeks, frames[2], all, wants_alive, original, sounds, faults, last_fault;
    int override_frame, subroutes, empty_subroute, detach_on_seek, expect_sound;
    MMI32 add_frame;
    char trace[96];
    int count;
} Fixture;

static void mark(Fixture *f,char c)
{
    CHECK(f->count<94);
    f->trace[f->count++]=c;
    f->trace[f->count]=0;
}

static void *resize_track(void *p,void *data,MMU32 old,MMU32 bytes,int priority)
{
    Fixture *f=(Fixture *)p;
    int fresh=data==NULL;
    (void)old; (void)priority;
    if (f->fail_duplicate==2 && f->copy.refs)
        return NULL;
    data=realloc(data,bytes);
    if (data && fresh)
        ++f->track_blocks;
    return data;
}

static void free_track(void *p,void *data,MMU32 bytes)
{
    Fixture *f=(Fixture *)p;
    (void)bytes;
    CHECK(f->track_blocks>0);
    --f->track_blocks;
    free(data);
}

static MMActorOwner *owner(void *p,void *actor)
{
    Fixture *f=(Fixture *)p;
    CHECK(actor==&f->live || actor==&f->copy);
    return &((Actor *)actor)->owner;
}

static void *create_snapshot(void *p)
{
    Fixture *f=(Fixture *)p;
    mark(f,'D');
    if (f->fail_duplicate==1)
        return NULL;
    mm_actor_owner_init(&f->copy.owner);
    f->copy.refs=1;
    return &f->copy;
}

static void retain(void *p,void *object)
{
    Fixture *f=(Fixture *)p;
    if (object==&f->copy)
        ++f->copy.refs;
    else
    {
        CHECK(object==&f->body_refs || object==&f->template_refs);
        ++*(int *)object;
    }
}

static void release(void *p,void *object)
{
    Fixture *f=(Fixture *)p;
    if (object==&f->copy)
    {
        mark(f,'L');
        CHECK(f->copy.refs>0);
        if (!--f->copy.refs)
            mm_actor_owner_dispose(object,&f->transaction.actors);
    }
    else
    {
        CHECK(object==&f->body_refs || object==&f->template_refs);
        CHECK(*(int *)object>0);
        --*(int *)object;
    }
}

static void reset_costume(void *p)
{
    Fixture *f=(Fixture *)p;
    mark(f,'Z');
    CHECK(f->registered && f->undo_refs==1);
    CHECK(!f->copy.owner.runtime.body && !f->copy.owner.runtime.scene);
    CHECK(f->copy.owner.runtime.state.frame==2147483647L);
    ++f->detached;
}

static void life_dirty(void *p)
{
    ((Fixture *)p)->copy.owner.runtime.life_dirty=1;
}

static void recording(void *p,int enabled)
{
    ((Fixture *)p)->copy.owner.runtime.recording.recording=enabled;
}

static void route_services(void *p,void *actor,MMActorRouteEditServices *out)
{
    Fixture *f=(Fixture *)p;
    CHECK(actor==&f->copy);
    memset(out,0,sizeof(*out));
    out->edits.context=p;
    out->life_dirty=life_dirty;
    out->recording=recording;
    out->default_costume=reset_costume;
}

static void restored(void *p,void *scene)
{
    Fixture *f=(Fixture *)p;
    mark(f,'R');
    CHECK(scene==f);
    CHECK(f->live.owner.runtime.state.frame==10 && f->live.owner.runtime.state.nextEvent==42);
    f->edited=0;
    ++f->restored;
}

static int apply(void *p)
{
    Fixture *f=(Fixture *)p;
    MMActorEvent event;
    mark(f,'A');
    CHECK(f->copy.refs==1 && f->copy.owner.track.events!=f->live.owner.track.events);
    CHECK(mm_actor_events_get_fixed(f->live.owner.track.events,0,&event));
    event.nfrm=17;
    CHECK(mm_actor_events_set_fixed(f->live.owner.track.events,0,&event));
    f->live.owner.runtime.state.frame=17;
    ++f->edited;
    return !f->fail_apply;
}

static MMActorUndoState *create_undo(void *p,void **handle)
{
    Fixture *f=(Fixture *)p;
    mark(f,'U');
    if (f->fail_undo_create)
        return NULL;
    CHECK(mm_actor_undo_init(&f->undo,&f->undo_context,1));
    f->undo_refs=1;
    *handle=f;
    return &f->undo;
}

static int add_undo(void *p,void *handle)
{
    Fixture *f=(Fixture *)p;
    MMActorEvent event;
    CHECK(handle==f && f->undo.actor==&f->copy && f->copy.refs==2);
    CHECK(f->undo.sound==f->expect_sound && f->undo.actor_id==23);
    CHECK(mm_actor_events_get_fixed(f->copy.owner.track.events,0,&event) && event.nfrm==f->add_frame);
    mark(f,'J');
    if (f->fail_undo)
        return 0;
    ++f->registered;
    ++f->undo_refs;
    return 1;
}

static void clear_undo(void *p) { mark((Fixture *)p,'C'); }

static void release_undo(void *p,void *handle)
{
    Fixture *f=(Fixture *)p;
    CHECK(handle==f && f->undo_refs>0);
    mark(f,'W');
    if (!--f->undo_refs)
        release(p,f->undo.actor);
}

static void publish_snapshot(void *p,void *snapshot)
{
    Fixture *f=(Fixture *)p;
    mark(f,'O');
    CHECK(snapshot==&f->copy && f->result.snapshot==snapshot && f->detached && f->copy.refs==2);
    ++f->publications;
}

static void publish_alive(void *p,int value,int trashed)
{
    Fixture *f=(Fixture *)p;
    mark(f,'O');
    CHECK(value==f->alive.alive && trashed==f->alive.trashed);
    if (trashed)
        ++f->trash;
    else
        ++f->alive_writes;
}

static int subroute(void *p)
{
    Fixture *f=(Fixture *)p;
    MMActorEventTable *events=f->live.owner.track.events;
    mark(f,'E');
    ++f->edited;
    ++f->subroutes;
    if (f->empty_subroute)
        while (mm_actor_events_count(events))
            CHECK(mm_actor_events_delete(events,0));
    return !f->fail_subroute;
}

static int seek(void *p,MMI32 frame)
{
    Fixture *f=(Fixture *)p;
    mark(f,'S');
    CHECK(f->seeks<2);
    if (f->seeks==1 && f->has_output)
        CHECK(f->alive_writes==1);
    f->frames[f->seeks++]=frame;
    f->live.owner.runtime.state.frame=frame;
    f->live.owner.runtime.state.nextEvent=18;
    if (f->seeks==2 && f->detach_on_seek)
        f->live.owner.runtime.scene=NULL;
    return f->seeks!=f->fail_seek;
}

static int forward(void *p,int all,int wants_alive,MMI32 original)
{
    Fixture *f=(Fixture *)p;
    mark(f,'F');
    ++f->edited;
    f->all=all;
    f->wants_alive=wants_alive;
    f->original=original;
    f->live.owner.runtime.state.nextEvent=99;
    if (f->override_frame)
        f->live.owner.runtime.state.frame=f->override_frame;
    return 1;
}

static void sound(void *p)
{
    Fixture *f=(Fixture *)p;
    mark(f,'H');
    ++f->sounds;
}

static void diagnostic(void *p,int fault)
{
    Fixture *f=(Fixture *)p;
    mark(f,'X');
    ++f->faults;
    f->last_fault=fault;
}

static void init(Fixture *f)
{
    MMActorOwnerServices *actors;
    MMActorUndoCreateServices *undo;
    memset(f,0,sizeof(*f));
    f->transaction.context=f;
    f->transaction.apply=apply;
    actors=&f->transaction.actors;
    actors->context=f;
    actors->owner=owner;
    actors->create=create_snapshot;
    actors->retain=retain;
    actors->release=release;
    actors->route_services=route_services;
    actors->invalidate_views=restored;
    undo=&f->transaction.undo;
    undo->context=f;
    undo->create=create_undo;
    undo->add=add_undo;
    undo->clear=clear_undo;
    undo->release=release_undo;
    f->result.snapshot=f;
    f->result.context=f;
    f->result.publish=publish_snapshot;
    f->alive.alive=7;
    f->alive.trashed=3;
    f->alive.context=f;
    f->alive.publish=publish_alive;
    f->deletion.context=f;
    f->deletion.timeline=mm_actor_track_timeline_services();
    f->deletion.delete_subroute=subroute;
    f->deletion.seek=seek;
    f->deletion.delete_forward=forward;
    f->deletion.sound_frame=sound;
    f->deletion.diagnostic=diagnostic;
    mm_movie_undo_context_init(&f->undo_context);
    mm_actor_owner_init(&f->live.owner);
    f->live.owner.runtime.actor=23;
    f->live.owner.runtime.state.frame=10;
    f->live.owner.runtime.state.addEvent=0;
    f->live.owner.runtime.state.nextEvent=42;
    f->live.owner.runtime.scene=f;
    f->live.owner.runtime.body=&f->body_refs;
    f->live.owner.runtime.template_object=&f->template_refs;
    f->body_refs=f->template_refs=1;
    f->add_frame=9;
}

static void create_track(Fixture *f)
{
    const unsigned short angles[]={0,0,0};
    MMStorageAllocator allocator={NULL,resize_track,free_track};
    MMActorTrack *track=&f->live.owner.track;
    MMActorStationaryTrack rows;
    allocator.context=f;
    CHECK(mm_actor_track_create(track,&allocator,0,NULL));
    mm_actor_stationary_track(&rows,f->add_frame,angles);
    CHECK(mm_actor_path_insert(track->route,0,&rows.route));
    CHECK(mm_actor_events_insert(track->events,0,sizeof(rows.entrance),&rows.entrance,&rows.events[0]));
    CHECK(mm_actor_events_insert(track->events,1,sizeof(rows.action),&rows.action,&rows.events[1]));
    CHECK(mm_actor_events_insert(track->events,2,sizeof(rows.step),&rows.step,&rows.events[2]));
    f->live.owner.runtime.first=f->fail_lifetime?2147483647L:f->add_frame;
    f->live.owner.runtime.last=20;
    f->live.owner.runtime.life_dirty=1;
}

static void done(Fixture *f)
{
    if (f->result.snapshot==&f->copy)
        release(f,&f->copy);
    if (f->undo_refs)
        release_undo(f,f);
    CHECK(!f->copy.refs && !f->undo_refs);
    mm_actor_owner_dispose(&f->live,&f->transaction.actors);
    CHECK(!f->body_refs && !f->template_refs && !f->track_blocks);
}

static int delete_actor(Fixture *f,int all,int output)
{
    create_track(f);
    f->has_output=output;
    return mm_actor_transaction_delete(&f->live,all,output?&f->alive:NULL,&f->transaction,&f->deletion);
}

static void edits(void)
{
    static const char *fail_traces[]={"D","DL","DARL","DAURL","DAUJCWLRL"};
    Fixture f;
    MMActorEvent event;
    int kind,fail,output;
    for (kind=0;kind<=MM_TRANSACTION_SOUND;++kind) for (output=0;output<2;++output) for (fail=0;fail<6;++fail)
    {
        init(&f);
        create_track(&f);
        f.fail_duplicate=fail<=2?fail:0;
        f.fail_apply=fail==3;
        f.fail_undo_create=fail==4;
        f.fail_undo=fail==5;
        f.expect_sound=kind==MM_TRANSACTION_SOUND;
        CHECK(mm_actor_transaction_apply(&f.live,kind,output?&f.result:NULL,&f.transaction)==!fail);
        CHECK(mm_actor_events_get_fixed(f.live.owner.track.events,0,&event));
        if (!fail)
        {
            CHECK(f.registered==1 && f.detached && f.edited==1 && event.nfrm==17);
            CHECK(f.publications==(kind==MM_TRANSACTION_ACTION && output));
            CHECK(f.copy.refs==(f.publications?2:1) && f.body_refs==1 && f.template_refs==2);
            CHECK(!strcmp(f.trace,f.publications?"DAUJWZO":"DAUJWZL"));
        }
        else
        {
            CHECK(f.result.snapshot==&f && !f.publications && !f.registered && !f.edited);
            CHECK(!strcmp(f.trace,fail_traces[fail-1]));
            CHECK(!f.copy.refs && f.body_refs==1 && f.template_refs==1);
            CHECK(event.nfrm==f.add_frame && f.live.owner.runtime.state.frame==10);
        }
        done(&f);
    }
}

static void deletion(void)
{
    Fixture f;
    int fail;
    init(&f); f.override_frame=50; CHECK(delete_actor(&f,0,1));
    CHECK(!strcmp(f.trace,"DSFOSHUJWZL") && f.frames[0]==9 && f.frames[1]==51);
    CHECK(f.original==42 && f.wants_alive && !f.all && f.alive.alive==1 && f.copy.refs==1 && f.edited==1);
    CHECK(f.live.owner.runtime.last==9 && !f.live.owner.runtime.life_dirty);
    done(&f);
    init(&f); f.add_frame=10; CHECK(delete_actor(&f,0,1));
    CHECK(!strcmp(f.trace,"DEOUJWZL") && !f.seeks && !f.sounds && f.subroutes==1);
    done(&f);
    init(&f); f.add_frame=10; f.empty_subroute=1; CHECK(delete_actor(&f,0,1) && !f.alive.alive);
    done(&f);
    init(&f); f.add_frame=10; CHECK(delete_actor(&f,0,0) && !f.alive_writes && f.alive.alive==7);
    done(&f);
    init(&f); f.add_frame=10; CHECK(delete_actor(&f,1,1) && !f.subroutes && f.all);
    done(&f);
    init(&f); f.live.owner.runtime.state.addEvent=-1; f.live.owner.runtime.scene=NULL;
    CHECK(delete_actor(&f,0,0) && !f.sounds && !f.wants_alive && !f.alive_writes);
    done(&f);
    init(&f); f.detach_on_seek=1; CHECK(delete_actor(&f,0,1) && f.seeks==2 && !f.sounds);
    done(&f);
    init(&f); f.fail_duplicate=1; CHECK(!delete_actor(&f,0,1));
    CHECK(!strcmp(f.trace,"DO") && f.trash==1 && !f.alive_writes && !f.faults && !f.copy.refs);
    done(&f);
    init(&f); f.fail_duplicate=1; CHECK(!delete_actor(&f,0,0) && !strcmp(f.trace,"D") && !f.trash);
    done(&f);
    init(&f); f.add_frame=10; f.fail_subroute=1; CHECK(!delete_actor(&f,0,1));
    CHECK(!strcmp(f.trace,"DEXL") && f.edited==1 && !f.restored && !f.copy.refs && f.alive.alive==7);
    done(&f);
    for (fail=1;fail<=2;++fail)
    {
        init(&f); f.fail_seek=fail; CHECK(!delete_actor(&f,0,1));
        CHECK(f.seeks==fail && !f.restored && !f.registered && !f.copy.refs && f.last_fault==MM_TRANSACTION_SEEK_FAILED);
        CHECK(f.alive_writes==(fail==2) && f.edited==(fail==2) && f.frames[0]==9);
        done(&f);
    }
    init(&f); f.fail_lifetime=1; CHECK(delete_actor(&f,0,1));
    CHECK(f.last_fault==MM_TRANSACTION_BAD_LIFETIME && f.registered==1 && !f.restored && f.copy.refs==1);
    CHECK(f.live.owner.runtime.last==2147483647L && f.live.owner.runtime.life_dirty);
    done(&f);
    init(&f); f.fail_undo=1; CHECK(!delete_actor(&f,0,1));
    CHECK(f.restored==1 && f.alive.alive==1 && f.alive_writes==1 && !f.edited && !f.copy.refs && !f.faults);
    done(&f);
}

int main(void)
{
    edits();
    deletion();
    puts("actor edit transaction C89 checks passed");
    return 0;
}
