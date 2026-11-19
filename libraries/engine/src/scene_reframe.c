#include "mm/scene_reframe.h"
#include "mm/actor_path.h"
#include "actor_archive_internal.h"
#include "mm/wide.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ACTOR_TYPE 0x41435452UL
#define SCENE_TYPE 0x5343454eUL
#define ROUTE_TYPE 0x50415448UL
#define TEMPLATE_TYPE 0x544d504cUL
#define TEXT_TYPE 0x54445420UL
#define THUMB_TYPE 0x5448554dUL

typedef struct Reframe
{
    double linear[3][3], translation[3];
    MMActorMatrix rotation;
} Reframe;

typedef struct Track
{
    MMActorPath *route;
    MMActorEventTable *events;
    const Reframe *change;
    const MMSceneReframeHost *host;
    const MMSceneReframePose *pose;
    MMActorPoint origin, pivot;
    MMActorLocation cut;
    MMI32 frame, active;
    int fixed_facing;
} Track;

static float real_value(MMI32 value) { return (float)value / 65536.0f; }

static int scalar(double value, MMI32 *out)
{
    if (!(value >= -32700.0 && value <= 32700.0))
        return 0;
    *out = (MMI32)((float)value * 65536.0f);
    return 1;
}

static int lens_reframe(const MMSceneReframeCamera *work, const MMSceneReframeCamera *camera, Reframe *out)
{
    const double radians = .008726646259971648;
    double magnification, scale[3], rotation, component;
    unsigned col, row, axis;
    magnification = tan(camera->vertical_fov * radians) / tan(work->vertical_fov * radians);
    scale[0] = magnification * camera->aspect / work->aspect;
    scale[1] = magnification;
    scale[2] = 1.0;
    if (!(scale[0] > 0 && scale[0] <= DBL_MAX && scale[1] > 0 && scale[1] <= DBL_MAX))
        return MM_SCENE_REFRAME_LENS;
    memset(out, 0, sizeof(*out));
    for (col = 0; col < 3; ++col)
    {
        out->translation[col] = camera->transform[9 + col];
        for (row = 0; row < 3; ++row)
        {
            rotation = 0;
            for (axis = 0; axis < 3; ++axis)
            {
                component = work->transform[axis * 3 + row] * (double)camera->transform[axis * 3 + col];
                out->linear[row][col] += component * scale[axis];
                rotation += component;
            }
            if (!scalar(rotation, &out->rotation.m[row][col]))
                return MM_SCENE_REFRAME_RANGE;
            out->translation[col] -= work->transform[9 + row] * out->linear[row][col];
        }
    }
    return MM_SCENE_REFRAME_OK;
}

static int point_reframe(const Reframe *change, MMActorPoint *point, const MMActorPoint *anchor)
{
    float input[3], origin[3];
    MMI32 output[3];
    double value;
    unsigned col, i;
    input[0] = real_value(point->dxr);
    input[1] = real_value(point->dyr);
    input[2] = real_value(point->dzr);
    origin[0] = real_value(anchor->dxr);
    origin[1] = real_value(anchor->dyr);
    origin[2] = real_value(anchor->dzr);
    for (col = 0; col < 3; ++col)
    {
        value = origin[col];
        for (i = 0; i < 3; ++i)
            value += ((double)input[i] - origin[i]) * change->linear[i][col];
        if (!scalar(value, &output[col]))
            return 0;
    }
    point->dxr = output[0];
    point->dyr = output[1];
    point->dzr = output[2];
    return 1;
}

/* World-space displacement at the unchanged route point. */
static int displacement(const Reframe *change, const MMActorPoint *translation,
                        const MMActorPoint *point, MMActorPoint *delta)
{
    double world[3], value;
    MMI32 output[3];
    unsigned col, i;
    world[0] = (double)real_value(translation->dxr) + real_value(point->dxr);
    world[1] = (double)real_value(translation->dyr) + real_value(point->dyr);
    world[2] = (double)real_value(translation->dzr) + real_value(point->dzr);
    for (col = 0; col < 3; ++col)
    {
        value = change->translation[col] - world[col];
        for (i = 0; i < 3; ++i)
            value += world[i] * change->linear[i][col];
        if (!scalar(value, &output[col]))
            return 0;
    }
    delta->dxr = output[0];
    delta->dyr = output[1];
    delta->dzr = output[2];
    return 1;
}

static int matrix_reframe(const Reframe *change, const MMActorMatrix *before, MMActorMatrix *out)
{
    unsigned row, col, i;
    double value;
    MMActorMatrix after;
    for (row = 0; row < 4; ++row)
        for (col = 0; col < 3; ++col)
        {
            value = 0;
            for (i = 0; i < 3; ++i)
                value += real_value(before->m[row][i]) * change->linear[i][col];
            if (!scalar(value, &after.m[row][col]))
                return 0;
        }
    *out = after;
    return 1;
}

static int location_le(const MMActorLocation *a, const MMActorLocation *b)
{
    if (a->irpt != b->irpt)
        return a->irpt < b->irpt;
    if (a->dwrOffset != b->dwrOffset)
        return a->dwrOffset < b->dwrOffset;
    return a->dnfrm <= b->dnfrm;
}

static void read_event(MMActorEventTable *events, MMU32 row, MMActorEvent *event)
{
    memcpy(event, mm_actor_events_fixed(events, row), sizeof(*event));
}

static int split_segment(Track *track)
{
    MMActorRoutePoint first, next, split;
    MMActorLocation old;
    MMActorEvent event;
    MMU32 i, node;
    int inserted;
    old = track->cut;
    if (old.irpt < 0 || (MMU32)old.irpt >= track->route->rows.count)
        return MM_SCENE_REFRAME_LOCATION;
    node = (MMU32)old.irpt;
    mm_actor_path_get(track->route, node, &first);
    track->pivot = first.xyz;
    if (!old.dwrOffset)
        return MM_SCENE_REFRAME_OK;
    if (old.dwrOffset < 0 || old.dwrOffset > first.dwr || node + 1 >= track->route->rows.count)
        return MM_SCENE_REFRAME_LOCATION;
    mm_actor_path_get(track->route, node + 1, &next);
    track->pivot = next.xyz;
    inserted = old.dwrOffset < first.dwr;
    if (inserted)
    {
        mm_actor_interpolate(&first.xyz, &next.xyz, mm_fixed_div(old.dwrOffset, first.dwr), &track->pivot);
        split.xyz = track->pivot;
        split.dwr = first.dwr - old.dwrOffset;
        if (!mm_actor_path_insert(track->route, node + 1, &split))
            return MM_SCENE_REFRAME_MEMORY;
        first.dwr = old.dwrOffset;
        mm_actor_path_put(track->route, node, &first);
    }
    for (i = 0; i < track->events->rows.count; ++i)
    {
        read_event(track->events, i, &event);
        if (event.rtel.irpt == old.irpt && event.rtel.dwrOffset >= old.dwrOffset)
        {
            ++event.rtel.irpt;
            event.rtel.dwrOffset -= old.dwrOffset;
        }
        else if (inserted && event.rtel.irpt > old.irpt)
        {
            if (event.rtel.irpt == 2147483647L)
                return MM_SCENE_REFRAME_LOCATION;
            ++event.rtel.irpt;
        }
        else
            continue;
        memcpy(mm_actor_events_fixed(track->events, i), &event, sizeof(event));
    }
    ++track->cut.irpt;
    track->cut.dwrOffset = 0;
    return MM_SCENE_REFRAME_OK;
}

static int locate_active_entrance(Track *track)
{
    MMU32 i;
    MMActorEvent event;
    track->active = -1;
    if (!track->pose)
        return MM_SCENE_REFRAME_OK;
    track->cut = track->pose->location;
    for (i = 0; i < track->events->rows.count; ++i)
    {
        read_event(track->events, i, &event);
        if (event.aet == MM_ACTOR_ADD && event.nfrm <= track->frame && location_le(&event.rtel, &track->cut))
            track->active = (MMI32)i;
    }
    if (track->active < 0)
        return MM_SCENE_REFRAME_ENTRANCE;
    read_event(track->events, (MMU32)track->active, &event);
    return event.nfrm < track->frame ? split_segment(track) : MM_SCENE_REFRAME_OK;
}

static void inverse_entrance(const MMActorMatrix *matrix, MMActorMatrix *inverse)
{
    unsigned i, j;
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j)
            inverse->m[i][j] = matrix->m[j][i];
    /* Euler entrance matrices have no translation. */
    for (i = 0; i < 3; ++i)
        inverse->m[3][i] = 0;
}

static int rewrite_entrance(Track *track, MMU32 row, MMActorEntrance *add,
                            const MMActorMatrix *entrance, int follows_path)
{
    MMActorPoint delta;
    MMActorMatrix transformed;
    if (!displacement(track->change, &track->origin, &add->translation, &delta) ||
        !scalar((double)real_value(add->translation.dxr) + real_value(delta.dxr), &add->translation.dxr) ||
        !scalar((double)real_value(add->translation.dyr) + real_value(delta.dyr), &add->translation.dyr) ||
        !scalar((double)real_value(add->translation.dzr) + real_value(delta.dzr), &add->translation.dzr))
        return 0;
    if (follows_path)
    {
        mm_matrix_multiply(&transformed, entrance, &track->change->rotation);
        track->host->to_euler(&transformed, add);
    }
    memcpy(mm_actor_events_payload(track->events, row, NULL), add, sizeof(*add));
    return 1;
}

static int track_event(Track *track, MMU32 row, MMI32 kind, const MMActorPoint *anchor,
                       const MMActorMatrix *entrance, const MMActorMatrix *inverse, int follows_path)
{
    MMActorPoint point;
    MMActorMatrix before, composed, transformed;
    void *payload;
    if (kind == MM_ACTOR_MOVE || kind == MM_ACTOR_TWEAK)
    {
        payload = mm_actor_events_payload(track->events, row, NULL);
        memcpy(&point, payload, sizeof(point));
        if (!point_reframe(track->change, &point, anchor))
            return 0;
        memcpy(payload, &point, sizeof(point));
    }
    else if (kind == MM_ACTOR_ROTATE_HERE || (kind == MM_ACTOR_ROTATE_FORWARD && !follows_path))
    {
        payload = mm_actor_events_payload(track->events, row, NULL);
        memcpy(&before, payload, sizeof(before));
        if (kind == MM_ACTOR_ROTATE_FORWARD)
        {
            mm_matrix_multiply(&composed, &before, entrance);
            if (!matrix_reframe(track->change, &composed, &transformed))
                return 0;
            mm_matrix_multiply(&before, &transformed, inverse);
        }
        else if (!matrix_reframe(track->change, &before, &before))
            return 0;
        memcpy(payload, &before, sizeof(before));
    }
    return 1;
}

static int insert_at_cut(Track *track, MMU32 index, int partial, MMU32 *end)
{
    MMActorPoint delta, point;
    MMActorEvent event;
    MMActorMatrix before = {{{0}}}, rotation;
    unsigned i;
    event.nfrm = track->frame;
    event.rtel = track->cut;
    if (partial)
    {
        if (!displacement(track->change, &track->pose->translation, &track->pivot, &delta))
            return MM_SCENE_REFRAME_RANGE;
        event.aet = MM_ACTOR_MOVE;
        if (!mm_actor_events_insert(track->events, index++, sizeof(delta), &delta, &event))
            return MM_SCENE_REFRAME_EVENTS;
        ++*end;
        point = track->pose->position;
        if (point.dxr != track->pivot.dxr || point.dyr != track->pivot.dyr || point.dzr != track->pivot.dzr)
        {
            if (!point_reframe(track->change, &point, &track->pivot))
                return MM_SCENE_REFRAME_RANGE;
            event.aet = MM_ACTOR_TWEAK;
            if (!mm_actor_events_insert(track->events, index++, sizeof(point), &point, &event))
                return MM_SCENE_REFRAME_EVENTS;
            ++*end;
        }
    }
    for (i = 0; i < 9; ++i)
        if (!scalar(track->pose->rotation[i], &before.m[i / 3][i % 3]))
            return MM_SCENE_REFRAME_RANGE;
    if (!matrix_reframe(track->change, &before, &rotation))
        return MM_SCENE_REFRAME_RANGE;
    event.aet = MM_ACTOR_ROTATE_HERE;
    if (!mm_actor_events_insert(track->events, index, sizeof(rotation), &rotation, &event))
        return MM_SCENE_REFRAME_EVENTS;
    ++*end;
    return MM_SCENE_REFRAME_OK;
}

static int reframe_subroute(Track *track, MMU32 start, MMU32 *end_out)
{
    static const MMActorPoint zero = {0, 0, 0};
    MMActorEvent entry, event;
    MMActorRoutePoint point;
    MMActorEntrance add;
    MMActorMatrix entrance, inverse, transformed, basis;
    MMU32 end, node, first, last, at, insert;
    int active, partial, follows_path, status;
    read_event(track->events, start, &entry);
    if (entry.aet != MM_ACTOR_ADD || entry.rtel.irpt < 0 || (MMU32)entry.rtel.irpt >= track->route->rows.count)
        return MM_SCENE_REFRAME_ENTRANCE;
    for (end = start + 1; end < track->events->rows.count; ++end)
    {
        read_event(track->events, end, &event);
        if (event.aet == MM_ACTOR_ADD)
            break;
    }
    active = (MMI32)start == track->active;
    partial = active && entry.nfrm < track->frame;
    if (!partial && entry.nfrm < track->frame)
    {
        *end_out = end;
        return MM_SCENE_REFRAME_OK;
    }
    first = partial ? (MMU32)track->cut.irpt + 1 : (MMU32)entry.rtel.irpt;
    last = end < track->events->rows.count ? (MMU32)event.rtel.irpt : track->route->rows.count;
    if (last < first || last > track->route->rows.count)
        return MM_SCENE_REFRAME_SUBROUTE;
    for (node = first; node < last; ++node)
    {
        mm_actor_path_get(track->route, node, &point);
        if (!point_reframe(track->change, &point.xyz, partial ? &track->pivot : &zero))
            return MM_SCENE_REFRAME_RANGE;
        /* Preserve interpolation distances and timing. */
        mm_actor_path_put(track->route, node, &point);
    }
    memcpy(&add, mm_actor_events_payload(track->events, start, NULL), sizeof(add));
    track->host->from_euler(&add, &entrance);
    inverse_entrance(&entrance, &inverse);
    mm_actor_path_get(track->route, (MMU32)entry.rtel.irpt, &point);
    follows_path = !track->fixed_facing && point.dwr != 0;
    if (!partial && !rewrite_entrance(track, start, &add, &entrance, follows_path))
        return MM_SCENE_REFRAME_RANGE;

    at = start + 1;
    if (!partial && !follows_path)
    {
        if (!matrix_reframe(track->change, &entrance, &transformed))
            return MM_SCENE_REFRAME_RANGE;
        mm_matrix_multiply(&basis, &transformed, &inverse);
        event = entry;
        event.aet = MM_ACTOR_ROTATE_FORWARD;
        if (!mm_actor_events_insert(track->events, at++, sizeof(basis), &basis, &event))
            return MM_SCENE_REFRAME_EVENTS;
        ++end;
    }
    /* Keep events already evaluated at the cut and transform later events. */
    insert = at;
    for (; at < end; ++at)
    {
        read_event(track->events, at, &event);
        if (active && location_le(&event.rtel, &track->cut))
        {
            insert = at + 1;
            if (partial)
                continue;
        }
        if (!track_event(track, at, event.aet, partial && event.aet == MM_ACTOR_TWEAK ? &track->pivot : &zero,
                         &entrance, &inverse, follows_path))
            return MM_SCENE_REFRAME_RANGE;
    }
    if (active)
    {
        status = insert_at_cut(track, insert, partial, &end);
        if (status != MM_SCENE_REFRAME_OK)
            return status;
    }
    *end_out = end;
    return MM_SCENE_REFRAME_OK;
}

static MMActorPath *read_route(const MMMovieArchiveEditor *editor, MMU32 actor)
{
    const MMActorPathFactory factory = {NULL, 0};
    MMChunkAccess chunks = editor->chunks(editor->context);
    MMReadStream stream;
    MMU32 number;
    if (mm_chunk_link_number(&chunks, ACTOR_TYPE, actor, 0, ROUTE_TYPE, &number) != MM_CHUNK_OK ||
        !editor->read(editor->context, ROUTE_TYPE, number, &stream))
        return NULL;
    return mm_actor_path_read(&stream, &factory);
}

static int reframe_actor(const MMMovieArchiveEditor *editor, MMU32 scene, const MMChunkChild *link,
                         MMI32 frame, const Reframe *change, const MMSceneReframeHost *host)
{
    MMActorHeader header;
    MMSceneReframePose pose;
    MMChunkAccess chunks;
    Track track;
    MMU32 actor, copy, number, model, text, row;
    int result;
    actor = link->number;
    if (!mm_actor_archive_read_actor(editor, actor, &header))
        return MM_SCENE_REFRAME_ACTOR;
    if (mm_actor_archive_copy_actor(editor, actor, &header, 0, &copy) != MM_ACTOR_EDIT_ARCHIVE_OK)
        return MM_SCENE_REFRAME_COPY;
    memset(&track, 0, sizeof(track));
    track.frame = frame;
    track.change = change;
    track.host = host;
    if (host->pose(host->context, header.actor_id, &pose))
        track.pose = &pose;
    track.origin.dxr = mm_signed_word(header.translation[0]);
    track.origin.dyr = mm_signed_word(header.translation[1]);
    track.origin.dzr = mm_signed_word(header.translation[2]);
    chunks = editor->chunks(editor->context);
    track.fixed_facing = header.source_id == 0 &&
        mm_chunk_link_number(&chunks, ACTOR_TYPE, actor, 0, TEMPLATE_TYPE, &model) == MM_CHUNK_OK &&
        mm_chunk_link_number(&chunks, TEMPLATE_TYPE, model, 0, TEXT_TYPE, &text) == MM_CHUNK_OK;
    result = MM_SCENE_REFRAME_TRACK;
    track.route = read_route(editor, actor);
    if (!track.route)
        goto done;
    track.events = mm_actor_archive_read_event_group(editor, actor, NULL);
    if (!track.events)
        goto done;
    result = locate_active_entrance(&track);
    for (row = 0; result == MM_SCENE_REFRAME_OK && row < track.events->rows.count;)
        result = reframe_subroute(&track, row, &row);
    if (result != MM_SCENE_REFRAME_OK)
        goto done;
    if (!mm_actor_archive_write_route(editor, copy, track.route->storage.buffers[0].data, track.route->rows.count) ||
        !mm_actor_archive_write_event_group(editor, track.events, &number, &copy))
    {
        result = MM_SCENE_REFRAME_WRITE;
        goto done;
    }
    editor->unlink(editor->context, SCENE_TYPE, scene, ACTOR_TYPE, actor, link->slot);
    if (!editor->adopt(editor->context, SCENE_TYPE, scene, ACTOR_TYPE, copy, link->slot))
        result = MM_SCENE_REFRAME_LINK;
done:
    if (track.events)
        mm_actor_events_release(track.events);
    if (track.route)
        mm_actor_path_release(track.route);
    return result;
}

int mm_scene_archive_reframe(const MMMovieArchiveEditor *editor, MMU32 scene, MMI32 frame,
                             const MMSceneReframeCamera *work, const MMSceneReframeCamera *camera,
                             const MMSceneReframeHost *host)
{
    Reframe change;
    MMChunkAccess chunks;
    MMChunkEntry entry;
    MMChunkChild child, *actors;
    MMU32 position, i, count, thumb;
    int result;
    result = lens_reframe(work, camera, &change);
    if (result != MM_SCENE_REFRAME_OK)
        return result;
    chunks = editor->chunks(editor->context);
    if (mm_chunk_find(&chunks, SCENE_TYPE, scene, &position) != MM_CHUNK_OK ||
        mm_chunk_at(&chunks, position, &entry) != MM_CHUNK_OK || !entry.child_count)
        return MM_SCENE_REFRAME_EMPTY;
    if ((size_t)entry.child_count > (size_t)-1 / sizeof(*actors))
        return MM_SCENE_REFRAME_MEMORY;
    actors = (MMChunkChild *)malloc(entry.child_count * sizeof(*actors));
    if (!actors)
        return MM_SCENE_REFRAME_MEMORY;
    count = 0;
    for (i = 0; i < entry.child_count; ++i)
    {
        if (mm_chunk_child_at(&chunks, position, i, &child) != MM_CHUNK_OK)
        {
            free(actors);
            return MM_SCENE_REFRAME_EMPTY;
        }
        if (child.type == ACTOR_TYPE)
            actors[count++] = child;
    }
    result = count ? MM_SCENE_REFRAME_OK : MM_SCENE_REFRAME_EMPTY;
    for (i = 0; i < count && result == MM_SCENE_REFRAME_OK; ++i)
        result = reframe_actor(editor, scene, &actors[i], frame, &change, host);
    free(actors);
    if (result == MM_SCENE_REFRAME_OK)
    {
        chunks = editor->chunks(editor->context);
        if (mm_chunk_link_number(&chunks, SCENE_TYPE, scene, 0, THUMB_TYPE, &thumb) == MM_CHUNK_OK)
            editor->unlink(editor->context, SCENE_TYPE, scene, THUMB_TYPE, thumb, 0);
    }
    return result;
}
