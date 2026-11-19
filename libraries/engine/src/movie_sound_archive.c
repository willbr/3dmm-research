#include "sound_archive_internal.h"
#include "mm/movie_sound.h"

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
