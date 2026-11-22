#include "sound_edit_core.h"
#include "archive_file.h"
#include "mm/movie_sound.h"
#include "mm/sound_resource.h"
#include "mm/wave_import.h"
#include <SDL3/SDL.h>

int marcus_sound_edit_plan(const MarcusScene *scenes, size_t count, int selected,
                           const MarcusSoundSheet *sheet, int row, const MarcusSoundOptions *options,
                           int remove, int importing, const MarcusSoundAsset *asset,
                           MarcusSoundEditPlan *out, const char **message)
{
    MarcusSoundEditPlan plan = {0};
    const int music = options->style == MARCUS_SOUND_MUSIC;
    size_t scene = (size_t)selected;
    *message = NULL;
    if (selected < 0 || options->volume < 0 || options->volume > 65536 ||
        options->style < MARCUS_SOUND_EFFECT || options->style > MARCUS_SOUND_MUSIC ||
        (options->loop != 0 && options->loop != 1) || (options->queue != 0 && options->queue != 1))
    {
        *message = "Choose a scene and valid sound settings.";
        return MARCUS_SOUND_EDIT_FAILED;
    }
    if (asset && music != (asset->style == MARCUS_SOUND_MUSIC))
    {
        *message = "Music uses MIDI; sampled sounds use Effects or Speech.";
        return MARCUS_SOUND_EDIT_FAILED;
    }
    if (!importing && !asset)
    {
        const MMMovieSoundSheetCue *entry = marcus_sound_sheet_cue(sheet, row);
        const MMMovieTimingCue *cue;
        if (!entry)
            return MARCUS_SOUND_EDIT_FAILED;
        cue = &entry->source;
        scene = entry->origin_scene;
        plan.cue.actor_sound = (cue->flags & MM_TIMING_ACTOR_SOUND) != 0;
        plan.cue.queue = (cue->flags & MM_TIMING_QUEUE) != 0;
        if (!remove && music != (cue->style == MARCUS_SOUND_MUSIC))
        {
            *message = "Music uses MIDI; sampled sounds can switch between Effects and Speech.";
            return MARCUS_SOUND_EDIT_FAILED;
        }
        if (plan.cue.actor_sound && (options->frame != cue->frame || options->style != cue->style))
        {
            *message = "Actor sounds retain their animation binding and category.";
            return MARCUS_SOUND_EDIT_FAILED;
        }
        if (!remove && options->frame == cue->frame && options->volume == cue->volume &&
            options->style == cue->style && options->loop == ((cue->flags & MM_TIMING_LOOP) != 0) &&
            (!plan.cue.actor_sound || options->queue == plan.cue.queue))
            return MARCUS_SOUND_EDIT_UNCHANGED;
        plan.existing = 1;
        plan.cue.frame = cue->frame;
        plan.cue.source = cue->source;
        plan.cue.actor = cue->actor;
        plan.cue.style = cue->style;
        plan.cue.cel = cue->motion_cel;
        plan.cue.child = cue->child;
        plan.cue.number = cue->sound_number;
    }
    if (scene >= count || options->frame < scenes[scene].first_frame || options->frame > scenes[scene].last_frame)
    {
        *message = "The cue frame must be inside its scene.";
        return MARCUS_SOUND_EDIT_FAILED;
    }
    plan.scene = (MMU32)scenes[scene].chunk;
    plan.values.frame = (MMI32)options->frame;
    plan.values.volume = (MMI32)options->volume;
    plan.values.style = options->style;
    plan.values.loop = options->loop;
    plan.values.queue = options->queue;
    plan.values.remove = remove;
    *out = plan;
    return MARCUS_SOUND_EDIT_READY;
}

static int read_audio(void *context, MMU32 offset, void *bytes, size_t count)
{
    SDL_IOStream *input = context;
    return SDL_SeekIO(input, offset, SDL_IO_SEEK_SET) == offset && SDL_ReadIO(input, bytes, count) == count;
}

static const char *import_audio(MMPrivateArchive *file, const MarcusSoundImport *audio, int style, MMU32 *sound)
{
    SDL_IOStream *input = SDL_IOFromFile(audio->path, "rb");
    const char *error = NULL;
    MMReadStream source = {NULL, 0, read_audio};
    MMArchiveWriter writer = mm_private_archive_writer(file);
    Sint64 length;
    int midi, written;
    if (!input)
        return "Cannot open the audio file.";
    length = SDL_GetIOSize(input);
    midi = !SDL_strcasecmp(audio->extension, ".mid") || !SDL_strcasecmp(audio->extension, ".midi");
    if (length <= 0 || length > 64 * 1024 * 1024L)
        error = "Audio files must be between 1 byte and 64 MiB.";
    else if (midi != (style == MARCUS_SOUND_MUSIC))
        error = "Music cues use MIDI files; WAV/MP3 audio uses Effects or Speech.";
    else
    {
        source.context = input;
        source.length = (MMU32)length;
        writer.big_endian = 0;
        written = midi ? mm_sound_import_midi(&source, &audio->title, sound, &writer) :
                         audio->wave(&source, style, &audio->title, sound, &writer);
        if (midi && written == MM_SOUND_BAD_MIDI)
            error = "Cannot import this MIDI file.";
        else if (!midi && written == MM_WAVE_BAD_AUDIO)
            error = "Cannot decode this WAV or MP3 file.";
        else if (written <= 0)
            error = "Cannot encode this sound in the native movie format.";
    }
    SDL_CloseIO(input);
    return error;
}

static const char *edit_archive(MMPrivateArchive *file, const MarcusSoundEditPlan *plan, const MMU32 *added)
{
    static const char *const messages[] = {
        NULL,
        "Cannot find movie root.",
        "Cannot link imported sound.",
        "Cannot retain imported sound.",
        "Cannot read scene sound events.",
        "Invalid scene events.",
        "Cannot decode or encode the scene sound packet.",
        "The sound cue could not be found.",
        "Cannot allocate the sound edit.",
        "Cannot write scene sound events.",
        "Cannot link scene sound events.",
        "Cannot read sound owner.",
        "Cannot read actor sound track.",
        "Cannot write actor sound events.",
        "Cannot copy sound owner or its children.",
        "Cannot link sound owner."
    };
    const MMMovieArchiveEditor editor = mm_private_archive_editor(file);
    MMMovieSoundArchiveEdit edit = plan->values;
    int status;
    edit.cue = plan->existing ? &plan->cue : NULL;
    edit.added = added;
    status = mm_movie_archive_edit_sound(&editor, plan->scene, &edit);
    if (status < 0 || (size_t)status >= sizeof(messages) / sizeof(*messages))
        return "Cannot apply the native sound edit.";
    return messages[status];
}

const char *marcus_sound_edit_write(const char *candidate, const MMPrivateArchiveProfile *profile,
                                    const MarcusSoundEditPlan *plan, const MarcusSoundImport *audio,
                                    const char *library_path, MMU32 library_chunk)
{
    MMPrivateArchive *file = marcus_archive_open(candidate, MM_PRIVATE_ARCHIVE_EDIT, profile);
    const char *error = NULL;
    MMU32 added = 0;
    if (!file)
        return "Cannot open the sound working copy.";
    if (library_path)
    {
        MMPrivateArchive *library = marcus_archive_open(library_path, MM_PRIVATE_ARCHIVE_READ, profile);
        if (!library || !mm_private_archive_copy(library, file, MM_SOUND_TYPE, library_chunk, 0, &added))
            error = "Cannot copy the selected built-in sound.";
        mm_private_archive_close(library);
    }
    else if (audio)
        error = import_audio(file, audio, plan->values.style, &added);
    if (!error)
        error = edit_archive(file, plan, audio || library_path ? &added : NULL);
    if (!error && !mm_private_archive_save(file, MM_SOUND_CREATOR))
        error = "Cannot save sound edits.";
    mm_private_archive_close(file);
    return error;
}

const char *marcus_sound_preview_write(const char *candidate, const MMPrivateArchiveProfile *profile,
                                       const MarcusSoundImport *audio, int style)
{
    MMPrivateArchive *file = marcus_archive_open(candidate, MM_PRIVATE_ARCHIVE_CREATE, profile);
    const char *error;
    MMU32 sound = 0;
    if (!file)
        return "Cannot prepare this audio file.";
    error = import_audio(file, audio, style, &sound);
    if (!error && (sound != 0 || !mm_private_archive_save(file, MM_SOUND_CREATOR)))
        error = "Cannot prepare this audio file.";
    mm_private_archive_close(file);
    return error;
}
