#include "sound_archive_internal.h"
#include "mm/actor_data.h"
#include "mm/movie_sound.h"
#include "mm/scene_data.h"
#include "mm/scene_sound.h"
#include <string.h>

int mm_sound_archive_match(const MMMovieSoundArchiveCue *cue, const MMTag *tag, MMU32 child, int queue)
{
    return cue->source == tag->sid && (tag->sid == 0 ? cue->child == child : cue->number == tag->number) &&
           (cue->queue != 0) == (queue != 0);
}

static int adopt_sound(const MMMovieArchiveEditor *editor, MMU32 parent_type, MMU32 parent, MMU32 number, MMU32 *slot)
{
    const MMChunkAccess chunks = editor->chunks(editor->context);
    MMU32 next;
    if (mm_movie_sound_free_slot(&chunks, parent_type, parent, &next) != MM_CHUNK_OK ||
        next >= 0x7fffffffUL || !editor->adopt(editor->context, parent_type, parent, MM_SOUND_CHUNK, number, next))
        return 0;
    if (slot)
        *slot = next;
    return 1;
}

int mm_movie_archive_edit_sound(const MMMovieArchiveEditor *editor, MMU32 scene, const MMMovieSoundArchiveEdit *edit)
{
    MMChunkAccess chunks;
    MMChunkEntry root;
    MMU32 position, child;
    child = 0;
    if (edit->added)
    {
        chunks = editor->chunks(editor->context);
        if (mm_chunk_type_at(&chunks, MM_SOUND_MOVIE, 0, &root, &position) != MM_CHUNK_OK)
            return MM_SOUND_ARCHIVE_ROOT;
        if (!adopt_sound(editor, MM_SOUND_SCENE, scene, *edit->added, &child))
            return MM_SOUND_ARCHIVE_ADOPT_SCENE;
        if (!adopt_sound(editor, MM_SOUND_MOVIE, root.number, *edit->added, NULL))
            return MM_SOUND_ARCHIVE_ADOPT_MOVIE;
    }
    if (edit->cue && edit->cue->actor_sound)
        return mm_actor_archive_edit_sound(editor, scene, edit);
    return mm_scene_archive_edit_sound(editor, scene, edit, child);
}

typedef struct ImportedSoundLinks
{
    const MMChunkEdit *edit;
    const MMChunkAccess *chunks;
    MMU32 movie, scene;
} ImportedSoundLinks;

static int link_imported_sound(const ImportedSoundLinks *links, MMU32 slot, const MMTag *tag)
{
    MMChunkChild child;
    int status;
    if (tag->sid != 0 || tag->type != MM_SOUND_CHUNK)
        return 1;
    status = mm_chunk_link_number(links->chunks, MM_SOUND_SCENE, links->scene, slot, MM_SOUND_CHUNK, &child.number);
    if (status != MM_CHUNK_NOT_FOUND)
        return status == MM_CHUNK_OK;
    status = mm_chunk_link_number(links->chunks, MM_SOUND_MOVIE, links->movie, slot, MM_SOUND_CHUNK, &child.number);
    if (status != MM_CHUNK_OK)
        return status == MM_CHUNK_NOT_FOUND;
    child.type = MM_SOUND_CHUNK;
    child.slot = slot;
    return mm_chunk_adopt(links->edit, MM_SOUND_SCENE, links->scene, &child, 0) == MM_CHUNK_OK;
}

static void *actor_sound_sink(void *context, MMU32 width)
{
    (void)width;
    return context;
}

static void release_actor_sound_sink(void *context) { (void)context; }

static int link_actor_sound(void *context, MMU32 row, MMU32 bytes, const void *payload, const void *fixed)
{
    MMActorSound cue;
    MMI32 kind;
    (void)row;
    (void)bytes;
    memcpy(&kind, fixed, sizeof(kind));
    if (kind != MM_ACTOR_SOUND)
        return 1;
    memcpy(&cue, payload, sizeof(cue));
    return cue.no_sound || link_imported_sound((const ImportedSoundLinks *)context, cue.child, &cue.tag);
}

static int link_scene_sounds(MMMovieSceneSource *source, ImportedSoundLinks *links)
{
    const MMMovieSceneReader reader = mm_movie_scene_tag_reader(source);
    MMActorEventServices actor = {0};
    MMMovieSceneGroup group;
    MMMovieSceneEvent event;
    MMSceneSoundWire packet;
    MMTag tag;
    MMU32 i, slot, number;
    MMI32 j;
    int start, ok;
    void *sink;
    actor.context = links;
    actor.new_group = actor_sound_sink;
    actor.insert = link_actor_sound;
    actor.release = release_actor_sound_sink;
    for (start = 1; start >= 0; --start)
    {
        if (reader.group(reader.context, links->scene, start, &group) != MM_MOVIE_OK)
            return 0;
        ok = 1;
        for (i = 0; i < group.count && ok; ++i)
        {
            reader.event(reader.context, &group, i, &event);
            if (start && event.kind == MM_SCENE_ADD_ACTOR)
            {
                if (event.bytes != 4)
                {
                    ok = 0;
                    break;
                }
                slot = mm_movie_scene_event_word(&event, 0);
                ok = mm_chunk_link_number(links->chunks, MM_SOUND_SCENE, links->scene, slot, MM_ACTOR_CHUNK, &number) ==
                         MM_CHUNK_OK && mm_movie_actor_events(&source->graph, number, &actor, &sink) == MM_MOVIE_OK;
            }
            else if (!start && event.kind == MM_SCENE_PLAY_SOUND)
            {
                ok = mm_scene_sound_wire(event.data, event.bytes, event.big_endian, &packet);
                for (j = 0; ok && j < packet.header.ctagc; ++j)
                {
                    mm_scene_sound_wire_tag(&packet, j, &slot, &tag);
                    ok = link_imported_sound(links, slot, &tag);
                }
            }
        }
        reader.close(reader.context, &group);
        if (!ok)
            return 0;
    }
    return 1;
}

int mm_movie_archive_link_imported_sounds(const MMMovieSessionGraph *graph, MMU32 movie,
                                          const MMChunkEdit *edit)
{
    MMMovieSceneSource source;
    ImportedSoundLinks links;
    MMChunkAccess chunks;
    MMChunkEntry entry;
    MMChunkChild child;
    MMU32 row, i;
    graph->access(graph->context, &chunks);
    if (mm_chunk_find(&chunks, MM_SOUND_MOVIE, movie, &row) != MM_CHUNK_OK ||
        mm_chunk_at(&chunks, row, &entry) != MM_CHUNK_OK)
        return 0;
    for (i = 0; i < entry.child_count; ++i)
    {
        if (mm_chunk_child_at(&chunks, row, i, &child) != MM_CHUNK_OK)
            return 0;
        if (child.type == MM_SOUND_CHUNK)
            break;
    }
    if (i == entry.child_count)
        return 1;
    source.movie = NULL;
    source.graph = *graph;
    links.edit = edit;
    links.chunks = &chunks;
    links.movie = movie;
    /* Adoption adds only a child to a scene, so the movie row stays current. */
    for (i = 0; i < entry.child_count; ++i)
    {
        if (mm_chunk_child_at(&chunks, row, i, &child) != MM_CHUNK_OK)
            return 0;
        links.scene = child.number;
        if (child.type == MM_SOUND_SCENE && !link_scene_sounds(&source, &links))
            return 0;
    }
    return 1;
}
