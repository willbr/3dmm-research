#include "movie_session_internal.h"
#include "mm/actor_pose.h"
#include "mm/template_runtime.h"
#include <stdlib.h>

static void *part_array(MMU32 count, size_t width)
{
    if ((size_t)count != count || (count && (size_t)-1 / (size_t)count < width))
        return NULL;
    return calloc((size_t)count, width);
}

void mm_movie_body_meshes_dispose(MMMovieBodyMeshes *geometry)
{
    MMU32 i;
    if (!geometry)
        return;
    for (i = 0; i < geometry->model_count; ++i)
        mm_model_mesh_dispose(&geometry->models[i]);
    free(geometry->models);
    free(geometry->parts);
    free(geometry);
}

static int world_matrices(const MMMovieEvaluatedBody *body, MMMovieBodyMeshes *out)
{
    enum { UNVISITED, VISITING, COMPLETE };
    MMActorBodyPose pose;
    MMActorMatrix root;
    unsigned char *marks = (unsigned char *)part_array(body->part_count, 1);
    MMU32 *stack = (MMU32 *)part_array(body->part_count, sizeof(*stack));
    MMU32 i, depth, index;
    MMI32 parent;
    int status = MM_MOVIE_OK;
    if (!marks || !stack)
    {
        status = MM_MOVIE_MEMORY;
        goto done;
    }
    pose.translation = body->actor.translation;
    pose.orientation = body->actor.orientation;
    mm_actor_pose_root(&pose, &root);
    for (i = 0; i < body->part_count; ++i)
    {
        depth = 0;
        parent = (MMI32)i;
        while (parent != -1)
        {
            if (parent < 0 || (MMU32)parent >= body->part_count || marks[parent] == VISITING)
            {
                status = MM_MOVIE_BAD_FILE;
                goto done;
            }
            if (marks[parent] != UNVISITED)
                break;
            marks[parent] = VISITING;
            stack[depth++] = (MMU32)parent;
            parent = body->parts[parent].parent;
        }
        while (depth)
        {
            index = stack[--depth];
            parent = body->parts[index].parent;
            mm_matrix_multiply(&out->parts[index].world, &body->parts[index].matrix,
                               parent < 0 ? &root : &out->parts[parent].world);
            out->parts[index].model_index = -1;
            marks[index] = COMPLETE;
        }
    }
done:
    free(stack);
    free(marks);
    return status;
}

static int read_model(const MMMovieArchive *archive, const MMChunkEntry *entry,
                      MMModelMesh *mesh)
{
    MMMoviePayload payload;
    int status = mm_session_payload(archive, entry, 0, &payload);
    if (status != MM_MOVIE_OK)
        return status;
    status = mm_model_mesh_read(payload.stream, mesh);
    free(payload.decoded);
    switch (status)
    {
    case MM_MODEL_MESH_OK: return MM_MOVIE_OK;
    case MM_MODEL_MESH_IO: return MM_MOVIE_IO;
    case MM_MODEL_MESH_MEMORY: return MM_MOVIE_MEMORY;
    default: return MM_MOVIE_BAD_FILE;
    }
}

static int load_models(const MMMovieSession *movie, const MMMovieEvaluatedBody *body,
                       const MMMovieSource *source, MMMovieBodyMeshes *out)
{
    MMMovieArchive archive;
    MMU32 count = 0, i, j, position;
    MMU32 *numbers = NULL;
    const MMMovieAsset *asset;
    const MMMovieAsset *owner = &body->model_owner;
    MMChunkEntry entry;
    int status = MM_MOVIE_OK;
    for (i = 0; i < body->part_count; ++i)
        if (body->parts[i].active_model.type)
            ++count;
    if (!count)
        return MM_MOVIE_OK;
    if (!owner->type || owner->source < 0)
        return MM_MOVIE_BAD_FILE;
    status = mm_session_archive(movie, source, owner, &archive);
    if (status != MM_MOVIE_OK)
        return status;
    if (mm_chunk_find(&archive.access, owner->type, owner->number, &position) != MM_CHUNK_OK)
    {
        status = MM_MOVIE_ASSET_NOT_FOUND;
        goto done;
    }
    numbers = (MMU32 *)part_array(count, sizeof(*numbers));
    out->models = (MMModelMesh *)part_array(count, sizeof(*out->models));
    if (!numbers || !out->models)
    {
        status = MM_MOVIE_MEMORY;
        goto done;
    }
    for (i = 0; i < body->part_count; ++i)
    {
        asset = &body->parts[i].active_model;
        if (!asset->type)
            continue;
        if (asset->type != MM_TEMPLATE_BMDL || asset->source != owner->source)
        {
            status = MM_MOVIE_BAD_FILE;
            goto done;
        }
        for (j = 0; j < out->model_count && numbers[j] != asset->number; ++j)
            ;
        if (j == out->model_count)
        {
            if (mm_chunk_find(&archive.access, asset->type, asset->number, &position) != MM_CHUNK_OK ||
                mm_chunk_at(&archive.access, position, &entry) != MM_CHUNK_OK)
            {
                status = MM_MOVIE_ASSET_NOT_FOUND;
                goto done;
            }
            status = read_model(&archive, &entry, &out->models[j]);
            if (status != MM_MOVIE_OK)
                goto done;
            numbers[j] = asset->number;
            ++out->model_count;
        }
        out->parts[i].model_index = (MMI32)j;
    }
done:
    free(numbers);
    mm_session_archive_close(&archive);
    return status;
}

static int body_meshes(const MMMovieSession *movie, const MMMovieEvaluatedBody *body, const MMMovieSource *source,
                       MMMovieBodyMeshes **out)
{
    MMMovieBodyMeshes *geometry;
    int status = MM_MOVIE_OK;
    if (!movie || !body || !out || body->part_count > 0x7fffffffUL ||
        (body->part_count && !body->parts))
        return MM_MOVIE_BAD_FILE;
    geometry = (MMMovieBodyMeshes *)calloc(1, sizeof(*geometry));
    if (!geometry)
        return MM_MOVIE_MEMORY;
    geometry->part_count = body->part_count;
    if (body->part_count)
    {
        geometry->parts = (MMMoviePartMesh *)part_array(body->part_count, sizeof(*geometry->parts));
        status = geometry->parts ? world_matrices(body, geometry) : MM_MOVIE_MEMORY;
        if (status == MM_MOVIE_OK)
            status = load_models(movie, body, source, geometry);
    }
    if (status == MM_MOVIE_OK)
        *out = geometry;
    else
        mm_movie_body_meshes_dispose(geometry);
    return status;
}

int mm_movie_session_body_meshes(const MMMovieSession *movie, const MMMovieEvaluatedBody *body, MMMovieSourceRead source,
                                void *context, MMMovieBodyMeshes **out)
{
    MMMovieSource input;
    input.stream = source;
    input.graph = NULL;
    input.context = context;
    return body_meshes(movie, body, &input, out);
}

int mm_movie_session_body_meshes_graph(const MMMovieSession *movie, const MMMovieEvaluatedBody *body,
                                      MMMovieSourceGraph source, void *context, MMMovieBodyMeshes **out)
{
    MMMovieSource input;
    input.stream = NULL;
    input.graph = source;
    input.context = context;
    return body_meshes(movie, body, &input, out);
}
