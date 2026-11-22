#include "live_document.h"
#include <stdio.h>
#include <string.h>

int marcus_scene_rows_init(MarcusSceneRows *rows, const MMStorageAllocator *allocator)
{
    rows->count = 0;
    return mm_storage_init(&rows->storage, allocator) == MM_STORAGE_OK;
}

void marcus_scene_rows_dispose(MarcusSceneRows *rows)
{
    mm_storage_dispose(&rows->storage);
    rows->count = 0;
}

int marcus_scene_rows_resize(MarcusSceneRows *rows, size_t count)
{
    if (count > (MM_STORAGE_LIMIT - 1) / sizeof(MarcusScene) ||
        mm_storage_ensure(&rows->storage, (MMU32)(count * sizeof(MarcusScene)), 0, 0) != MM_STORAGE_OK)
        return 0;
    rows->count = count;
    return 1;
}

int marcus_scene_rows_rebuild(MarcusSceneRows *rows, const MarcusSceneRows *base,
                              const MMMovieSceneDefinitions *definitions, const MMMovieRangeJournal *ranges)
{
    const MMPreviewLiveScene *added = mm_movie_scene_definitions_data(definitions);
    MarcusScene *scenes;
    size_t i;
    if (definitions->count > (size_t)-1 - base->count ||
        !marcus_scene_rows_resize(rows, base->count + definitions->count))
        return 0;
    scenes = (MarcusScene *)rows->storage.buffers[0].data;
    if (base->count)
        memcpy(scenes, base->storage.buffers[0].data, base->count * sizeof(*scenes));
    for (i = base->count; i < rows->count; ++i)
    {
        MarcusScene *scene = scenes + i;
        memset(scene, 0, sizeof(*scene));
        scene->chunk = added[i - base->count].number;
        scene->first_frame = scene->last_frame = 1;
        /* The storage limit bounds the scene count to at most nine digits. */
        sprintf(scene->name, "Scene %lu", (unsigned long)(i + 1));
    }
    for (i = 0; i < rows->count; ++i)
    {
        const MMMovieSceneRange *range = mm_movie_range_journal_find(ranges, (MMU32)scenes[i].chunk);
        if (range)
        {
            scenes[i].first_frame = range->first;
            scenes[i].last_frame = range->last;
        }
    }
    return 1;
}

int marcus_live_document_mount_boards(const MarcusBoardStore *store, const MarcusLiveDocumentServices *s)
{
    const MarcusStoredBoard *boards = marcus_board_store_data(store);
    MMU32 i;
    for (i = 0; i < store->count; ++i)
        if (boards[i].source && !s->mount(s->context, boards[i].source, marcus_stored_board_file(boards + i)))
            return 0;
    return 1;
}

static int mount_edit_sources(const MarcusLiveDocument *doc, const MarcusLiveDocumentServices *s)
{
    const MMMoviePropEdit *props = mm_movie_prop_journal_data(doc->definitions.props);
    const MMMovieActorTextEdit *texts = mm_movie_text_journal_data(doc->texts);
    const MMMovieActorTrackEdit *tracks = mm_movie_track_journal_data(doc->tracks);
    MMU32 i;
    if (!s->resources(s->context))
        return 0;
    if ((doc->definitions.props->count ||
         mm_movie_scene_definitions_uses_source(doc->definitions.scenes, doc->retail_source)) &&
        !s->retail(s->context))
        return 0;
    if (!marcus_live_document_mount_boards(doc->boards, s))
        return 0;
    for (i = 0; i < doc->definitions.props->count; ++i)
        if (props[i].file && !s->mount(s->context, props[i].definition.source, props[i].file))
            return 0;
    for (i = 0; i < doc->texts->count; ++i)
        if (!s->mount(s->context, texts[i].source, texts[i].file))
            return 0;
    for (i = 0; i < doc->tracks->count; ++i)
        if (!s->mount(s->context, tracks[i].source, tracks[i].file))
            return 0;
    return 1;
}

int marcus_live_document_apply(const MarcusLiveDocument *doc, const MarcusLiveDocumentServices *s,
                               MarcusSceneRows *scenes, const MarcusSceneRows *base,
                               MarcusActorNames *names, const MarcusActorNames *base_names)
{
    MMMovieLiveDefinitions definitions = doc->definitions;
    MMPreviewLiveDefinitions preview = mm_movie_definitions_preview(&definitions);
    const MMMoviePropEdit *props = mm_movie_prop_journal_data(definitions.props);
    MMU32 i;
    MarcusActorNames staged;
    int status = MARCUS_LIVE_DOCUMENT_MEMORY;
    if (!mount_edit_sources(doc, s) || !s->install(s->context, &preview))
        return MARCUS_LIVE_DOCUMENT_FAILED;
    if (!marcus_actor_names_init(&staged, &names->storage.allocator))
        return MARCUS_LIVE_DOCUMENT_MEMORY;
    if (!marcus_actor_names_copy(&staged, base_names))
        goto done;
    for (i = 0; i < definitions.props->count; ++i)
        if (!marcus_actor_names_set(&staged, props[i].definition.actor_id, props[i].name, strlen(props[i].name)))
            goto done;
    if (!s->text_names(s->context, doc->texts, &staged))
        status = MARCUS_LIVE_DOCUMENT_FAILED;
    else if (marcus_scene_rows_rebuild(scenes, base, definitions.scenes, doc->ranges))
    {
        marcus_actor_names_swap(names, &staged);
        status = MARCUS_LIVE_DOCUMENT_OK;
    }
done:
    marcus_actor_names_dispose(&staged);
    return status;
}
