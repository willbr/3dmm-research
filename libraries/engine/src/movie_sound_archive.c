#include "sound_archive_internal.h"
#include "mm/movie_sound.h"

int mm_sound_archive_match(const MMMovieSoundArchiveCue *cue, const MMTag *tag, MMU32 child, int queue)
{
    return cue->source == tag->sid && (tag->sid == 0 ? cue->child == child : cue->number == tag->number) &&
           (cue->queue != 0) == (queue != 0);
}

static int adopt_sound(const MMMovieArchiveEditor *editor, MMU32 parent_type, MMU32 parent, MMU32 number, MMU32 *slot)
{
    MMChunkAccess chunks;
    MMU32 index, row, next;
    MMChunkChild child;
    chunks = editor->chunks(editor->context);
    if (mm_chunk_find(&chunks, parent_type, parent, &index) != MM_CHUNK_OK)
        return 0;
    next = 0;
    /* Child rows are sorted by slot. Other types can use the same slot. */
    for (row = 0; mm_chunk_child_at(&chunks, index, row, &child) == MM_CHUNK_OK; ++row)
    {
        if (child.slot > next)
            break;
        if (child.slot == next && child.type == MM_SOUND_CHUNK)
            ++next;
    }
    if (next >= 0x7fffffffUL || !editor->adopt(editor->context, parent_type, parent, MM_SOUND_CHUNK, number, next))
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
