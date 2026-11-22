#include "animation_request.h"

static void read_point(const void *points, size_t index, MMActorAnimationPoint *out)
{
    const MarcusPathPoint *point = (const MarcusPathPoint *)points + index;
    out->x = point->x;
    out->y = point->y;
    out->z = point->z;
}

int marcus_set_object_action(MarcusPlayer *player, int object, int action, int cel, int freeze)
{
    MMActorAnimation edit = {0};
    edit.kind = MM_ACTOR_ANIMATION_ACTION;
    edit.action = action;
    edit.cel = cel;
    edit.freeze = freeze != 0;
    edit.replace = 1;
    return marcus_edit_animation(player, object, &edit);
}

int marcus_tweak_object_path(MarcusPlayer *player, int object, float x, float y, float z)
{
    MMActorAnimation edit = {0};
    MarcusPathPoint point;
    point.x = x;
    point.y = y;
    point.z = z;
    edit.kind = MM_ACTOR_ANIMATION_TWEAK;
    edit.replace = 1;
    edit.points = &point;
    edit.point_count = 1;
    edit.read_point = read_point;
    return marcus_edit_animation(player, object, &edit);
}

int marcus_record_object_path(MarcusPlayer *player, int object, const MarcusPathPoint *points, int count, int replace)
{
    MMActorAnimation edit = {0};
    if (!points || count <= 0 || count > MARCUS_PATH_MAX_POINTS)
        return marcus_edit_animation(player, object, NULL);
    edit.kind = MM_ACTOR_ANIMATION_RECORD;
    edit.replace = replace != 0;
    edit.points = points;
    edit.point_count = (size_t)count;
    edit.read_point = read_point;
    return marcus_edit_animation(player, object, &edit);
}
