#include "movie_session_internal.h"
#include "mm/action_definition.h"
#include "mm/preview_actor.h"
#include "mm/scene_data.h"
#include "mm/template_create.h"
#include "mm/template_runtime.h"
#include "mm/text_generation.h"
#include "mm/wide.h"
#include <stdlib.h>
#include <string.h>

#define ACTOR_TYPE 0x41435452UL
#define ROUTE_TYPE 0x50415448UL
#define ACTOR_EVENTS_TYPE 0x47474145UL

typedef struct ActorEvents
{
    MMCollectionStorage storage;
    MMGroup group;
    int *status;
} ActorEvents;

typedef struct ActorInput
{
    const MMMovieSession *movie;
    MMMovieSourceRead source;
    void *source_context;
    MMU32 actor_position;
    MMActorRoutePoint *route;
    MMI32 route_count;
    ActorEvents *events;
    MMReadStream template_stream;
    MMChunkIndex *content_index, *template_index;
    MMU32 template_position, action_position;
    unsigned short rest[3];
    int text, status;
    MMI32 action, cel_count;
    MMU32 action_flags;
    MMMovieGroup cels;
    MMMovieEvaluatedBody *evaluated_body;
    MMMovieCustomCostume custom;
    MMMovieAsset generic, text_material;
    MMPreviewActor actor;
    MMActorPoint translation;
    MMActorMatrix orientation;
} ActorInput;

static int fail(ActorInput *input, int status)
{
    input->status = status;
    return 0;
}

static void *new_events(void *p, MMU32 width)
{
    ActorInput *input = (ActorInput *)p;
    ActorEvents *events = (ActorEvents *)calloc(1, sizeof(*events));
    if (!events)
    {
        fail(input, MM_MOVIE_MEMORY);
        return NULL;
    }
    mm_storage_init(&events->storage, NULL);
    events->status = &input->status;
    events->group.storage = &events->storage;
    events->group.fixed_bytes = width;
    events->group.free_count = -1;
    return events;
}

static void release_events(void *p)
{
    ActorEvents *events = (ActorEvents *)p;
    if (events)
    {
        mm_storage_dispose(&events->storage);
        free(events);
    }
}

static MMU32 event_width(void *p) { return ((ActorEvents *)p)->group.fixed_bytes; }
static MMU32 event_count(void *p) { return ((ActorEvents *)p)->group.count; }
static void *event_fixed(void *p, MMU32 row)
{
    ActorEvents *events = (ActorEvents *)p;
    MMGroupLocation location;
    if (mm_group_location(&events->group, row, &location) != MM_STORAGE_OK)
        return NULL;
    return events->storage.buffers[0].data + location.offset;
}
static MMI32 event_size(void *p, MMU32 row)
{
    ActorEvents *events = (ActorEvents *)p;
    MMGroupLocation location;
    if (mm_group_location(&events->group, row, &location) != MM_STORAGE_OK)
        return -1;
    return location.bytes - (MMI32)events->group.fixed_bytes;
}
static void *event_data(void *p, MMU32 row)
{
    unsigned char *fixed = (unsigned char *)event_fixed(p, row);
    return fixed ? fixed + event_width(p) : NULL;
}
static int event_insert(void *p, MMU32 row, MMU32 size, const void *data, const void *fixed)
{
    ActorEvents *events = (ActorEvents *)p;
    int result = mm_group_insert(&events->group, row, size, data, fixed);
    if (result != MM_STORAGE_OK)
        *events->status = result == MM_STORAGE_MEMORY ? MM_MOVIE_MEMORY : MM_MOVIE_BAD_FILE;
    return result == MM_STORAGE_OK;
}

static int actor_entry(ActorInput *input, MMU32 type, MMChunkEntry *entry)
{
    MMChunkAccess access = mm_chunk_index_access(input->movie->index);
    MMU32 position;
    return mm_session_child(&access, input->actor_position, 0, type, entry, &position);
}

static int read_actor_header(void *p, MMU32 number, MMActorHeader *out)
{
    ActorInput *input = (ActorInput *)p;
    MMChunkAccess access = mm_chunk_index_access(input->movie->index);
    MMChunkEntry entry;
    MMMoviePayload payload;
    int status;
    (void)number;
    if (mm_chunk_at(&access, input->actor_position, &entry) != MM_CHUNK_OK)
        return fail(input, MM_MOVIE_BAD_FILE);
    status = mm_session_payload(&input->movie->stream, input->movie->index, &entry, 0, &payload);
    if (status != MM_MOVIE_OK)
        return fail(input, status);
    status = mm_actor_read_header(payload.stream, out);
    free(payload.decoded);
    return status ? 1 : fail(input, MM_MOVIE_BAD_FILE);
}

static int read_route(void *p, MMU32 number, void **out)
{
    ActorInput *input = (ActorInput *)p;
    MMChunkEntry entry;
    MMMoviePayload payload;
    MMCollectionHeader header;
    unsigned char bytes[MM_ACTOR_ROUTE_BYTES];
    MMActorRoutePoint *point;
    MMU32 i;
    int status;
    (void)number;
    if (actor_entry(input, ROUTE_TYPE, &entry) != MM_CHUNK_OK)
        return fail(input, MM_MOVIE_BAD_FILE);
    status = mm_session_payload(&input->movie->stream, input->movie->index, &entry, 0, &payload);
    if (status != MM_MOVIE_OK)
        return fail(input, status);
    status = mm_collection_header_read(payload.stream, MM_COLLECTION_ARRAY, 0, &header);
    if (status != MM_STORAGE_OK || header.width != sizeof(bytes) || (size_t)header.first_bytes != header.first_bytes)
    {
        free(payload.decoded);
        return fail(input, status == MM_STORAGE_IO ? MM_MOVIE_IO : MM_MOVIE_BAD_FILE);
    }
    input->route_count = (MMI32)header.count;
    if (header.count)
    {
        input->route = (MMActorRoutePoint *)calloc((size_t)header.count, sizeof(*input->route));
        if (!input->route)
        {
            free(payload.decoded);
            return fail(input, MM_MOVIE_MEMORY);
        }
    }
    for (i = 0; i < header.count; ++i)
    {
        if (!mm_read_exact(payload.stream, header.header_bytes + i * sizeof(bytes), bytes, sizeof(bytes)))
        {
            free(payload.decoded);
            return fail(input, MM_MOVIE_IO);
        }
        point = input->route + i;
        point->xyz.dxr = mm_signed_word(mm_session_word(bytes, header.big_endian));
        point->xyz.dyr = mm_signed_word(mm_session_word(bytes + 4, header.big_endian));
        point->xyz.dzr = mm_signed_word(mm_session_word(bytes + 8, header.big_endian));
        point->dwr = mm_signed_word(mm_session_word(bytes + 12, header.big_endian));
    }
    free(payload.decoded);
    *out = input;
    return 1;
}

static int read_events(void *p, MMU32 number, void **out)
{
    ActorInput *input = (ActorInput *)p;
    MMActorEventServices services;
    MMChunkEntry entry;
    MMMovieGroup group;
    ActorEvents *wire = NULL;
    MMU32 i;
    MMI32 size;
    const unsigned char *row;
    int status, order;
    const unsigned short one = 1;
    (void)number;
    if (actor_entry(input, ACTOR_EVENTS_TYPE, &entry) != MM_CHUNK_OK)
        return fail(input, MM_MOVIE_BAD_FILE);
    status = mm_session_group(&input->movie->stream, input->movie->index, &entry, &group);
    if (status != MM_MOVIE_OK)
        return fail(input, status);
    status = MM_MOVIE_BAD_FILE;
    if (group.header.width != MM_ACTOR_EVENT_BYTES)
        goto done;
    wire = (ActorEvents *)new_events(input, group.header.width);
    if (!wire)
    {
        status = MM_MOVIE_MEMORY;
        goto done;
    }
    for (i = 0; i < group.header.count; ++i)
    {
        row = mm_session_group_row(&group, i, &size);
        if (!row)
            goto done;
        if (!event_insert(wire, i, (MMU32)size, row + group.header.width, row))
        {
            status = input->status;
            goto done;
        }
    }
    services.context = input;
    services.fixed_width = event_width;
    services.count = event_count;
    services.fixed = event_fixed;
    services.size = event_size;
    services.data = event_data;
    services.new_group = new_events;
    services.insert = event_insert;
    services.release = release_events;
    order = (group.header.big_endian == (*(const unsigned char *)&one == 0)) ? MM_ACTOR_NATIVE : MM_ACTOR_OTHER;
    input->events = (ActorEvents *)mm_actor_read_events(wire, order, &services);
    wire = NULL;
    status = input->events ? MM_MOVIE_OK : (input->status ? input->status : MM_MOVIE_BAD_FILE);
done:
    release_events(wire);
    free(group.bytes);
    if (status != MM_MOVIE_OK)
        return fail(input, status);
    *out = input->events;
    return 1;
}

static int fetch_template(void *p, const MMActorHeader *header, void **token)
{
    ActorInput *input = (ActorInput *)p;
    MMMovieAsset asset;
    MMChunkAccess access;
    MMChunkEntry entry;
    MMMoviePayload payload;
    MMU32 position;
    unsigned char bytes[16];
    int status, big;
    unsigned i;
    const unsigned short one = 1;
    asset.source = header->source_id;
    asset.type = header->template_type;
    asset.number = header->template_number;
    input->template_stream = input->movie->stream;
    input->template_index = input->movie->index;
    if (asset.source < 0 || (asset.source && !input->source))
        return fail(input, MM_MOVIE_ASSET_NOT_FOUND);
    if (asset.source)
    {
        status = input->source(input->source_context, &asset, &input->template_stream);
        if (status == MM_MOVIE_OK)
            status = mm_session_index(&input->template_stream, &input->movie->policy, &input->content_index);
        if (status != MM_MOVIE_OK)
            return fail(input, status);
        input->template_index = input->content_index;
    }
    access = mm_chunk_index_access(input->template_index);
    status = mm_chunk_find(&access, asset.type, asset.number, &input->template_position);
    if (status == MM_CHUNK_NOT_FOUND)
        return fail(input, MM_MOVIE_ASSET_NOT_FOUND);
    if (status != MM_CHUNK_OK || mm_chunk_at(&access, input->template_position, &entry) != MM_CHUNK_OK)
        return fail(input, MM_MOVIE_BAD_FILE);
    status = mm_session_payload(&input->template_stream, input->template_index, &entry, 0, &payload);
    if (status != MM_MOVIE_OK)
        return fail(input, status);
    status = mm_read_exact(payload.stream, 0, bytes, sizeof(bytes));
    free(payload.decoded);
    if (!status)
        return fail(input, MM_MOVIE_BAD_FILE);
    big = bytes[0] == 0 && bytes[1] == 1;
    if (!big && (bytes[0] != 1 || bytes[1] != 0))
        big = *(const unsigned char *)&one == 0;
    for (i = 0; i < 3; ++i)
        input->rest[i] = big ? (unsigned short)((unsigned)bytes[4 + 2*i] * 256 + bytes[5 + 2*i])
                             : (unsigned short)((unsigned)bytes[5 + 2*i] * 256 + bytes[4 + 2*i]);
    status = mm_session_child(&access, input->template_position, 0, MM_TDT, &entry, &position);
    if (status != MM_CHUNK_OK && status != MM_CHUNK_NOT_FOUND)
        return fail(input, MM_MOVIE_BAD_FILE);
    input->text = status == MM_CHUNK_OK;
    *token = input;
    return 1;
}

static int load_action(ActorInput *input, MMI32 id)
{
    MMChunkAccess access;
    MMChunkEntry entry;
    MMMoviePayload payload;
    MMU32 parent, position;
    unsigned char bytes[8];
    int status, big;
    const unsigned short one = 1;
    if (id < 0)
        return fail(input, MM_MOVIE_BAD_FILE);
    if (input->action == id)
        return 1;
    input->action = -1;
    free(input->cels.bytes);
    input->cels.bytes = NULL;
    if (input->text)
    {
        input->cel_count = mm_text_cel_count(id);
        input->action_flags = mm_text_action_flags();
    }
    else
    {
        access = mm_chunk_index_access(input->template_index);
        if (mm_session_child(&access, input->template_position, (MMU32)id, MM_ACTN, &entry, &parent) != MM_CHUNK_OK)
            return fail(input, MM_MOVIE_ASSET_NOT_FOUND);
        input->action_position = parent;
        status = mm_session_payload(&input->template_stream, input->template_index, &entry, 0, &payload);
        if (status != MM_MOVIE_OK)
            return fail(input, status);
        status = mm_read_exact(payload.stream, 0, bytes, sizeof(bytes));
        free(payload.decoded);
        if (!status)
            return fail(input, MM_MOVIE_BAD_FILE);
        big = bytes[0] == 0 && bytes[1] == 1;
        if (!big && (bytes[0] != 1 || bytes[1] != 0))
            big = *(const unsigned char *)&one == 0;
        input->action_flags = mm_session_word(bytes + 4, big);
        if (mm_session_child(&access, parent, 0, MM_ACTION_GGCL, &entry, &position) != MM_CHUNK_OK)
            return fail(input, MM_MOVIE_ASSET_NOT_FOUND);
        status = mm_session_group(&input->template_stream, input->template_index, &entry, &input->cels);
        if (status != MM_MOVIE_OK)
            return fail(input, status);
        if (input->cels.header.width != 8)
            return fail(input, MM_MOVIE_BAD_FILE);
        input->cel_count = (MMI32)input->cels.header.count;
    }
    if (input->cel_count <= 0)
        return fail(input, MM_MOVIE_BAD_FILE);
    input->action = id;
    return 1;
}

static int step(void *p, MMI32 action, MMI32 cel, MMI32 *out)
{
    ActorInput *input = (ActorInput *)p;
    const unsigned char *row;
    MMI32 size;
    if (!load_action(input, action))
        return 0;
    if (input->text)
        *out = mm_text_cel_distance(action);
    else
    {
        cel = mm_template_wrap_cel(cel, input->cel_count);
        row = mm_session_group_row(&input->cels, (MMU32)cel, &size);
        if (!row)
            return fail(input, MM_MOVIE_BAD_FILE);
        *out = mm_signed_word(mm_session_word(row + 4, input->cels.header.big_endian));
    }
    return 1;
}

static MMU32 route_width(void *p) { (void)p; return MM_ACTOR_ROUTE_BYTES; }
static MMI32 route_count(void *p) { return ((ActorInput *)p)->route_count; }
static void route_point(void *p, MMI32 row, MMActorRoutePoint *out)
{
    ActorInput *input = (ActorInput *)p;
    if (row < 0 || row >= input->route_count)
    {
        fail(input, MM_MOVIE_BAD_FILE);
        memset(out, 0, sizeof(*out));
    }
    else
        *out = input->route[row];
}
static MMI32 motion_count(void *p) { return (MMI32)event_count(p); }
static MMI32 motion_size(void *p, MMI32 row) { return event_size(p, (MMU32)row); }
static void motion_event(void *p, MMI32 row, MMActorEvent *out)
{
    const void *fixed = event_fixed(p, (MMU32)row);
    if (fixed)
        memcpy(out, fixed, sizeof(*out));
    else
        memset(out, 0, sizeof(*out));
}
static void motion_payload(void *p, MMI32 row, void *out)
{
    MMI32 size = motion_size(p, row);
    if (size > 0)
        memcpy(out, event_data(p, (MMU32)row), (size_t)size);
}
static void put_event(void *p, MMI32 row, const MMActorEvent *value)
{
    void *fixed = event_fixed(p, (MMU32)row);
    if (fixed)
        memcpy(fixed, value, sizeof(*value));
}

static const MMActorMotionServices motion = {route_width, route_count, route_point, event_width, motion_count,
                                           motion_event, motion_size, motion_payload};

static MMMovieBodyInput body_input(const ActorInput *input)
{
    MMMovieBodyInput args;
    args.movie = input->movie;
    args.source = input->source;
    args.context = input->source_context;
    args.stream = &input->template_stream;
    args.index = input->template_index;
    args.template_position = input->template_position;
    args.action_position = input->action_position;
    args.text = input->text;
    args.cels = &input->cels;
    args.cel = 0;
    return args;
}

static int reset_costume(void *p, void *token, void *body)
{
    ActorInput *input = (ActorInput *)p;
    MMMovieBodyInput args;
    int status;
    (void)token;
    if (!body)
        return 1;
    if (input->text)
    {
        memset(&input->text_material, 0, sizeof(input->text_material));
        return 1;
    }
    args = body_input(input);
    status = mm_session_costume_defaults(&args, input->evaluated_body);
    if (status != MM_MOVIE_OK)
        return fail(input, status);
    return 1;
}

static void apply_material(ActorInput *input, MMI32 set, const MMMovieAsset *material)
{
    if (input->text && set == 0)
        input->text_material = *material;
    else
        mm_session_apply_material(input->evaluated_body, set, material);
}

static void effect(void *p, const MMActorEffectCommand *command, MMActorEffectReply *reply)
{
    ActorInput *input = (ActorInput *)p;
    MMMovieBodyInput args;
    int status = MM_MOVIE_OK;
    switch (command->operation)
    {
    case MM_EFFECT_CEL_COUNT:
    case MM_EFFECT_ACTION_FLAGS:
        reply->success = load_action(input, command->first);
        if (reply->success)
        {
            reply->value = input->cel_count;
            reply->flags = input->action_flags;
        }
        break;
    case MM_EFFECT_SET_ACTION_CEL:
        reply->success = load_action(input, command->first);
        if (reply->success && !input->text)
        {
            args = body_input(input);
            args.cel = mm_template_wrap_cel(command->second, input->cel_count);
            status = mm_session_body_cel(&args, input->evaluated_body);
        }
        break;
    case MM_EFFECT_DEFAULT_COSTUME:
        reply->success = reset_costume(input, NULL, input->evaluated_body);
        break;
    case MM_EFFECT_FETCH_CUSTOM:
        args = body_input(input);
        status = mm_session_costume_find(&args, command->first, &input->custom);
        if (status == MM_MOVIE_OK)
            reply->resource = &input->custom;
        break;
    case MM_EFFECT_APPLY_CUSTOM:
        args = body_input(input);
        status = mm_session_costume_apply(&args, input->evaluated_body, (const MMMovieCustomCostume *)command->resource);
        break;
    case MM_EFFECT_FETCH_MATERIAL:
        input->generic.source = command->tag->sid;
        input->generic.type = command->tag->type;
        input->generic.number = command->tag->number;
        status = mm_session_costume_material(input->movie, input->source, input->source_context, &input->generic);
        if (status == MM_MOVIE_OK)
            reply->resource = &input->generic;
        break;
    case MM_EFFECT_APPLY_MATERIAL:
        apply_material(input, command->first, (const MMMovieAsset *)command->resource);
        break;
    default:
        break;
    }
    if (status != MM_MOVIE_OK)
        reply->success = fail(input, status);
}
static int apply(void *p, MMI32 index)
{
    ActorInput *input = (ActorInput *)p;
    MMActorEffectServices effects;
    MMActorPoint origin;
    effects.context = p;
    effects.write_event = put_event;
    effects.run = effect;
    effects.publish = NULL;
    origin.dxr = mm_signed_word(input->actor.header.translation[0]);
    origin.dyr = mm_signed_word(input->actor.header.translation[1]);
    origin.dzr = mm_signed_word(input->actor.header.translation[2]);
    return mm_actor_effect_apply(&input->actor.state, p, input->events, index, &origin, input->evaluated_body != NULL,
                                 input->evaluated_body == NULL, &motion, &effects) && input->status == MM_MOVIE_OK;
}
static int fault(void *p) { return ((ActorInput *)p)->status == MM_MOVIE_OK ? 0 : -1; }
static int bind(void *p, void *token) { (void)p; (void)token; return 1; }
static int create_body(void *p, void *token, void **out)
{
    ActorInput *input = (ActorInput *)p;
    MMMovieBodyInput args;
    int status;
    if (input->evaluated_body && !input->text)
    {
        args = body_input(input);
        status = mm_session_body_shape(&args, input->evaluated_body, &input->actor.header);
        if (status != MM_MOVIE_OK)
            return fail(input, status);
        if (!reset_costume(p, token, input->evaluated_body))
            return 0;
    }
    *out = input->evaluated_body;
    return 1;
}
static int text(void *p, void *token) { (void)token; return ((ActorInput *)p)->text; }
static void rest(void *p, void *token, unsigned short out[3])
{
    (void)token;
    memcpy(out, ((ActorInput *)p)->rest, 3 * sizeof(*out));
}
static void locate(void *p, void *token, const MMActorPoint *point, const MMActorMatrix *matrix)
{
    ActorInput *input = (ActorInput *)p;
    (void)token;
    input->translation = *point;
    input->orientation = *matrix;
}
static int pose(void *p, void *t, void *b, MMI32 action, MMI32 cel)
{
    (void)t;
    (void)b;
    (void)cel;
    return load_action((ActorInput *)p, action);
}
static void clear(void *p) { (void)p; }
static int cues(void *p) { (void)p; return 1; }

static int seek(ActorInput *input, MMU32 number, MMI32 frame, MMMovieEvaluatedActor *out)
{
    MMChunkAccess chunks = mm_chunk_index_access(input->movie->index);
    MMPreviewActorServices services;
    int ok;
    memset(&services, 0, sizeof(services));
    services.context = input;
    services.timeline.context = input;
    services.timeline.motion = motion;
    services.timeline.resolve = step;
    services.timeline.put_event = put_event;
    services.fault = fault;
    services.header = read_actor_header;
    services.route = read_route;
    services.events = read_events;
    services.fetch = fetch_template;
    services.bind = bind;
    services.body_create = create_body;
    services.text = text;
    services.rest = rest;
    services.locate = locate;
    services.pose = pose;
    services.apply = apply;
    services.clear_cues = services.clear_motion = clear;
    services.default_costume = reset_costume;
    services.motion_cues = cues;
    mm_preview_actor_init(&input->actor);
    ok = mm_preview_actor_load(&input->actor, number, &chunks, 1, &services) > 0 &&
         mm_preview_actor_seek(&input->actor, frame, &services) > 0 && input->status == MM_MOVIE_OK;
    if (!ok)
        return input->status ? input->status : MM_MOVIE_BAD_FILE;
    out->number = number;
    out->header = input->actor.header;
    out->state = input->actor.state;
    out->translation = input->translation;
    out->orientation = input->orientation;
    return MM_MOVIE_OK;
}

static int evaluate_body(ActorInput *input, const MMMovieEvaluatedActor *actor)
{
    MMMovieBodyInput arguments;
    MMMovieEvaluatedBody *body = input->evaluated_body;
    int status;
    body->actor = *actor;
    if (!actor->state.onStage)
    {
        free(body->parts);
        body->parts = NULL;
        body->part_count = 0;
        return MM_MOVIE_OK;
    }
    if (!load_action(input, actor->state.action))
        return input->status;
    arguments = body_input(input);
    arguments.cel = mm_template_wrap_cel(actor->state.cel, input->cel_count);
    status = mm_session_body_parts(&arguments, body);
    if (status == MM_MOVIE_OK && input->text)
        mm_session_apply_material(body, 0, &input->text_material);
    return status;
}

static int evaluate_slot(const MMMovieSession *movie, const MMChunkAccess *access, MMU32 scene_position,
                         MMU32 slot, MMI32 frame, MMMovieSourceRead source, void *context,
                         MMMovieEvaluatedActor *out, MMMovieEvaluatedBody **body)
{
    MMChunkEntry entry;
    ActorInput input;
    int status;
    memset(&input, 0, sizeof(input));
    input.movie = movie;
    input.source = source;
    input.source_context = context;
    input.action = -1;
    if (mm_session_child(access, scene_position, slot, ACTOR_TYPE, &entry, &input.actor_position) != MM_CHUNK_OK)
        return MM_MOVIE_BAD_FILE;
    if (body)
    {
        input.evaluated_body = (MMMovieEvaluatedBody *)calloc(1, sizeof(*input.evaluated_body));
        if (!input.evaluated_body)
            return MM_MOVIE_MEMORY;
    }
    status = seek(&input, entry.number, frame, out);
    if (status == MM_MOVIE_OK && body)
        status = evaluate_body(&input, out);
    if (status == MM_MOVIE_OK && body)
        *body = input.evaluated_body;
    else
        mm_movie_body_dispose(input.evaluated_body);
    free(input.cels.bytes);
    mm_chunk_index_close(input.content_index);
    release_events(input.events);
    free(input.route);
    return status;
}

static int next_actor_start(const MMMovieGroup *starts, MMU32 *cursor, MMU32 *slot)
{
    const unsigned char *row;
    MMI32 size;
    int big = starts->header.big_endian;
    while (*cursor < starts->header.count)
    {
        row = mm_session_group_row(starts, *cursor, &size);
        ++*cursor;
        if (mm_session_word(row + 4, big) == MM_SCENE_ADD_ACTOR)
        {
            *slot = mm_session_word(row + 8, big);
            return 1;
        }
    }
    return 0;
}

static int read_actor_scene(const MMMovieSession *movie, const MMChunkAccess *access, MMU32 scene_slot, MMI32 frame,
                              MMU32 *position, MMMovieSessionScene *scene, MMMovieGroup *starts)
{
    int status = mm_session_scene(movie, access, scene_slot, position, scene);
    if (status != MM_MOVIE_OK)
        return status;
    if (frame < scene->first_frame || frame > scene->last_frame || frame == 2147483647L)
        return MM_MOVIE_FRAME_OUT_OF_RANGE;
    return mm_session_scene_group(movie, access, *position, 1, starts);
}

static int evaluate(const MMMovieSession *movie, MMU32 scene_slot, MMI32 frame, MMU32 ordinal,
                    MMMovieSourceRead source, void *context, MMMovieEvaluatedActor *out,
                    MMMovieEvaluatedBody **body)
{
    MMChunkAccess access;
    MMMovieSessionScene scene;
    MMMovieGroup starts;
    MMU32 position, slot, cursor = 0;
    int status;
    if (!movie || !movie->index || !out)
        return MM_MOVIE_BAD_FILE;
    access = mm_chunk_index_access(movie->index);
    status = read_actor_scene(movie, &access, scene_slot, frame, &position, &scene, &starts);
    if (status != MM_MOVIE_OK)
        return status;
    while (next_actor_start(&starts, &cursor, &slot))
        if (ordinal-- == 0)
        {
            free(starts.bytes);
            return evaluate_slot(movie, &access, position, slot, frame, source, context, out, body);
        }
    free(starts.bytes);
    return MM_MOVIE_ACTOR_NOT_FOUND;
}

int mm_movie_session_actor(const MMMovieSession *movie, MMU32 scene_slot, MMI32 frame, MMU32 ordinal,
                           MMMovieSourceRead source, void *context, MMMovieEvaluatedActor *out)
{
    return evaluate(movie, scene_slot, frame, ordinal, source, context, out, NULL);
}

int mm_movie_session_body(const MMMovieSession *movie, MMU32 scene_slot, MMI32 frame, MMU32 ordinal,
                          MMMovieSourceRead source, void *context, MMMovieEvaluatedBody **out)
{
    MMMovieEvaluatedActor actor;
    if (!out)
        return MM_MOVIE_BAD_FILE;
    return evaluate(movie, scene_slot, frame, ordinal, source, context, &actor, out);
}

typedef struct FrameInput
{
    const MMMovieSession *movie;
    MMChunkAccess access;
    MMMovieGroup starts;
    MMU32 scene_slot, scene_position, cursor;
    MMMovieSourceRead source;
    void *source_context;
    MMMovieEvaluatedFrame *result;
} FrameInput;

static int begin_frame(void *context, MMI32 frame)
{
    FrameInput *input = (FrameInput *)context;
    MMMovieEvaluatedFrame *result = input->result;
    MMMovieGroup starts;
    MMU32 slot, cursor = 0;
    size_t bytes;
    int status = read_actor_scene(input->movie, &input->access, input->scene_slot, frame,
                                  &input->scene_position, &result->scene, &starts);
    if (status != MM_MOVIE_OK)
        return status;
    input->starts = starts;
    status = mm_session_camera_at(input->movie, &input->access, input->scene_position, &input->starts,
                                   frame, input->source, input->source_context, &result->camera);
    if (status != MM_MOVIE_OK)
        return status;
    result->frame = frame;
    while (next_actor_start(&input->starts, &cursor, &slot))
        ++result->actor_count;
    if (result->actor_count)
    {
        bytes = (size_t)result->actor_count * sizeof(*result->actors);
        if (bytes / sizeof(*result->actors) != result->actor_count)
            return MM_MOVIE_MEMORY;
        result->actors = (MMMovieEvaluatedActor *)malloc(bytes);
        if (!result->actors)
            return MM_MOVIE_MEMORY;
    }
    return MM_MOVIE_OK;
}

static MMU32 frame_actor_count(void *context)
{
    return ((const FrameInput *)context)->result->actor_count;
}

static int seek_frame_actor(void *context, MMU32 ordinal, MMI32 frame)
{
    FrameInput *input = (FrameInput *)context;
    MMU32 slot;
    if (!next_actor_start(&input->starts, &input->cursor, &slot))
        return MM_MOVIE_BAD_FILE;
    return evaluate_slot(input->movie, &input->access, input->scene_position, slot, frame, input->source,
                          input->source_context, &input->result->actors[ordinal], NULL);
}

int mm_movie_session_seek(const MMMovieSession *movie, MMU32 scene_slot, MMI32 frame,
                          MMMovieSourceRead source, void *context, MMMovieEvaluatedFrame **out)
{
    FrameInput input;
    MMMovieFrameServices services;
    int status;
    if (!movie || !movie->index || !out)
        return MM_MOVIE_BAD_FILE;
    memset(&input, 0, sizeof(input));
    input.movie = movie;
    input.access = mm_chunk_index_access(movie->index);
    input.scene_slot = scene_slot;
    input.source = source;
    input.source_context = context;
    input.result = (MMMovieEvaluatedFrame *)calloc(1, sizeof(*input.result));
    if (!input.result)
        return MM_MOVIE_MEMORY;
    services.context = &input;
    services.begin = begin_frame;
    services.actor_count = frame_actor_count;
    services.seek_actor = seek_frame_actor;
    services.finish = NULL;
    status = mm_movie_seek_frame(frame, &services);
    free(input.starts.bytes);
    if (status == MM_MOVIE_OK)
        *out = input.result;
    else
        mm_movie_frame_dispose(input.result);
    return status;
}

void mm_movie_frame_dispose(MMMovieEvaluatedFrame *frame)
{
    if (frame)
    {
        free(frame->actors);
        free(frame);
    }
}
