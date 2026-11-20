#ifndef MM_TEST_ACTOR_STAGE_OWNER_FIXTURE_H
#define MM_TEST_ACTOR_STAGE_OWNER_FIXTURE_H
#include "mm/actor_effect.h"
#include "mm/actor_owner.h"
#include "mm/actor_stage_edit.h"
#include <string.h>

typedef struct StageOwnerFixture
{
    MMActorOwner owner;
    int fail_entrance, entrances, exits;
} StageOwnerFixture;

static int stage_fixture_init(StageOwnerFixture *f, MMI32 frame)
{
    memset(f, 0, sizeof(*f));
    mm_actor_owner_init(&f->owner);
    f->owner.runtime.state.frame = frame;
    return mm_actor_track_create(&f->owner.track, NULL, 0, NULL);
}
static void stage_fixture_dispose(StageOwnerFixture *f)
{
    mm_actor_track_dispose(&f->owner.track, NULL);
    mm_actor_sounds_dispose(&f->owner.sounds);
}
static void stage_fixture_placement(void *p, MMActorPoint *point, MMActorPoint *origin, unsigned short *yaw)
{
    (void)p;
    memset(point, 0, sizeof(*point));
    memset(origin, 0, sizeof(*origin));
    *yaw = 0;
}
static void stage_fixture_range(void *p, MMActorEditRange *range)
{
    StageOwnerFixture *f = p;
    range->first = f->owner.runtime.first;
    range->last = f->owner.runtime.last;
}
static void stage_fixture_publish_range(void *p, const MMActorEditRange *range)
{
    StageOwnerFixture *f = p;
    f->owner.runtime.first = range->first;
    f->owner.runtime.last = range->last;
}
static void stage_fixture_pose(void *p, MMActorPoseInputs *inputs)
{
    (void)p;
    memset(inputs, 0, sizeof(*inputs));
}
static void stage_fixture_locate(void *p, const MMActorBodyPose *pose) { (void)p; (void)pose; }
static void stage_fixture_effect(void *p) { (void)p; }
static void stage_fixture_life(void *p) { ((StageOwnerFixture *)p)->owner.runtime.life_dirty = 1; }
static void stage_fixture_recording(void *p, int value)
{
    ((StageOwnerFixture *)p)->owner.runtime.recording.recording = value;
}
static int stage_fixture_flags(void *p, MMI32 action, MMU32 *out)
{
    (void)p;
    (void)action;
    *out = 8;
    return 1;
}
static int stage_fixture_apply(void *p, MMActorState *state, MMI32 index)
{
    StageOwnerFixture *f = p;
    MMActorTrack *track = &f->owner.track;
    MMActorEvent event;
    const MMActorMotionServices motion = mm_actor_track_motion_services();
    if (!mm_actor_events_get_fixed(track->events, index, &event))
        return 0;
    switch (event.aet)
    {
    case MM_ACTOR_ADD:
        state->addEvent = index;
        state->onStage = 1;
        ++f->entrances;
        return !f->fail_entrance;
    case MM_ACTOR_ACTION:
        state->actionEvent = index;
        return 1;
    case MM_ACTOR_REMOVE:
        state->onStage = 0;
        ++f->exits;
        return 1;
    default:
        return mm_actor_state_apply(state, track->route, track->events, index, &f->owner.runtime.origin,
                                    &motion) == MM_ACTOR_APPLIED;
    }
}
static MMActorStageEditServices stage_fixture_services(StageOwnerFixture *f)
{
    MMActorStageEditServices s;
    MMActorEditServices *edit = &s.subroute.route.edits;
    memset(&s, 0, sizeof(s));
    s.placement = stage_fixture_placement;
    s.subroute.live = &f->owner;
    s.subroute.timeline = mm_actor_track_timeline_services();
    s.subroute.lifetime = mm_actor_owner_lifetime(&f->owner);
    s.subroute.pose_inputs = stage_fixture_pose;
    s.subroute.locate_body = stage_fixture_locate;
    mm_actor_route_edit_bind_tables(&s.subroute.route);
    s.subroute.route.life_dirty = stage_fixture_life;
    s.subroute.route.recording = stage_fixture_recording;
    s.subroute.route.hide_body = s.subroute.route.default_costume = stage_fixture_effect;
    edit->context = f;
    edit->tables = mm_actor_edit_tables(f->owner.track.events, NULL);
    edit->apply_event = stage_fixture_apply;
    edit->get_range = stage_fixture_range;
    edit->publish_range = stage_fixture_publish_range;
    edit->dirty = edit->invalidate_range = stage_fixture_effect;
    s.subroute.action.context = f;
    s.subroute.action.sounds.edits = *edit;
    s.subroute.action.action_flags = stage_fixture_flags;
    s.subroute.action.mark_life_dirty = stage_fixture_life;
    return s;
}
#endif
