#include "archive_file.h"
#include "mm/actor_archive.h"
#include "mm/movie_scene_journal.h"
#include "mm/scene_data.h"
#include "mm/string_ops.h"
#include <SDL3/SDL.h>
#include <string.h>

typedef struct ArchiveFile
{
    SDL_IOStream *stream;
    size_t references;
} ArchiveFile;

static void retain(void *context, void *file)
{
    (void)context;
    ++((ArchiveFile *)file)->references;
}

static void release(void *p)
{
    ArchiveFile *file = p;
    if (--file->references == 0)
    {
        SDL_CloseIO(file->stream);
        SDL_free(file);
    }
}

static void release_block(void *context, void *file)
{
    (void)context;
    release(file);
}

static void *create_temp(void *context)
{
    (void)context;
    return NULL;
}

static int read_at(void *p, MMU32 offset, void *data, size_t bytes)
{
    SDL_IOStream *stream = ((ArchiveFile *)p)->stream;
    return !bytes || (SDL_SeekIO(stream, offset, SDL_IO_SEEK_SET) == offset &&
                      SDL_ReadIO(stream, data, bytes) == bytes);
}

static int write_at(void *p, MMU32 offset, const void *data, size_t bytes)
{
    SDL_IOStream *stream = ((ArchiveFile *)p)->stream;
    return !bytes || (SDL_SeekIO(stream, offset, SDL_IO_SEEK_SET) == offset &&
                      SDL_WriteIO(stream, data, bytes) == bytes &&
                      (offset != 0 || SDL_FlushIO(stream)));
}

static MMU32 size(void *p)
{
    const Sint64 bytes = SDL_GetIOSize(((ArchiveFile *)p)->stream);
    return bytes >= 0 && bytes <= 0x7fffffffL ? (MMU32)bytes : 0xffffffffUL;
}

static int extend(void *file, MMU32 bytes)
{
    const MMU32 end = size(file);
    const unsigned char zero = 0;
    return bytes == end || (bytes > end && write_at(file, bytes - 1, &zero, 1));
}

static MMReadStream read_stream(void *file)
{
    MMReadStream stream = {NULL, 0, read_at};
    const MMU32 bytes = size(file);
    stream.context = file;
    if (bytes <= 0x7fffffffUL)
        stream.length = bytes;
    return stream;
}

static int read_block(void *context, void *file, MMU32 offset, void *data, size_t bytes)
{
    (void)context;
    return read_at(file, offset, data, bytes);
}

static int write_block(void *context, void *file, MMU32 offset, const void *data, size_t bytes)
{
    (void)context;
    return write_at(file, offset, data, bytes);
}

static ArchiveFile *open_file(const char *path, const char *mode)
{
    ArchiveFile *file = SDL_malloc(sizeof(*file));
    if (!file)
        return NULL;
    file->stream = SDL_IOFromFile(path, mode);
    if (!file->stream)
    {
        SDL_free(file);
        return NULL;
    }
    file->references = 1;
    return file;
}

MMPrivateArchive *marcus_archive_open(const char *path, int mode, const MMPrivateArchiveProfile *profile)
{
    static const MMChunkOwnerFileIO owner_io = {NULL, create_temp, read_stream, size, extend, NULL,
                                                write_at, release, NULL, NULL, NULL, NULL, NULL};
    static const MMBlockFileServices blocks = {NULL, retain, release_block, create_temp, read_block, write_block};
    const char *open_mode = mode == MM_PRIVATE_ARCHIVE_READ ? "rb" : mode == MM_PRIVATE_ARCHIVE_EDIT ? "r+b" : "w+b";
    ArchiveFile *file = open_file(path, open_mode);
    return file ? mm_private_archive_open(file, mode, &owner_io, &blocks, profile) : NULL;
}

int marcus_archive_input_open(MarcusArchiveInput *input, const char *path, const MMArchiveReaderOptions *options,
                               int expanded)
{
    const unsigned short order = 1;
    MMChunkPolicy policy = {0};
    MarcusArchiveInput next = {0};
    MMReadStream stream;
    int status;
    if (!path || !*path || !(next.file = open_file(path, "rb")))
        return MM_ARCHIVE_INVALID_PAYLOAD;
    stream = read_stream(next.file);
    policy.expanded_runtime = expanded;
    policy.native_big_endian = *(const unsigned char *)&order == 0;
    policy.max_name_bytes = MM_CHUNK_NAME_WIRE_BYTES;
    status = mm_archive_reader_open_stream(&stream, &policy, options, &next.reader);
    if (status != MM_ARCHIVE_READ_OK)
    {
        marcus_archive_input_close(&next);
        return status;
    }
    marcus_archive_input_close(input);
    *input = next;
    return MM_ARCHIVE_READ_OK;
}

void marcus_archive_input_close(MarcusArchiveInput *input)
{
    mm_archive_reader_close(input->reader);
    if (input->file)
        release(input->file);
    input->file = NULL;
    input->reader = NULL;
}

typedef struct TextArchives
{
    MMPrivateArchive *source, *destination;
} TextArchives;

static int copy_text_asset(void *p, MMU32 type, MMU32 number, MMU32 *copied)
{
    const TextArchives *archives = p;
    return mm_private_archive_copy(archives->source, archives->destination, type, number, 0, copied);
}

static int replace_text(MMPrivateArchive *file, MMU32 actor, const MMMovieActorTextEdit *edit,
                        const MarcusArchiveDrafts *drafts, int *detail)
{
    TextArchives archives;
    MMMovieArchiveEditor editor;
    MMActorAuthorHost host = {0};
    MMActorTextReplacement request;
    MMStringValue name = {0};
    unsigned short units[257];
    int status;
    *detail = 0;
    archives.source = drafts->open(drafts->context, edit->file);
    if (!archives.source)
        return MARCUS_ARCHIVE_TEXT_OPEN;
    archives.destination = file;
    editor = mm_private_archive_editor(file);
    name.buffer = units;
    name.unit = editor.text_unit;
    mm_string_from_ansi(&name, drafts->text, edit->name.legacy, strlen(edit->name.legacy));
    request.actor = actor;
    request.name = (const unsigned char *)units + name.unit;
    request.characters = name.unit == 1 ? *(const unsigned char *)units : units[0];
    request.retail_font = edit->retail_font;
    request.custom_material = edit->custom_material;
    host.destination = &editor;
    host.text = drafts->text;
    host.context = &archives;
    host.copy = copy_text_asset;
    status = mm_actor_archive_replace_text(&host, &request, detail);
    mm_private_archive_close(archives.source);
    return status;
}

typedef struct ActorAppearance
{
    MMPrivateArchive *file;
    const MMMovieActorTextEdit *text;
    const MMMovieActorTrackEdit *costume;
    const MarcusArchiveDrafts *drafts;
    MarcusArchiveActorResult *result;
} ActorAppearance;

static int apply_appearance(void *p, MMU32 actor)
{
    const ActorAppearance *appearance = p;
    const MarcusArchiveDrafts *drafts = appearance->drafts;
    MarcusArchiveActorResult *result = appearance->result;
    if (appearance->text)
    {
        result->text = replace_text(appearance->file, actor, appearance->text, drafts, &result->text_detail);
        if (result->text != MM_ACTOR_AUTHOR_OK)
            return 0;
    }
    if (appearance->costume)
    {
        MMPrivateArchive *track = drafts->open(drafts->context, appearance->costume->file);
        result->costume = track ? mm_actor_archive_import_events(appearance->file, actor, track)
                                : MM_ACTOR_EDIT_ARCHIVE_COPY;
        mm_private_archive_close(track);
    }
    return result->costume == MM_ACTOR_EDIT_ARCHIVE_OK;
}

int marcus_archive_place_edits(MMPrivateArchive *file, const MMMoviePoseJournal *poses,
                              const MMMovieTextJournal *texts, const MMMovieActorTrackJournal *costumes,
                              const MarcusArchiveDrafts *drafts, MarcusArchiveActorResult *out)
{
    const MMMovieArchiveEditor editor = mm_private_archive_editor(file);
    MMMoviePoseJournal merged;
    const MMMovieActorPoseEdit *rows;
    MMU32 i;
    int ready;
    memset(out, 0, sizeof(*out));
    mm_movie_pose_journal_init(&merged, NULL);
    ready = mm_movie_pose_journal_export(&merged, poses, texts, costumes);
    if (ready)
    {
        rows = mm_movie_pose_journal_data(&merged);
        for (i = 0; i < merged.count; ++i)
        {
            const MMMovieActorPoseEdit *row = rows + i;
            ActorAppearance appearance;
            MMActorArchivePose pose;
            appearance.file = file;
            appearance.text = mm_movie_text_journal_find(texts, row->scene, row->actor);
            appearance.costume = mm_movie_track_journal_find(costumes, row->scene, row->actor);
            appearance.drafts = drafts;
            appearance.result = out;
            pose.scene = row->scene;
            pose.actor = row->actor;
            pose.translation = row->offset;
            pose.rotation = row->shape.rotation;
            pose.scale = row->shape.scale;
            pose.context = &appearance;
            pose.appearance = apply_appearance;
            out->status = mm_actor_archive_place(&editor, &pose);
            if (out->status != MM_ACTOR_EDIT_ARCHIVE_OK)
                break;
        }
    }
    mm_movie_pose_journal_dispose(&merged);
    return ready;
}

static int apply_animation_text(void *p, MMPrivateArchive *file, MMU32 actor)
{
    ActorAppearance *appearance = p;
    appearance->file = file;
    return apply_appearance(appearance, actor);
}

int marcus_archive_animate(const char *path, const char *track, const MMPrivateArchiveProfile *profile,
                           const MMActorArchiveAnimation *animation, const MMMovieActorTextEdit *text,
                           const MarcusArchiveDrafts *drafts,
                           MMU32 creator, MarcusArchiveAnimationResult *out)
{
    MMPrivateArchive *candidate = marcus_archive_open(path, MM_PRIVATE_ARCHIVE_EDIT, profile);
    MMPrivateArchive *source = marcus_archive_open(track, MM_PRIVATE_ARCHIVE_READ, profile);
    MMActorArchiveAnimation request = *animation;
    ActorAppearance appearance = {0};
    int result = MARCUS_ARCHIVE_ANIMATION_OPEN;
    memset(out, 0, sizeof(*out));
    if (text)
    {
        appearance.text = text;
        appearance.drafts = drafts;
        appearance.result = &out->actor;
        request.context = &appearance;
        request.appearance = apply_animation_text;
    }
    if (candidate && source)
    {
        out->actor.status = mm_actor_archive_animate(candidate, source, &request, &out->scene);
        if (out->actor.status != MM_ACTOR_EDIT_ARCHIVE_OK || out->scene != MM_SCENE_ARCHIVE_EDIT_OK)
            result = MARCUS_ARCHIVE_ANIMATION_EDIT;
        else
            result = mm_private_archive_save(candidate, creator)
                ? MARCUS_ARCHIVE_ANIMATION_OK : MARCUS_ARCHIVE_ANIMATION_SAVE;
    }
    mm_private_archive_close(source);
    mm_private_archive_close(candidate);
    return result;
}
