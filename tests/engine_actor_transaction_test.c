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
    void *snapshot;
    MMActorDeleteResult alive;
    Actor live, copy;
    MMActorUndoState undo;
    MMMovieUndoContext undo_context;
    int body_refs, template_refs, track_blocks, undo_refs;
    int edited, registered, restored, detached, trash, alive_writes;
    int fail_duplicate, fail_apply, fail_undo_create, fail_undo, fail_seek, fail_lifetime;
    int has_output, seeks, frames[2], sounds, faults, last_fault, hidden;
    int second_subroute, keep_original, detach_on_seek, expect_sound, sound_refs;
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
    CHECK(f->snapshot==f);
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
    CHECK(f->live.owner.runtime.state.frame==10 && f->live.owner.runtime.state.nextEvent==3);
    f->edited=0;
    ++f->restored;
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
    CHECK(f->snapshot==f);
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

static void publish_edit(void *p,const MMActorState *state)
{
    ((Fixture *)p)->live.owner.runtime.state=*state;
}

static void dirty_edit(void *p)
{
    Fixture *f=(Fixture *)p;
    mark(f,'F');
    ++f->edited;
}

static void get_range(void *p,MMActorEditRange *range)
{
    MMActorRuntime *runtime=&((Fixture *)p)->live.owner.runtime;
    range->first=runtime->first;
    range->last=runtime->last;
}

static void publish_range(void *p,const MMActorEditRange *range)
{
    MMActorRuntime *runtime=&((Fixture *)p)->live.owner.runtime;
    runtime->first=range->first;
    runtime->last=range->last;
}

static void dirty_life(void *p)
{
    ((Fixture *)p)->live.owner.runtime.life_dirty=1;
}

static void edit_recording(void *p,int enabled)
{
    ((Fixture *)p)->live.owner.runtime.recording.recording=enabled;
}

static void edit_costume(void *p)
{
    Fixture *f=(Fixture *)p;
    CHECK(!f->live.owner.runtime.state.onStage);
    mark(f,'E');
}

static int seek(void *p,MMActorState *state,MMI32 frame)
{
    Fixture *f=(Fixture *)p;
    mark(f,'S');
    CHECK(f->seeks<2);
    if (f->seeks==1 && f->has_output)
        CHECK(f->alive_writes==1);
    if (!f->seeks && state->frame!=2147483647L)
    {
        state->nextEvent=f->keep_original?3:2;
        state->location.irpt=0;
    }
    f->frames[f->seeks++]=frame;
    state->frame=frame;
    if (f->seeks==2 && f->detach_on_seek)
        f->live.owner.runtime.scene=NULL;
    return f->seeks!=f->fail_seek;
}

static void hide_for_seek(void *p)
{
    Fixture *f=(Fixture *)p;
    CHECK(f->live.owner.runtime.state.onStage);
    mark(f,'I');
    ++f->hidden;
}

static int costume_for_seek(void *p)
{
    Fixture *f=(Fixture *)p;
    CHECK(!f->live.owner.runtime.state.onStage && !f->live.owner.runtime.recording.recording);
    mark(f,'K');
    return 1;
}

static int seek_fault(void *p)
{
    Fixture *f=(Fixture *)p;
    return f->hidden && f->fail_seek ? -1 : 0;
}

static void seek_failed(void *p,MMI32 frame)
{
    Fixture *f=(Fixture *)p;
    CHECK(frame==8 && !f->live.owner.runtime.state.onStage);
    mark(f,'X');
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
    MMActorSubrouteEditServices *edit;
    memset(f,0,sizeof(*f));
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
    f->snapshot=f;
    f->alive.alive=7;
    f->alive.trashed=3;
    f->alive.context=f;
    f->alive.publish=publish_alive;
    f->deletion.context=f;
    edit=&f->deletion.subroute;
    edit->timeline=mm_actor_track_timeline_services();
    edit->route.edits.context=f;
    edit->route.edits.publish=publish_edit;
    edit->route.edits.dirty=dirty_edit;
    edit->route.edits.get_range=get_range;
    edit->route.edits.publish_range=publish_range;
    edit->route.life_dirty=dirty_life;
    edit->route.recording=edit_recording;
    edit->route.default_costume=edit_costume;
    mm_actor_route_edit_bind_tables(&edit->route);
    edit->seek=seek;
    f->deletion.sound_frame=sound;
    f->deletion.diagnostic=diagnostic;
    mm_movie_undo_context_init(&f->undo_context);
    mm_actor_owner_init(&f->live.owner);
    f->live.owner.runtime.actor=23;
    f->live.owner.runtime.state.frame=10;
    f->live.owner.runtime.state.addEvent=0;
    f->live.owner.runtime.state.nextEvent=3;
    f->live.owner.runtime.scene=f;
    f->live.owner.runtime.body=&f->body_refs;
    f->live.owner.runtime.template_object=&f->template_refs;
    f->body_refs=f->template_refs=1;
    f->add_frame=9;
}

static void append_subroute(MMActorTrack *track,MMI32 frame,MMI32 node)
{
    const unsigned short angles[]={0,0,0};
    MMI32 index=mm_actor_events_count(track->events);
    MMActorStationaryTrack rows;
    mm_actor_stationary_track(&rows,frame,angles);
    rows.events[0].rtel.irpt=rows.events[1].rtel.irpt=rows.events[2].rtel.irpt=node;
    CHECK(mm_actor_path_insert(track->route,node,&rows.route));
    CHECK(mm_actor_events_insert(track->events,index,sizeof(rows.entrance),&rows.entrance,&rows.events[0]));
    CHECK(mm_actor_events_insert(track->events,index+1,sizeof(rows.action),&rows.action,&rows.events[1]));
    CHECK(mm_actor_events_insert(track->events,index+2,sizeof(rows.step),&rows.step,&rows.events[2]));
}

static void create_track(Fixture *f)
{
    MMStorageAllocator allocator={NULL,resize_track,free_track};
    MMActorTrack *track=&f->live.owner.track;
    allocator.context=f;
    CHECK(mm_actor_track_create(track,&allocator,0,NULL));
    append_subroute(track,f->add_frame,0);
    if (f->second_subroute)
        append_subroute(track,f->keep_original?10:20,1);
    f->deletion.subroute.route.edits.tables=mm_actor_edit_tables(track->events,NULL);
    if (f->keep_original)
    {
        MMActorState *state=&f->live.owner.runtime.state;
        state->frame=11;
        state->location.irpt=1;
        state->addEvent=3;
        state->nextEvent=4;
    }
    f->live.owner.runtime.first=f->fail_lifetime?2147483647L:f->add_frame;
    f->live.owner.runtime.last=20;
    f->live.owner.runtime.life_dirty=1;
}

static void done(Fixture *f)
{
    if (f->snapshot==&f->copy)
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

static int action_flags(void *p,MMI32 action,MMU32 *flags)
{
    Fixture *f=(Fixture *)p;
    CHECK(action==0 && f->copy.refs==1 && f->copy.owner.track.events!=f->live.owner.track.events);
    *flags=8;
    return 1;
}

static int apply_action(void *p,MMActorState *state,MMI32 index)
{
    Fixture *f=(Fixture *)p;
    MMActorEventTable *table=f->live.owner.track.events;
    MMActorEvent event;
    MMI32 value[2];
    CHECK(mm_actor_events_get_fixed(table,index,&event));
    CHECK(event.aet==MM_ACTOR_ACTION || event.aet==MM_ACTOR_FREEZE);
    CHECK(mm_actor_events_get_payload(table,index,value));
    if (event.aet==MM_ACTOR_ACTION)
    {
        state->action=value[0];
        state->cel=value[1];
        state->actionEvent=index;
        return !f->fail_apply;
    }
    state->frozen=value[0]!=0;
    return 1;
}

static int edit_action(Fixture *f,int output)
{
    MMActorActionEditServices services={0};
    const MMActorMotionServices motion=mm_actor_track_motion_services();
    services.context=f;
    services.action_flags=action_flags;
    services.mark_life_dirty=dirty_life;
    services.sounds.edits=f->deletion.subroute.route.edits;
    services.sounds.edits.apply_event=apply_action;
    services.sounds.edits.invalidate_range=dirty_life;
    f->live.owner.runtime.state.action=0;
    f->live.owner.runtime.state.cel=0;
    f->live.owner.runtime.state.actionEvent=1;
    return mm_actor_transaction_action(&f->live,0,3,1,0,output?&f->snapshot:NULL,
                                       &motion,&services,&f->transaction);
}

static void *fetch_sound(void *p,const MMTag *tag)
{
    Fixture *f=(Fixture *)p;
    mark(f,'A');
    CHECK(f->copy.refs==1 && f->copy.owner.track.events!=f->live.owner.track.events);
    CHECK(tag->sid==1 && tag->number==44 && !f->sound_refs);
    ++f->sound_refs;
    return &f->sound_refs;
}

static void sound_info(void *p,void *resource,MMActorSoundInfo *out)
{
    Fixture *f=(Fixture *)p;
    CHECK(resource==&f->sound_refs && f->sound_refs==1);
    out->style=2; out->volume=83; out->no_sound=0;
}

static void release_sound(void *p,void *resource)
{
    Fixture *f=(Fixture *)p;
    CHECK(resource==&f->sound_refs && f->sound_refs==1);
    --f->sound_refs;
    mark(f,'V');
}

static int apply_sound(void *p,MMActorState *state,MMI32 index)
{
    Fixture *f=(Fixture *)p;
    MMActorEventTable *table=f->live.owner.track.events;
    MMActorEvent event;
    MMActorSound sound;
    CHECK(mm_actor_events_get_fixed(table,index,&event) && event.aet==MM_ACTOR_SOUND);
    CHECK(mm_actor_events_size(table,index)==sizeof(sound));
    CHECK(mm_actor_events_get_payload(table,index,&sound));
    CHECK(sound.loop==2 && sound.queue==1 && sound.volume==83 && sound.style==2);
    CHECK(sound.cel==-1 && sound.tag.number==44 && sound.child==21);
    mark(f,'P');
    if (f->fail_apply) state->cel=7;
    return !f->fail_apply;
}

static int sounds_suppressed(void *p) { (void)p; return 1; }
static void sound_indicator(void *p)
{
    Fixture *f=(Fixture *)p;
    CHECK(!f->sound_refs);
    mark(f,'G');
}

static int edit_sound(Fixture *f)
{
    const MMTag tag={1,NULL,0,44};
    MMActorSoundEditServices services={0};
    services.context=f;
    services.runtime.context=f;
    services.runtime.fetch=fetch_sound;
    services.runtime.release=release_sound;
    services.sound_info=sound_info;
    services.sounds_suppressed=sounds_suppressed;
    services.set_sound_frame=sound_indicator;
    services.edits=f->deletion.subroute.route.edits;
    services.edits.apply_event=apply_sound;
    return mm_actor_transaction_sound(&f->live,&tag,2,1,0,-1,0,21,&services,&f->transaction);
}

static void edits(void)
{
    static const char *sound_fail_traces[]={"D","DL","DAPVRL","DAPFVGURL","DAPFVGUJCWLRL"};
    Fixture f;
    MMActorEvent event;
    int mode,fail;
    for (mode=0;mode<3;++mode) for (fail=0;fail<6;++fail)
    {
        init(&f);
        create_track(&f);
        f.fail_duplicate=fail<=2?fail:0;
        f.fail_apply=fail==3;
        f.fail_undo_create=fail==4;
        f.fail_undo=fail==5;
        f.expect_sound=mode==0;
        if (!mode) CHECK(edit_sound(&f)==!fail && !f.sound_refs);
        else CHECK(edit_action(&f,mode==2)==!fail);
        CHECK(mm_actor_events_get_fixed(f.live.owner.track.events,0,&event));
        if (!fail)
        {
            CHECK(f.registered==1 && f.detached && f.edited);
            CHECK(f.snapshot==(mode==2?(void *)&f.copy:(void *)&f));
            CHECK(f.copy.refs==(mode==2?2:1) && f.body_refs==1 && f.template_refs==2);
            if (!mode)
            {
                CHECK(f.edited==1 && !strcmp(f.trace,"DAPFVGUJWZL"));
                CHECK(mm_actor_events_get_fixed(f.live.owner.track.events,3,&event) && event.aet==MM_ACTOR_SOUND);
            }
            else
            {
                CHECK(f.live.owner.runtime.state.cel==3 && f.live.owner.runtime.state.frozen);
                CHECK(mm_actor_events_get_fixed(f.live.owner.track.events,f.live.owner.runtime.state.actionEvent,&event));
                CHECK(event.aet==MM_ACTOR_ACTION);
            }
        }
        else
        {
            CHECK(f.snapshot==&f && !f.registered && !f.edited);
            if (!mode || fail<=2) CHECK(!strcmp(f.trace,sound_fail_traces[fail-1]));
            CHECK(!f.copy.refs && f.body_refs==1 && f.template_refs==1);
            CHECK(event.nfrm==f.add_frame && f.live.owner.runtime.state.frame==10);
            CHECK(f.live.owner.runtime.state.cel==0 && mm_actor_events_count(f.live.owner.track.events)==3);
            CHECK(mm_actor_events_get_fixed(f.live.owner.track.events,1,&event) && event.aet==MM_ACTOR_ACTION);
        }
        done(&f);
    }
}

static void deletion(void)
{
    Fixture f;
    int fail;
    init(&f); CHECK(delete_actor(&f,0,1));
    CHECK(!strcmp(f.trace,"DSFOSHUJWZL") && f.frames[0]==9 && f.frames[1]==10);
    CHECK(f.alive.alive==1 && f.copy.refs==1 && f.edited==1);
    CHECK(mm_actor_events_count(f.live.owner.track.events)==2);
    CHECK(f.live.owner.runtime.last==9 && !f.live.owner.runtime.life_dirty);
    done(&f);
    init(&f); f.second_subroute=1; f.keep_original=1; CHECK(delete_actor(&f,0,1));
    CHECK(f.frames[0]==10 && f.frames[1]==11 && f.alive.alive==1);
    CHECK(mm_actor_events_count(f.live.owner.track.events)==3 && mm_actor_path_count(f.live.owner.track.route)==1);
    done(&f);
    init(&f); f.add_frame=10; CHECK(delete_actor(&f,0,1));
    CHECK(!strcmp(f.trace,"DFEOUJWZL") && !f.seeks && !f.sounds && !f.alive.alive);
    done(&f);
    init(&f); f.add_frame=10; f.second_subroute=1; CHECK(delete_actor(&f,0,1));
    CHECK(!strcmp(f.trace,"DFESOUJWZL") && f.seeks==1 && f.frames[0]==10 && f.alive.alive);
    CHECK(mm_actor_events_count(f.live.owner.track.events)==3 && f.live.owner.runtime.first==20);
    done(&f);
    init(&f); f.add_frame=10; CHECK(delete_actor(&f,0,0) && !f.alive_writes && f.alive.alive==7);
    done(&f);
    init(&f); f.add_frame=10; CHECK(delete_actor(&f,1,1) && f.seeks==2 && !f.alive.alive);
    done(&f);
    init(&f); f.live.owner.runtime.state.addEvent=-1; f.live.owner.runtime.scene=NULL;
    CHECK(delete_actor(&f,0,0) && !f.sounds && !f.alive_writes);
    done(&f);
    init(&f); f.detach_on_seek=1; CHECK(delete_actor(&f,0,1) && f.seeks==2 && !f.sounds);
    done(&f);
    init(&f); f.fail_duplicate=1; CHECK(!delete_actor(&f,0,1));
    CHECK(!strcmp(f.trace,"DO") && f.trash==1 && !f.alive_writes && !f.faults && !f.copy.refs);
    done(&f);
    init(&f); f.fail_duplicate=1; CHECK(!delete_actor(&f,0,0) && !strcmp(f.trace,"D") && !f.trash);
    done(&f);
    init(&f); f.add_frame=10; f.second_subroute=1; f.fail_seek=1; CHECK(!delete_actor(&f,0,1));
    CHECK(!strcmp(f.trace,"DFESXL") && f.edited==1 && !f.restored && !f.copy.refs && f.alive.alive==7);
    CHECK(mm_actor_events_count(f.live.owner.track.events)==3 && f.live.owner.runtime.first==20);
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

static void owner_seek(void)
{
    Fixture f;
    MMActorState working;
    MMActorSubrouteEditServices *edit;
    int fail;
    for (fail=0;fail<=1;++fail)
    {
        init(&f);
        create_track(&f);
        edit=&f.deletion.subroute;
        edit->live=&f.live.owner;
        edit->playback.context=&f;
        edit->playback.hide=hide_for_seek;
        edit->playback.reset_costume=costume_for_seek;
        edit->playback.fault=seek_fault;
        edit->playback.seek_failed=seek_failed;
        working=f.live.owner.runtime.state;
        working.onStage=1;
        f.live.owner.runtime.recording.recording=1;
        f.fail_seek=fail;
        CHECK(mm_actor_subroute_seek(&working,8,edit)==!fail);
        CHECK(!strcmp(f.trace,fail?"IX":"IK") && !working.onStage && working.frame==8);
        CHECK(!memcmp(&working,&f.live.owner.runtime.state,sizeof(working)));
        CHECK(f.live.owner.runtime.recording.recording==fail);
        done(&f);
    }
}

int main(void)
{
    edits();
    deletion();
    owner_seek();
    puts("actor edit transaction C89 checks passed");
    return 0;
}
