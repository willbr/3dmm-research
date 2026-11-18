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
    int status = mm_session_payload(in->archive, entry, sizeof(bytes), &payload);
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
    MMChunkAccess access = in->archive->access;
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
    request.access = in->archive->access;
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

typedef struct MovieDefaultTable
{
    const MMMovieBodyInput *input;
    MMMovieEvaluatedBody *body;
    MMMovieGroup group;
    int status;
} MovieDefaultTable;

static int movie_costume_id(void *context, MMI32 set, MMI32 index, MMI32 *id)
{
    MovieDefaultTable *table = (MovieDefaultTable *)context;
    const MMMovieGroup *group = &table->group;
    const unsigned char *row;
    MMI32 size, count;
    row = mm_session_group_row(group, (MMU32)set, &size);
    if (row && size >= 4)
    {
        count = mm_signed_word(mm_session_word(row, group->header.big_endian));
        if (index >= 0 && index < count && count <= size / 4)
        {
            *id = mm_signed_word(mm_session_word(row + 4 + (MMU32)index * 4, group->header.big_endian));
            return 1;
        }
    }
    table->status = MM_MOVIE_BAD_FILE;
    return 0;
}

static int movie_costume_prepare(void *context, MMI32 set, MMI32 id)
{
    MovieDefaultTable *table = (MovieDefaultTable *)context;
    MMMovieCustomCostume custom;
    table->status = mm_session_costume_find(table->input, id, &custom);
    if (table->status == MM_MOVIE_OK)
        table->status = custom.set == set ? mm_session_costume_apply(table->input, table->body, &custom)
                                        : MM_MOVIE_BAD_FILE;
    return table->status == MM_MOVIE_OK;
}

static int group_defaults(const MMMovieBodyInput *in, MMMovieEvaluatedBody *body, const MMChunkEntry *entry,
                         MMI32 sets)
{
    MovieDefaultTable table;
    MMTemplateDefaultServices defaults;
    table.status = mm_session_group(in->archive, entry, &table.group);
    if (table.status != MM_MOVIE_OK)
        return table.status;
    if (table.group.header.width == 4 && table.group.header.count == (MMU32)sets)
    {
        table.input = in;
        table.body = body;
        defaults.context = &table;
        defaults.costume_id = movie_costume_id;
        defaults.prepare = movie_costume_prepare;
        defaults.apply = NULL;
        mm_template_bind_defaults(sets, &defaults);
    }
    else
        table.status = MM_MOVIE_BAD_FILE;
    free(table.group.bytes);
    return table.status;
}

typedef struct MovieLegacyCostumes
{
    const MMMovieBodyInput *input;
    const MMChunkAccess *access;
    MMMovieEvaluatedBody *body;
    unsigned char *selected;
    MMU32 children;
    MMI32 sets, remaining;
    MMMovieCustomCostume candidate;
    int status;
} MovieLegacyCostumes;

static int movie_legacy_child(void *context, MMU32 ordinal, MMTemplateChild *out)
{
    MovieLegacyCostumes *legacy = (MovieLegacyCostumes *)context;
    MMChunkChild child;
    if (ordinal >= legacy->children)
        return 0;
    if (mm_chunk_child_at(legacy->access, legacy->input->template_position, ordinal, &child) != MM_CHUNK_OK)
    {
        legacy->status = MM_MOVIE_BAD_FILE;
        return -1;
    }
    out->type = child.type;
    out->number = child.number;
    out->id = child.slot;
    return 1;
}

static int movie_legacy_set(void *context, const MMTemplateChild *child, MMI32 *set)
{
    MovieLegacyCostumes *legacy = (MovieLegacyCostumes *)context;
    MMMovieCustomCostume *candidate = &legacy->candidate;
    MMChunkEntry entry;
    if (mm_chunk_find(legacy->access, child->type, child->number, &candidate->position) != MM_CHUNK_OK ||
        mm_chunk_at(legacy->access, candidate->position, &entry) != MM_CHUNK_OK)
    {
        legacy->status = MM_MOVIE_BAD_FILE;
        return 0;
    }
    legacy->status = read_custom(legacy->input, &entry, &candidate->set);
    if (legacy->status != MM_MOVIE_OK)
        return 0;
    *set = candidate->set;
    return 1;
}

static int movie_legacy_default(void *context, const MMTemplateChild *child, MMI32 set)
{
    MovieLegacyCostumes *legacy = (MovieLegacyCostumes *)context;
    (void)child;
    if (set < 0 || set >= legacy->sets || legacy->selected[set])
        return 1;
    legacy->status = mm_session_costume_apply(legacy->input, legacy->body, &legacy->candidate);
    if (legacy->status != MM_MOVIE_OK)
        return -1;
    legacy->selected[set] = 1;
    return --legacy->remaining != 0;
}

static int legacy_defaults(const MMMovieBodyInput *in, MMMovieEvaluatedBody *body, const MMChunkAccess *access,
                           MMI32 sets)
{
    MovieLegacyCostumes legacy;
    MMTemplateCostumeScan scan;
    MMChunkEntry parent;
    if (mm_chunk_at(access, in->template_position, &parent) != MM_CHUNK_OK)
        return MM_MOVIE_BAD_FILE;
    legacy.selected = (unsigned char *)calloc((size_t)sets, 1);
    if (!legacy.selected)
        return MM_MOVIE_MEMORY;
    legacy.input = in;
    legacy.access = access;
    legacy.body = body;
    legacy.children = parent.child_count;
    legacy.sets = legacy.remaining = sets;
    legacy.status = MM_MOVIE_OK;
    scan.context = &legacy;
    scan.child = movie_legacy_child;
    scan.costume_set = movie_legacy_set;
    scan.visit = movie_legacy_default;
    mm_template_scan_costumes(-1, &scan);
    free(legacy.selected);
    return legacy.status == MM_MOVIE_OK && legacy.remaining ? MM_MOVIE_ASSET_NOT_FOUND : legacy.status;
}

int mm_session_costume_defaults(const MMMovieBodyInput *in, MMMovieEvaluatedBody *body)
{
    MMChunkAccess access = in->archive->access;
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

int mm_session_costume_material(const MMMovieSession *movie, const MMMovieSource *source,
                                const MMMovieAsset *asset)
{
    MMMovieArchive archive;
    MMU32 position;
    int status;
    if (asset->type != MATERIAL_TYPE || asset->source < 0)
        return MM_MOVIE_BAD_FILE;
    status = mm_session_archive(movie, source, asset, &archive);
    if (status != MM_MOVIE_OK)
        return status;
    status = mm_chunk_find(&archive.access, asset->type, asset->number, &position);
    mm_session_archive_close(&archive);
    return status == MM_CHUNK_OK ? MM_MOVIE_OK
         : status == MM_CHUNK_NOT_FOUND ? MM_MOVIE_ASSET_NOT_FOUND : MM_MOVIE_BAD_FILE;
}
