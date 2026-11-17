#include "movie_session_internal.h"
#include "mm/body_runtime.h"
#include "mm/material_resource.h"
#include "mm/template_runtime.h"
#include <stdlib.h>

#define MATERIAL_TYPE 0x4d54524cUL

static int read_custom(const MMMovieBodyInput *in, const MMChunkEntry *entry, MMI32 *set)
{
    const unsigned short one = 1;
    MMMoviePayload payload;
    unsigned char bytes[8];
    int status = mm_session_payload(in->stream, in->index, entry, sizeof(bytes), &payload);
    if (status != MM_MOVIE_OK)
        return status;
    if (!mm_read_exact(payload.stream, 0, bytes, sizeof(bytes)))
        status = MM_MOVIE_IO;
    else
        *set = mm_custom_material_read_set(bytes, *(const unsigned char *)&one == 0, NULL);
    free(payload.decoded);
    return status;
}

int mm_session_costume_find(const MMMovieBodyInput *in, MMI32 id, MMMovieCustomCostume *out)
{
    MMChunkAccess access = mm_chunk_index_access(in->index);
    MMChunkEntry entry;
    int status;
    if (id < 0 || in->text)
        return MM_MOVIE_ASSET_NOT_FOUND;
    status = mm_session_child(&access, in->template_position, (MMU32)id, MM_CMTL, &entry, &out->position);
    if (status != MM_CHUNK_OK)
        return status == MM_CHUNK_NOT_FOUND ? MM_MOVIE_ASSET_NOT_FOUND : MM_MOVIE_BAD_FILE;
    return read_custom(in, &entry, &out->set);
}

static void binding(MMMovieAsset *asset, MMI32 source, const MMChunkEntry *entry)
{
    asset->source = source;
    asset->type = entry->type;
    asset->number = entry->number;
}

typedef struct MovieCostumeBinding
{
    MMChunkAccess access;
    MMU32 position;
    int status;
} MovieCostumeBinding;

static MMI32 movie_part_set(void *context, void *body, MMI32 part)
{
    (void)context;
    return ((const MMMovieEvaluatedBody *)body)->parts[part].set;
}

static int movie_custom_part(void *context, void *target, MMI32 ordinal, MMI32 slot)
{
    MovieCostumeBinding *request = (MovieCostumeBinding *)context;
    MMMovieEvaluatedBody *body = (MMMovieEvaluatedBody *)target;
    MMMovieBodyPart *part = &body->parts[ordinal];
    MMChunkEntry entry;
    MMU32 position;
    int status = mm_session_child(&request->access, request->position, (MMU32)slot, MATERIAL_TYPE, &entry, &position);
    if (status != MM_CHUNK_OK)
    {
        request->status = status == MM_CHUNK_NOT_FOUND ? MM_MOVIE_ASSET_NOT_FOUND : MM_MOVIE_BAD_FILE;
        return 0;
    }
    binding(&part->material, body->model_owner.source, &entry);
    part->material_owner = body->model_owner;
    status = mm_session_child(&request->access, request->position, (MMU32)slot, MM_TEMPLATE_BMDL, &entry, &position);
    if (status == MM_CHUNK_OK)
        binding(&part->active_model, body->model_owner.source, &entry);
    else if (status != MM_CHUNK_NOT_FOUND)
    {
        request->status = MM_MOVIE_BAD_FILE;
        return 0;
    }
    return 1;
}

int mm_session_costume_apply(const MMMovieBodyInput *in, MMMovieEvaluatedBody *body, const MMMovieCustomCostume *custom)
{
    MovieCostumeBinding request;
    MMBodySetServices parts;
    MMI32 count;
    request.access = mm_chunk_index_access(in->index);
    request.position = custom->position;
    request.status = MM_MOVIE_OK;
    parts.context = &request;
    parts.part_set = movie_part_set;
    parts.apply_part = movie_custom_part;
    count = mm_body_apply_set(body, (MMI32)body->part_count, custom->set, &parts);
    if (count < 0)
        return request.status;
    return count ? MM_MOVIE_OK : MM_MOVIE_BAD_FILE;
}

static int movie_generic_part(void *context, void *body, MMI32 ordinal, MMI32 slot)
{
    const MMMovieAsset *asset = (const MMMovieAsset *)context;
    MMMovieBodyPart *part = &((MMMovieEvaluatedBody *)body)->parts[ordinal];
    (void)slot;
    part->material = *asset;
    part->material_owner = *asset;
    return 1;
}

void mm_session_apply_material(MMMovieEvaluatedBody *body, MMI32 set, const MMMovieAsset *asset)
{
    MMBodySetServices parts;
    parts.context = (void *)asset;
    parts.part_set = movie_part_set;
    parts.apply_part = movie_generic_part;
    mm_body_apply_set(body, (MMI32)body->part_count, set, &parts);
}

static int group_defaults(const MMMovieBodyInput *in, MMMovieEvaluatedBody *body, const MMChunkEntry *entry,
                         MMI32 sets)
{
    MMMovieGroup group;
    MMMovieCustomCostume custom;
    MMI32 set, size, count, id;
    const unsigned char *row;
    int status = mm_session_group(in->stream, in->index, entry, &group);
    if (status != MM_MOVIE_OK)
        return status;
    if (group.header.width != 4 || group.header.count != (MMU32)sets)
        status = MM_MOVIE_BAD_FILE;
    for (set = 0; set < sets && status == MM_MOVIE_OK; ++set)
    {
        row = mm_session_group_row(&group, (MMU32)set, &size);
        if (!row || size < 4)
        {
            status = MM_MOVIE_BAD_FILE;
            break;
        }
        count = mm_signed_word(mm_session_word(row, group.header.big_endian));
        id = mm_signed_word(mm_session_word(row + 4, group.header.big_endian));
        if (count <= 0 || count > size / 4)
        {
            status = MM_MOVIE_BAD_FILE;
            break;
        }
        status = mm_session_costume_find(in, id, &custom);
        if (status == MM_MOVIE_OK)
            status = custom.set == set ? mm_session_costume_apply(in, body, &custom) : MM_MOVIE_BAD_FILE;
    }
    free(group.bytes);
    return status;
}

static int legacy_defaults(const MMMovieBodyInput *in, MMMovieEvaluatedBody *body, const MMChunkAccess *access,
                           MMI32 sets)
{
    MMChunkEntry parent, entry;
    MMChunkChild child;
    MMMovieCustomCostume custom;
    unsigned char *applied;
    MMU32 i;
    MMI32 remaining = sets;
    int status = MM_MOVIE_OK;
    if (mm_chunk_at(access, in->template_position, &parent) != MM_CHUNK_OK)
        return MM_MOVIE_BAD_FILE;
    applied = (unsigned char *)calloc((size_t)sets, 1);
    if (!applied)
        return MM_MOVIE_MEMORY;
    for (i = 0; i < parent.child_count && status == MM_MOVIE_OK && remaining; ++i)
    {
        if (mm_chunk_child_at(access, in->template_position, i, &child) != MM_CHUNK_OK)
        {
            status = MM_MOVIE_BAD_FILE;
            break;
        }
        if (child.type != MM_CMTL)
            continue;
        if (mm_chunk_find(access, child.type, child.number, &custom.position) != MM_CHUNK_OK ||
            mm_chunk_at(access, custom.position, &entry) != MM_CHUNK_OK)
        {
            status = MM_MOVIE_BAD_FILE;
            break;
        }
        status = read_custom(in, &entry, &custom.set);
        if (status != MM_MOVIE_OK)
            break;
        /* Templates derive their default from the first costume in each set. */
        if (custom.set >= 0 && custom.set < sets && !applied[custom.set])
        {
            status = mm_session_costume_apply(in, body, &custom);
            applied[custom.set] = 1;
            --remaining;
        }
    }
    free(applied);
    return status == MM_MOVIE_OK && remaining ? MM_MOVIE_ASSET_NOT_FOUND : status;
}

int mm_session_costume_defaults(const MMMovieBodyInput *in, MMMovieEvaluatedBody *body)
{
    MMChunkAccess access = mm_chunk_index_access(in->index);
    MMChunkEntry entry;
    MMU32 i, position;
    MMI32 sets = 0;
    int status;
    for (i = 0; i < body->part_count; ++i)
        if (body->parts[i].set >= sets)
            sets = body->parts[i].set + 1;
    if (!sets)
        return MM_MOVIE_OK;
    status = mm_session_child(&access, in->template_position, 0, MM_GGCM, &entry, &position);
    if (status == MM_CHUNK_OK)
        return group_defaults(in, body, &entry, sets);
    if (status != MM_CHUNK_NOT_FOUND)
        return MM_MOVIE_BAD_FILE;
    return legacy_defaults(in, body, &access, sets);
}

int mm_session_costume_material(const MMMovieSession *movie, MMMovieSourceRead source, void *context,
                                const MMMovieAsset *asset)
{
    MMChunkAccess access;
    MMChunkIndex *external = NULL;
    MMReadStream stream;
    MMU32 position;
    int status;
    if (asset->type != MATERIAL_TYPE || asset->source < 0)
        return MM_MOVIE_BAD_FILE;
    if (asset->source)
    {
        if (!source)
            return MM_MOVIE_ASSET_NOT_FOUND;
        status = source(context, asset, &stream);
        if (status == MM_MOVIE_OK)
            status = mm_session_index(&stream, &movie->policy, &external);
        if (status != MM_MOVIE_OK)
            return status;
    }
    access = mm_chunk_index_access(external ? external : movie->index);
    status = mm_chunk_find(&access, asset->type, asset->number, &position);
    mm_chunk_index_close(external);
    return status == MM_CHUNK_OK ? MM_MOVIE_OK
         : status == MM_CHUNK_NOT_FOUND ? MM_MOVIE_ASSET_NOT_FOUND : MM_MOVIE_BAD_FILE;
}
