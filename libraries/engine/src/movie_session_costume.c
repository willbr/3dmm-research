#include "movie_session_internal.h"
#include "mm/body_runtime.h"
#include "mm/material_resource.h"
#include "mm/template_runtime.h"
#include <stdlib.h>

static int read_custom(const MMMovieBodyInput *in, const MMChunkEntry *entry, MMI32 *set)
{
    const unsigned short one = 1;
    MMMoviePayload payload;
    unsigned char bytes[8];
    int status = mm_session_payload(&in->tmpl->archive, entry, sizeof(bytes), &payload);
    if (status != MM_MOVIE_OK)
        return status;
    if (!mm_read_exact(payload.stream, 0, bytes, sizeof(bytes)))
        status = MM_MOVIE_IO;
    else
        *set = mm_custom_material_read_set(bytes, *(const unsigned char *)&one == 0, NULL);
    free(payload.decoded);
    return status;
}

int mm_movie_costume_link(const MMChunkAccess *access, MMU32 parent, MMU32 id,
                          MMChunkEntry *out, MMU32 *position)
{
    MMU32 child;
    int status = mm_session_child(access, parent, id, MM_CMTL, out, &child);
    if (status != MM_CHUNK_OK)
        return status == MM_CHUNK_NOT_FOUND ? MM_MOVIE_ASSET_NOT_FOUND : MM_MOVIE_BAD_FILE;
    if (position)
        *position = child;
    return MM_MOVIE_OK;
}

int mm_session_costume_find(const MMMovieBodyInput *in, MMI32 id, MMMovieCustomCostume *out)
{
    MMChunkAccess access = in->tmpl->archive.access;
    MMChunkEntry entry;
    int status;
    if (id < 0 || in->text)
        return MM_MOVIE_ASSET_NOT_FOUND;
    status = mm_movie_costume_link(&access, in->tmpl->position, (MMU32)id, &entry, &out->position);
    if (status != MM_MOVIE_OK)
        return status;
    return read_custom(in, &entry, &out->set);
}

static void binding(MMMovieAsset *asset, MMI32 source, const MMMaterialChild *child)
{
    asset->source = source;
    asset->type = child->type;
    asset->number = child->number;
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
    MMCustomMaterialPart binding_part;
    int status = mm_custom_material_part(&request->access, request->position, (MMU32)slot, &binding_part);
    if (status != MM_CHUNK_OK)
    {
        request->status = MM_MOVIE_BAD_FILE;
        return 0;
    }
    if (!binding_part.material.type)
    {
        request->status = MM_MOVIE_ASSET_NOT_FOUND;
        return 0;
    }
    binding(&part->material, body->model_owner.source, &binding_part.material);
    part->material_owner = body->model_owner;
    if (binding_part.model.type)
        binding(&part->active_model, body->model_owner.source, &binding_part.model);
    return 1;
}

int mm_session_costume_apply(const MMMovieBodyInput *in, MMMovieEvaluatedBody *body, const MMMovieCustomCostume *custom)
{
    MovieCostumeBinding request;
    MMBodySetServices parts;
    MMI32 count;
    request.access = in->tmpl->archive.access;
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
    int status;
} MovieDefaultTable;

static int movie_costume_id(void *context, MMI32 set, MMI32 index, MMI32 *id)
{
    MovieDefaultTable *table = (MovieDefaultTable *)context;
    if (mm_template_costumes_id(table->input->tmpl->state.costumes, set, index, id))
        return 1;
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

static int group_defaults(const MMMovieBodyInput *in, MMMovieEvaluatedBody *body)
{
    MovieDefaultTable table;
    MMTemplateDefaultServices defaults;
    table.status = MM_MOVIE_OK;
    table.input = in;
    table.body = body;
    defaults.context = &table;
    defaults.costume_id = movie_costume_id;
    defaults.prepare = movie_costume_prepare;
    defaults.apply = NULL;
    mm_template_bind_defaults(in->tmpl->state.set_count, &defaults);
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
    if (mm_chunk_child_at(legacy->access, legacy->input->tmpl->position, ordinal, &child) != MM_CHUNK_OK)
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
    if (mm_chunk_at(access, in->tmpl->position, &parent) != MM_CHUNK_OK)
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
    MMMovieTemplate *tmpl = in->tmpl;
    MMTemplateRead reader;
    int status;
    if (!tmpl->state.set_count)
        return MM_MOVIE_OK;
    if (!tmpl->state.costumes && !tmpl->costumes_absent)
    {
        reader = mm_session_template_reader(tmpl);
        status = mm_template_read_costumes(&tmpl->state, &reader);
        if (status == MM_TEMPLATE_COSTUMES_ABSENT)
            tmpl->costumes_absent = 1;
        else if (status != MM_MOVIE_OK)
            return status;
    }
    return tmpl->state.costumes ? group_defaults(in, body)
                                : legacy_defaults(in, body, &tmpl->archive.access, tmpl->state.set_count);
}

int mm_session_costume_material(const MMMovieSession *movie, const MMMovieSource *source,
                                const MMMovieAsset *asset)
{
    MMMovieArchive archive;
    MMU32 position;
    int status;
    if (asset->type != MM_MATERIAL_CHUNK || asset->source < 0)
        return MM_MOVIE_BAD_FILE;
    status = mm_session_archive(movie, source, asset, &archive);
    if (status != MM_MOVIE_OK)
        return status;
    status = mm_chunk_find(&archive.access, asset->type, asset->number, &position);
    mm_session_archive_close(&archive);
    return status == MM_CHUNK_OK ? MM_MOVIE_OK
         : status == MM_CHUNK_NOT_FOUND ? MM_MOVIE_ASSET_NOT_FOUND : MM_MOVIE_BAD_FILE;
}
