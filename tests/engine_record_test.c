#include "engine_subroute_edit_fixture.h"
#include "mm/actor_transaction.h"
typedef struct RecordFixture
{
    SubFixture sub;
    MMActorRecording record,view_record,saved_record;
    MMActorState saved_state;
    MMActorPoint measured;
    int restores,point_reserves,fail_points,distances,seeks,fail_scene,invalid_handles,outputs;
    int output_frame,output_route;
    int aliased_output;
    unsigned output_mask;
    MMI32 forced_distance,scene_frame;
} RecordFixture;
static void record_publish(void *p,const MMActorRecording *record)
{
    RecordFixture *f=(RecordFixture *)p; f->view_record=*record;
    f->sub.base.recording=record->recording; f->sub.rejoining=record->rejoining;
}
static void record_restore(void *p,void *undo,MMActorState *state,MMActorRecording *record)
{
    RecordFixture *f=(RecordFixture *)p; CHECK(undo==&f->saved_state);
    ++f->restores; *state=f->saved_state; *record=f->saved_record; f->invalid_handles=1;
}
static MMI32 checked_events(void *p) { RecordFixture *f=(RecordFixture *)p; CHECK(!f->invalid_handles); return events(p); }
static MMI32 checked_nodes(void *p) { RecordFixture *f=(RecordFixture *)p; CHECK(!f->invalid_handles); return nodes(p); }
static MMI32 metric(void *p,const MMActorPoint *delta)
{
    RecordFixture *f=(RecordFixture *)p; f->measured=*delta; ++f->distances;
    if (f->forced_distance>=0) return f->forced_distance;
    return (delta->dxr<0?-delta->dxr:delta->dxr)+(delta->dyr<0?-delta->dyr:delta->dyr)+(delta->dzr<0?-delta->dzr:delta->dzr);
}
static int record_points(void *p,MMU32 count)
{ RecordFixture *f=(RecordFixture *)p; CHECK(count==1 && f->sub.base.reserves==1); ++f->point_reserves; return !f->fail_points; }
static int current_frame(void *p,MMI32 *frame) { *frame=((RecordFixture *)p)->scene_frame; return 1; }
static void output(void *p,int frame,int route,unsigned changed)
{
    RecordFixture *f=(RecordFixture *)p; ++f->outputs; f->output_frame=frame; f->output_route=route;
    f->output_mask=changed;
    if (changed&1) f->aliased_output=frame;
    if (changed&2) f->aliased_output=route;
    if (frame) CHECK(f->view_record.inserted && !f->restores);
}
static int scene_seek(void *p,MMActorState *state,MMActorRecording *record,MMI32 frame)
{
    RecordFixture *f=(RecordFixture *)p; CHECK(frame==f->scene_frame+1);
    CHECK(f->outputs==2 && f->output_frame && f->view_record.inserted);
    ++f->seeks; state->frame=frame;
    if (f->fail_scene) { state->cel=99; record->timestamp=99; return 0; }
    return 1;
}
static void record_diagnostic(void *p,int fault)
{ RecordFixture *f=(RecordFixture *)p; CHECK(fault>=0 && fault<=MM_RECORD_STEP_FAILED); ++f->sub.diagnostic_count; }
static MMActorRecordServices record_init(RecordFixture *f)
{
    MMActorRecordServices s;
    memset(f,0,sizeof(*f)); memset(&s,0,sizeof(s)); s.subroute=sub_init(&f->sub);
    f->sub.base.events=0; add(&f->sub,0,1); append(&f->sub.base,MM_ACTOR_ACTION,0,0,1); f->sub.base.bytes[1]=8;
    f->sub.base.point[1].dwr=4*ONE; f->sub.base.point[2].xyz.dxr=8*ONE;
    f->sub.base.state.location.irpt=0; f->sub.base.state.location.dwrOffset=f->sub.base.state.location.dnfrm=0;
    f->sub.base.state.firstEventInFrame=0; f->sub.base.state.actionEvent=1; f->sub.base.state.celCount=4; f->sub.base.state.step=-ONE;
    f->record.recording=f->record.rejoining=f->record.inserted=1; f->record.timestamp=1000;
    f->scene_frame=2; f->forced_distance=-1; f->saved_state=f->sub.base.state; f->saved_state.frame=44;
    f->saved_record=f->record; f->saved_record.timestamp=123; f->saved_record.gap=77;
    s.subroute.route.edits.scene_frame=current_frame; s.subroute.action.sounds.edits=s.subroute.route.edits;
    s.distance=metric; s.publish_record=record_publish; s.restore=record_restore; s.seek_scene=scene_seek;
    s.subroute.route.reserve_points=record_points; s.diagnostic=record_diagnostic; record_publish(f,&f->record);
    return s;
}
static MMActorMotionServices checked_tables(void)
{ MMActorMotionServices m=tables; m.event_count=checked_events; m.route_count=checked_nodes; return m; }
static int begin(RecordFixture *f,int replace,const MMActorRecordServices *s)
{ MMActorMotionServices m=checked_tables(); return mm_actor_record_begin(&f->sub.base.state,&f->record,f,f,replace,&f->saved_state,&m,s); }
static int move(RecordFixture *f,const MMActorPoint *delta,MMU32 flags,MMU32 now,MMActorRecordResult *out,const MMActorRecordServices *s)
{ MMActorMotionServices m=checked_tables(); return mm_actor_record_move(&f->sub.base.state,&f->record,&f->sub.base.sounds,f,f,7,delta,flags,now,out,&f->saved_state,&m,s); }
static int finish(RecordFixture *f,int replace,const MMActorRecordServices *s)
{ MMActorMotionServices m=checked_tables(); return mm_actor_record_end(&f->sub.base.state,&f->record,&f->sub.base.sounds,f,f,7,replace,&f->saved_state,&m,s); }
static MMActorRecordResult result(RecordFixture *f)
{ MMActorRecordResult out; memset(&out,0,sizeof(out)); out.frame=41; out.route=42; out.context=f; out.publish=output; return out; }
static void validity_test(void)
{
    RecordFixture f; MMActorRecordServices s=record_init(&f); MMActorPoint delta={0,0,0}; MMI32 value;
    CHECK(mm_actor_record_valid(&f.record,&delta,0,&s) && !f.distances);
    f.record.recording=0; f.record.timestamp=0xffffff00U; f.forced_distance=2*ONE-1;
    CHECK(!mm_actor_record_valid(&f.record,&delta,493,&s));
    CHECK(mm_actor_record_valid(&f.record,&delta,494,&s));
    f.forced_distance=2*ONE; CHECK(mm_actor_record_valid(&f.record,&delta,f.record.timestamp,&s));
    f.forced_distance=0; CHECK(mm_actor_record_valid(&f.record,&delta,f.record.timestamp-1,&s));
    f.sub.base.state.transform.rScaleStep=2*ONE; f.sub.base.state.step=0;
    CHECK(mm_actor_record_step(&f.sub.base.state,&value,&s.subroute.timeline) && value==2*ONE);
    f.sub.fail_resolve=1; value=-99;
    CHECK(!mm_actor_record_step(&f.sub.base.state,&value,&s.subroute.timeline) && value==ONE);
    done(&f.sub);
}
static void begin_test(void)
{
    RecordFixture f; MMActorRecordServices s;
    s=record_init(&f); f.record.recording=0; f.sub.base.state.location.dwrOffset=2*ONE; add(&f.sub,2,8);
    CHECK(begin(&f,0,&s));
    CHECK(f.record.recording && f.record.rejoining && !f.record.inserted && f.record.gap==-6 && f.record.timestamp==1000);
    CHECK(f.record.insertion.irpt==1 && !f.record.raw.dxr && f.sub.base.state.position.dxr==2*ONE && f.sub.base.nodes==4);
    CHECK(f.sub.base.point[0].dwr==2*ONE && f.sub.base.point[1].dwr==2*ONE && f.sub.base.event[2].rtel.irpt==3); done(&f.sub);
    s=record_init(&f); f.record.recording=0; append(&f.sub.base,MM_ACTOR_REMOVE,1,0,3); f.sub.base.bytes[2]=0; add(&f.sub,2,8);
    CHECK(begin(&f,0,&s) && !f.record.rejoining && f.sub.base.nodes==2 && f.sub.base.events==3);
    CHECK(f.sub.base.event[2].aet==MM_ACTOR_ADD && f.sub.base.event[2].rtel.irpt==1); done(&f.sub);
    s=record_init(&f); f.sub.base.point[0].dwr=0;
    CHECK(begin(&f,0,&s) && !f.record.rejoining); done(&f.sub);
    s=record_init(&f); f.sub.base.state.location.dwrOffset=ONE; f.sub.base.fail_insert=1;
    CHECK(!begin(&f,0,&s) && f.restores==1 && f.sub.base.state.frame==44 && f.record.timestamp==123); done(&f.sub);
    s=record_init(&f); f.record.recording=0; f.sub.dirty_state=f.sub.fail_resolve=1;
    append(&f.sub.base,MM_ACTOR_STEP,0,0,1); f.sub.base.payload[2][0]=-ONE; add(&f.sub,2,8);
    CHECK(!begin(&f,0,&s) && f.restores==1 && f.sub.diagnostic_count==1 && f.record.gap==77); done(&f.sub);
}
static void move_test(void)
{
    RecordFixture f; MMActorRecordServices s; MMActorRecordResult out;
    MMActorPoint delta={0,0,0}; int failure;
    s=record_init(&f); f.record.recording=0; out=result(&f);
    CHECK(move(&f,&delta,0,1000,&out,&s) && !out.written && out.frame==41 && !f.sub.base.reserves); done(&f.sub);
    for (failure=0;failure<2;++failure)
    {
        s=record_init(&f); out=result(&f);
        if (failure) f.fail_points=1; else f.sub.base.fail_reserve=1;
        CHECK(!move(&f,&delta,0,1000,&out,&s) && f.restores==1 && !out.written && f.point_reserves==failure && !f.outputs); done(&f.sub);
    }
    s=record_init(&f); out=result(&f);
    CHECK(move(&f,&delta,0,1749,&out,&s) && out.written && !out.frame && !out.route && !f.seeks && !f.restores);
    CHECK(f.record.timestamp==1000 && f.sub.base.state.step==-ONE); done(&f.sub);
    for (failure=0;failure<2;++failure)
    {
        s=record_init(&f); out=result(&f); f.fail_scene=failure;
        CHECK(move(&f,&delta,0,1750,&out,&s)==!failure);
        CHECK(out.frame && !out.route && f.seeks==1 && f.outputs==2 && f.restores==failure);
        CHECK(f.output_mask==1 && f.aliased_output==1);
        CHECK(failure ? f.sub.base.state.frame==44 : (f.sub.base.state.step==0 && f.record.timestamp==1000 && !f.record.gap)); done(&f.sub);
    }
    for (failure=0;failure<4;++failure)
    {
        s=record_init(&f); out=result(&f); delta.dxr=3*ONE; delta.dyr=delta.dzr=0;
        if (failure==1) f.sub.fail_resolve=1;
        if (failure==2) f.sub.base.fail_insert=1;
        if (failure==3) f.fail_scene=1;
        CHECK(move(&f,&delta,0,1100,&out,&s)==!failure);
        CHECK(out.written && out.frame==(failure==0 || failure==3) && out.route==out.frame);
        if (failure) CHECK(f.restores==1 && f.sub.base.state.frame==44 && f.record.timestamp==123);
        else CHECK(f.sub.base.nodes==4 && f.sub.base.point[0].dwr==ONE && f.sub.base.point[1].xyz.dxr==ONE &&
                   f.record.raw.dxr==3*ONE && f.record.timestamp==1100 && f.record.gap==1 && f.sub.base.range.last==9);
        CHECK(f.output_mask==3);
        done(&f.sub);
    }
    s=record_init(&f); out=result(&f); delta.dxr=0; delta.dyr=-3*ONE;
    f.sub.base.state.routeTranslation.dyr=ONE/2; f.record.gap=-1;
    CHECK(move(&f,&delta,MM_RECORD_GROUND,1100,&out,&s));
    CHECK(f.sub.base.point[1].xyz.dyr==-ONE/2 && f.sub.base.point[0].dwr==ONE && !f.record.gap && f.sub.base.range.last==8); done(&f.sub);
    s=record_init(&f); out=result(&f); f.forced_distance=0; f.record.raw.dxr=3; f.record.raw.dyr=-1; f.record.raw.dzr=3;
    delta.dxr=2147483647L; delta.dyr=(-2147483647L-1); delta.dzr=-5;
    CHECK(move(&f,&delta,0,1001,&out,&s));
    CHECK(!f.measured.dxr && !f.measured.dyr && f.measured.dzr==-2 && f.record.raw.dxr==3); done(&f.sub);
    s=record_init(&f); out=result(&f); f.forced_distance=0; delta.dxr=-1073741824L; delta.dyr=delta.dzr=0;
    CHECK(move(&f,&delta,0,1001,&out,&s) && f.measured.dxr==-715827882L); done(&f.sub);
    s=record_init(&f); out=result(&f); f.sub.base.state.frozen=1; f.sub.base.fail_apply=1; delta.dxr=0;
    CHECK(move(&f,&delta,0,1001,&out,&s) && f.sub.diagnostic_count==1 && !f.restores && out.written); done(&f.sub);
}
static void end_test(void)
{
    RecordFixture f; MMActorRecordServices s; MMActorRoutePoint point; int failure,i,found;
    s=record_init(&f); f.record.recording=0;
    CHECK(finish(&f,0,&s) && f.sub.base.state.frozen && !f.record.recording && !f.record.rejoining && f.sub.dirty_state && f.sub.base.invalidated==1); done(&f.sub);
    s=record_init(&f); owned(&f.sub,1,3); add(&f.sub,2,8);
    CHECK(finish(&f,1,&s) && f.sub.refs==0 && f.sub.base.nodes==2 && f.sub.base.state.frozen && !f.sub.base.state.step); done(&f.sub);
    s=record_init(&f); f.sub.base.state.position.dxr=8*ONE; f.sub.base.point[0].dwr=0;
    owned(&f.sub,1,3); owned(&f.sub,1,4); append(&f.sub.base,MM_ACTOR_TWEAK,1,0,4); append(&f.sub.base,MM_ACTOR_FREEZE,1,0,5);
    CHECK(finish(&f,0,&s) && f.sub.base.nodes==2 && f.sub.base.point[0].dwr==1 && f.sub.refs==1 && f.sub.base.events==3);
    CHECK(f.sub.base.event[2].aet==MM_ACTOR_COSTUME && f.sub.base.event[2].rtel.irpt==1); done(&f.sub);
    s=record_init(&f); f.sub.base.state.position.dxr=8*ONE; owned(&f.sub,1,3); add(&f.sub,2,8); owned(&f.sub,2,8);
    CHECK(finish(&f,0,&s) && f.sub.base.nodes==3 && f.sub.base.point[0].dwr==4*ONE && f.sub.base.event[3].rtel.irpt==2 && f.sub.refs==2); done(&f.sub);
    s=record_init(&f); f.record.rejoining=0; owned(&f.sub,1,3); add(&f.sub,2,8); owned(&f.sub,2,8);
    CHECK(finish(&f,0,&s) && f.sub.refs==2);
    found=0;
    for (i=0;i<f.sub.base.events;++i) if (f.sub.base.event[i].aet==MM_ACTOR_COSTUME && f.sub.base.event[i].rtel.irpt==0)
    { CHECK(f.sub.base.event[i].rtel.dnfrm==1); ++found; }
    CHECK(found==1);
    point=f.sub.base.point[2]; CHECK(point.xyz.dxr==8*ONE && f.sub.base.state.frozen && !f.sub.base.state.step); done(&f.sub);
    for (failure=0;failure<2;++failure)
    {
        s=record_init(&f);
        if (failure) f.sub.base.fail_reserve=1; else f.sub.base.fail_apply=1;
        CHECK(!finish(&f,1,&s) && f.restores==1 && !f.record.recording && !f.record.rejoining && f.record.timestamp==123 && f.record.gap==77); done(&f.sub);
    }
}
typedef struct GestureFixture
{
    MMActorRecordGesture gesture;
    MMActorOwner owner, snapshot;
    MMActorTransactionServices transaction;
    MMActorUndoState undo;
    MMMovieUndoContext undo_context;
    MMActorRecordServices record;
    MMActorState saved_state;
    MMActorRecording saved_record;
    MMU32 now;
    MMI32 scene_frame;
    int fail_action, frozen, notifications, restores, snapshot_refs, undo_refs, hides;
} GestureFixture;

static void *gesture_resize(void *p, void *data, MMU32 old, MMU32 bytes, int priority)
{ (void)p; (void)old; (void)priority; return realloc(data, bytes); }
static void gesture_free(void *p, void *data, MMU32 bytes)
{ (void)p; (void)bytes; free(data); }
static MMU32 gesture_clock(void *p)
{
    GestureFixture *f = p;
    CHECK(f->gesture.restore == &f->snapshot && !f->snapshot.runtime.scene && f->undo_refs == 1);
    return f->now;
}
static MMActorOwner *gesture_owner(void *p, void *actor)
{
    GestureFixture *f = p;
    CHECK(actor == &f->owner || actor == &f->snapshot);
    return actor;
}
static void *gesture_copy(void *p)
{
    GestureFixture *f = p;
    CHECK(f->hides && !f->snapshot_refs);
    mm_actor_owner_init(&f->snapshot);
    f->snapshot_refs = 1;
    return &f->snapshot;
}
static void gesture_retain(void *p, void *actor)
{
    GestureFixture *f = p;
    if (actor)
    {
        CHECK(actor == &f->snapshot && f->snapshot_refs > 0);
        ++f->snapshot_refs;
    }
}
static void gesture_release(void *p, void *actor)
{
    GestureFixture *f = p;
    CHECK(actor == &f->snapshot && f->snapshot_refs > 0);
    if (!--f->snapshot_refs)
        mm_actor_owner_dispose(actor, &f->transaction.actors);
}
static void gesture_hide_cursor(void *p)
{
    GestureFixture *f = p;
    CHECK(!f->gesture.restore);
    f->saved_state = f->owner.runtime.state;
    f->saved_record = f->owner.runtime.recording;
    ++f->hides;
}
static MMActorUndoState *gesture_create_undo(void *p, void **handle)
{
    GestureFixture *f = p;
    CHECK(!f->undo_refs && mm_actor_undo_init(&f->undo, &f->undo_context, 1));
    f->undo_refs = 1;
    *handle = f;
    return &f->undo;
}
static int gesture_add_undo(void *p, void *handle)
{
    GestureFixture *f = p;
    CHECK(handle == f && f->undo.actor == &f->snapshot && f->snapshot_refs == 2);
    CHECK(!f->gesture.restore && f->owner.runtime.recording.timestamp == f->saved_record.timestamp);
    ++f->undo_refs;
    return 1;
}
static void gesture_release_undo(void *p, void *handle)
{
    GestureFixture *f = p;
    CHECK(handle == f && f->undo_refs > 0);
    if (!--f->undo_refs) gesture_release(f, f->undo.actor);
}
static void gesture_notify(void *p, int replace)
{
    GestureFixture *f = p;
    CHECK(replace == f->gesture.replace && f->owner.runtime.recording.recording);
    if (replace) CHECK(mm_actor_path_count(f->owner.track.route) == 1);
    ++f->notifications;
}
static MMI32 gesture_distance(void *p, const MMActorPoint *delta)
{ (void)p; return delta->dxr; }
static int gesture_action_flags(void *p, MMI32 action, MMU32 *out)
{ (void)p; CHECK(action == 4); *out = 8; return 1; }
static int gesture_resolve(void *p, MMI32 action, MMI32 cel, MMI32 *out)
{ (void)p; (void)action; (void)cel; *out = ONE; return 1; }
static int gesture_apply(void *p, MMActorState *state, MMI32 index)
{
    GestureFixture *f = p;
    MMActorTrack *track = &f->owner.track;
    const MMActorMotionServices *motion = &f->record.subroute.timeline.motion;
    MMActorEvent row;
    MMI32 value[2];
    CHECK(mm_actor_events_get_fixed(track->events, index, &row));
    if (row.aet == MM_ACTOR_FREEZE)
    {
        CHECK(mm_actor_events_get_payload(track->events, index, value));
        f->frozen = value[0] != 0;
        if (f->fail_action) return 0;
    }
    if (row.aet != MM_ACTOR_ACTION)
        return mm_actor_state_apply(state, track->route, track->events, index, &f->owner.runtime.origin,
                                    motion) == MM_ACTOR_APPLIED;
    CHECK(mm_actor_events_get_payload(track->events, index, value));
    state->action = value[0]; state->cel = value[1]; state->actionEvent = index;
    return 1;
}
static void gesture_range(void *p, MMActorEditRange *out)
{
    GestureFixture *f = p;
    out->first = f->owner.runtime.first;
    out->last = f->owner.runtime.last;
}
static void gesture_publish_range(void *p, const MMActorEditRange *value)
{
    GestureFixture *f = p;
    f->owner.runtime.first = value->first;
    f->owner.runtime.last = value->last;
}
static int gesture_scene_frame(void *p, MMI32 *frame)
{ *frame = ((GestureFixture *)p)->scene_frame; return 1; }
static int gesture_seek(void *p, MMActorState *state, MMActorRecording *record, MMI32 frame)
{
    GestureFixture *f = p;
    CHECK(frame == f->scene_frame + 1 && record->inserted);
    state->frame = f->scene_frame = frame;
    return 1;
}
static void gesture_restore(void *p, void *snapshot, MMActorState *state, MMActorRecording *record)
{
    GestureFixture *f = p;
    CHECK(snapshot == &f->snapshot && f->snapshot_refs == 2 && !f->snapshot.runtime.scene);
    *state = f->saved_state;
    *record = f->saved_record;
    ++f->restores;
}
static void gesture_noop(void *p) { (void)p; }
static void gesture_invalidate(void *p, void *scene) { CHECK(p == scene); }
static void gesture_copy_life(void *p) { ((MMActorOwner *)p)->runtime.life_dirty = 1; }
static void gesture_copy_recording(void *p, int recording) { ((MMActorOwner *)p)->runtime.recording.recording = recording; }
static void gesture_routes(void *p, void *actor, MMActorRouteEditServices *routes)
{
    GestureFixture *f = p;
    CHECK(actor == &f->snapshot);
    memset(routes, 0, sizeof(*routes));
    routes->edits.context = actor;
    routes->hide_body = routes->default_costume = gesture_noop;
    routes->life_dirty = gesture_copy_life;
    routes->recording = gesture_copy_recording;
}
static void gesture_life(void *p) { ((GestureFixture *)p)->owner.runtime.life_dirty = 1; }
static int reject_point(void *p, MMI32 index, const MMActorRoutePoint *row)
{ (void)p; (void)index; (void)row; return 0; }
static int reject_events(void *p, MMU32 entries, MMU32 bytes)
{ (void)p; (void)entries; (void)bytes; return 0; }

static MMActorRecordGestureServices gesture_init(GestureFixture *f)
{
    static const MMStorageAllocator storage = {NULL, gesture_resize, gesture_free};
    MMActorRecordGestureServices s = {NULL, gesture_clock, gesture_hide_cursor, gesture_notify};
    MMActorEntrance entrance = {0};
    MMActorEvent row = {0};
    MMActorRoutePoint first = {{0,0,0}, 4 * ONE}, last = {{4 * ONE,0,0}, 0};
    MMI32 action[2] = {4, 6}, size = ONE;
    MMActorEditServices *edit;
    MMActorActionEditServices *act;
    MMActorRuntime *runtime;
    memset(f, 0, sizeof(*f));
    s.context = f;
    mm_actor_owner_init(&f->owner);
    mm_movie_undo_context_init(&f->undo_context);
    f->transaction.actors.context = f;
    f->transaction.actors.owner = gesture_owner;
    f->transaction.actors.create = gesture_copy;
    f->transaction.actors.retain = gesture_retain;
    f->transaction.actors.release = gesture_release;
    f->transaction.actors.release_template = gesture_release;
    f->transaction.actors.retain_template = gesture_retain;
    f->transaction.actors.route_services = gesture_routes;
    f->transaction.actors.invalidate_views = gesture_invalidate;
    f->transaction.undo.context = f;
    f->transaction.undo.create = gesture_create_undo;
    f->transaction.undo.add = gesture_add_undo;
    f->transaction.undo.release = gesture_release_undo;
    CHECK(mm_actor_track_create(&f->owner.track, &storage, 0, NULL));
    CHECK(mm_actor_path_insert(f->owner.track.route, 0, &first));
    CHECK(mm_actor_path_insert(f->owner.track.route, 1, &last));
    row.aet = MM_ACTOR_ADD; row.nfrm = 1;
    CHECK(mm_actor_events_insert(f->owner.track.events, 0, sizeof(entrance), &entrance, &row));
    row.aet = MM_ACTOR_ACTION;
    CHECK(mm_actor_events_insert(f->owner.track.events, 1, sizeof(action), action, &row));
    row.aet = MM_ACTOR_SIZE; row.nfrm = 2; row.rtel.irpt = 1;
    CHECK(mm_actor_events_insert(f->owner.track.events, 2, sizeof(size), &size, &row));
    runtime = &f->owner.runtime;
    runtime->scene = f;
    runtime->first = f->scene_frame = runtime->state.frame = 1;
    runtime->last = 2;
    runtime->state.onStage = 1; runtime->state.addEvent = 0; runtime->state.actionEvent = 1;
    runtime->state.nextEvent = 2; runtime->state.action = 4; runtime->state.cel = 6;
    runtime->state.step = -ONE;
    f->record.subroute.live = &f->owner;
    f->record.live_actor = &f->owner;
    f->record.subroute.timeline = mm_actor_track_timeline_services();
    f->record.subroute.timeline.resolve = gesture_resolve;
    f->record.subroute.lifetime = mm_actor_owner_lifetime(&f->owner);
    mm_actor_route_edit_bind_tables(&f->record.subroute.route);
    f->record.subroute.route.life_dirty = gesture_life;
    edit = &f->record.subroute.route.edits;
    edit->context = f;
    edit->tables = mm_actor_edit_tables(f->owner.track.events, NULL);
    edit->scene_frame = gesture_scene_frame; edit->apply_event = gesture_apply;
    edit->dirty = edit->invalidate_range = edit->update_sound_frame = gesture_noop;
    edit->get_range = gesture_range; edit->publish_range = gesture_publish_range;
    act = &f->record.subroute.action;
    act->context = f; act->action_flags = gesture_action_flags; act->mark_life_dirty = gesture_life;
    act->sounds.edits = *edit;
    f->record.distance = gesture_distance;
    f->record.restore = gesture_restore;
    f->record.seek_scene = gesture_seek;
    f->gesture.replace = 1;
    f->now = 0xffffff00U;
    return s;
}
static void gesture_done(GestureFixture *f)
{
    if (f->undo_refs) gesture_release_undo(f, f);
    CHECK(!f->undo_refs && !f->snapshot_refs);
    mm_actor_track_dispose(&f->owner.track, NULL);
    mm_actor_sounds_dispose(&f->owner.sounds);
}
static int gesture_start(GestureFixture *f, unsigned modifiers, const MMActorRecordGestureServices *s)
{
    MMActorEditTables tables = mm_actor_edit_tables(f->owner.track.events, NULL);
    f->record.subroute.route.edits.tables = tables;
    f->record.subroute.action.sounds.edits.tables = tables;
    return mm_actor_record_gesture_start(&f->gesture, modifiers, &f->record, &f->transaction, s);
}
static int gesture_sample(GestureFixture *f, unsigned advance, unsigned modifiers, MMI32 distance,
                          const MMActorRecordGestureServices *s)
{
    MMActorPoint delta = {0, 0, 0};
    delta.dxr = distance;
    f->now += advance;
    return mm_actor_record_gesture_sample(&f->gesture, &delta, modifiers, 1, f->now, &f->record, s);
}
static void gesture_test(void)
{
    GestureFixture f;
    MMActorRecordGestureServices s = gesture_init(&f);
    MMActorRuntime *runtime = &f.owner.runtime;
    MMActorPoint axes = {ONE, -ONE, -5 * ONE};
    const unsigned shift = MM_RECORD_GESTURE_SHIFT, cmd = MM_RECORD_GESTURE_COMMAND;
    CHECK(gesture_start(&f, cmd, &s));
    CHECK(f.gesture.action == -1 && runtime->recording.timestamp == f.now && f.snapshot_refs == 2);
    CHECK(f.gesture.restore == &f.snapshot && !f.snapshot.runtime.scene && f.snapshot.runtime.state.frame == 2147483647L);
    CHECK(gesture_sample(&f, 497, cmd, 0, &s) == MM_RECORD_GESTURE_IDLE);
    CHECK(gesture_sample(&f, 1, cmd | shift, 0, &s) == MM_RECORD_GESTURE_CEL);
    CHECK(runtime->state.cel == 7);
    CHECK(gesture_sample(&f, 0, 0, 0, &s) == MM_RECORD_GESTURE_IDLE && !f.notifications);
    CHECK(gesture_sample(&f, 165, 0, 4 * ONE, &s) == MM_RECORD_GESTURE_IDLE && f.notifications == 1);
    CHECK(gesture_sample(&f, 1, shift, 0, &s) == MM_RECORD_GESTURE_IDLE && runtime->state.frozen);
    CHECK(gesture_sample(&f, 166, shift, 0, &s) == MM_RECORD_GESTURE_FRAME && runtime->state.frozen);
    CHECK(gesture_sample(&f, 166, cmd | shift, 4 * ONE, &s) == MM_RECORD_GESTURE_POINT && !runtime->state.frozen);
    mm_actor_record_gesture_filter_axes(&f.gesture, runtime->recording.recording, &axes);
    CHECK(axes.dxr == 0 && axes.dyr == 0 && axes.dzr == -5 * ONE);
    f.record.subroute.route.edits.tables.reserve_events = reject_events;
    CHECK(gesture_sample(&f, 166, 0, 4 * ONE, &s) == MM_RECORD_GESTURE_IDLE && f.restores == 1);
    f.record.subroute.route.edits.tables = f.record.subroute.action.sounds.edits.tables;
    mm_actor_record_gesture_end(&f.gesture, &f.record, &f.transaction.actors);
    CHECK(!f.gesture.restore && !runtime->recording.recording && f.snapshot_refs == 1);
    gesture_done(&f);

    s = gesture_init(&f);
    runtime->state.location.dwrOffset = ONE / 2;
    CHECK(gesture_start(&f, shift | cmd, &s));
    CHECK(!f.frozen && !f.gesture.replace && !f.gesture.cycling);
    f.record.subroute.route.insert_point = reject_point;
    CHECK(gesture_sample(&f, 166, 0, 4 * ONE, &s) == MM_RECORD_GESTURE_IDLE);
    CHECK(f.restores == 1 && !f.notifications);
    mm_actor_route_edit_bind_tables(&f.record.subroute.route);
    CHECK(gesture_sample(&f, 0, 0, 4 * ONE, &s) == MM_RECORD_GESTURE_POINT && f.notifications == 1);
    mm_actor_record_gesture_end(&f.gesture, &f.record, &f.transaction.actors);
    CHECK(f.gesture.replace && !f.gesture.restore_default && !runtime->recording.recording && f.snapshot_refs == 1);
    gesture_done(&f);

    s = gesture_init(&f);
    f.fail_action = 1;
    CHECK(!gesture_start(&f, shift, &s));
    CHECK(f.frozen && gesture_sample(&f, 200, 0, 4 * ONE, &s) == MM_RECORD_GESTURE_IDLE);
    mm_actor_record_gesture_end(&f.gesture, &f.record, &f.transaction.actors);
    CHECK(!f.snapshot_refs && !f.notifications && !runtime->state.frozen && !runtime->recording.timestamp);
    f.fail_action = 0;
    CHECK(gesture_start(&f, 0, &s));
    mm_actor_record_gesture_end(&f.gesture, NULL, &f.transaction.actors);
    CHECK(!f.gesture.restore && f.snapshot_refs == 1);
    gesture_done(&f);
}
int main(void)
{
    validity_test(); begin_test(); move_test(); end_test(); gesture_test();
    puts("actor recording checks passed"); return 0;
}
