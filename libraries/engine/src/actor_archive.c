#include "mm/actor_archive.h"
#include "mm/actor_motion.h"
#include "mm/collections.h"
#include "mm/movie_sound.h"
#include "mm/rollcall_storage.h"
#include "mm/scene_data.h"
#include "mm/template_create.h"
#include "mm/wide.h"
#include <string.h>

#define ACTOR_TYPE 0x41435452UL
#define SCENE_TYPE 0x5343454eUL
#define START_TYPE 0x47475354UL
#define THUMB_TYPE 0x5448554dUL
#define MOVIE_TYPE 0x4d564945UL
#define ROLLCALL_TYPE 0x47535420UL
#define FRAME_INVALID 2147483647L
#define FRAME_MIN (-2147483647L - 1L)

typedef struct SoundLink
{
    MMU32 source, copied, slot;
} SoundLink;

static int shift_frame(MMI32 *frame, MMWide delta)
{
    MMWide shifted;
    if (*frame == FRAME_INVALID)
        return 1;
    shifted = mm_wide_add(mm_wide_i32(*frame), delta);
    if (mm_wide_compare(shifted, mm_wide_i32(FRAME_MIN)) < 0 ||
        mm_wide_compare(shifted, mm_wide_i32(FRAME_INVALID)) >= 0)
        return 0;
    *frame = mm_signed_word(shifted.low);
    return 1;
}

static int map_source(const MMActorArchiveHost *host, MMI32 *source)
{
    if (*source > 0 && !host->map_source(host->context, *source, source))
        return MM_ACTOR_ARCHIVE_SOURCE;
    return MM_ACTOR_ARCHIVE_OK;
}

static int embed(const MMActorArchiveHost *host, MMU32 parent_type, MMU32 parent, MMU32 type,
                 MMU32 source, MMU32 *copied, MMU32 *slot)
{
    MMChunkAccess chunks;
    if (!host->copy_asset(host->context, type, source, copied))
        return MM_ACTOR_ARCHIVE_COPY_ASSET;
    chunks = host->destination_chunks(host->context);
    if (!mm_movie_archive_next_child(&chunks, parent_type, parent, type, slot))
        return MM_ACTOR_ARCHIVE_SLOT;
    if (!host->adopt(host->context, parent_type, parent, type, *copied, *slot))
        return parent_type == SCENE_TYPE ? MM_ACTOR_ARCHIVE_ADOPT_SOUND : MM_ACTOR_ARCHIVE_ADOPT_ASSET;
    return MM_ACTOR_ARCHIVE_OK;
}

static int retarget(const MMActorArchiveHost *host, MMU32 owner_type, MMU32 owner, MMTag *tag)
{
    MMU32 slot;
    if (tag->sid == 0)
        return embed(host, owner_type, owner, tag->type, tag->number, &tag->number, &slot);
    return map_source(host, &tag->sid);
}

/* Only rewrite the source and chunk number, preserving the original byte order
 * and the rest of the text descriptor. */
static MMU32 text_word(const unsigned char *bytes, int big)
{
    MMU32 value;
    int i;
    value = 0;
    for (i = 0; i < 4; ++i)
        value = (value << 8) | bytes[big ? i : 3 - i];
    return value;
}

static void put_text_word(unsigned char *bytes, MMU32 value, int big)
{
    int i;
    for (i = 0; i < 4; ++i)
    {
        bytes[big ? 3 - i : i] = (unsigned char)(value & 255U);
        value >>= 8;
    }
}

static int retarget_template(const MMActorArchiveHost *host, MMU32 copied, MMActorHeader *header)
{
    MMChunkAccess chunks;
    MMU32 text;
    unsigned char bytes[MM_ACTOR_ARCHIVE_TEXT_BYTES];
    MMTag font;
    int big, result;
    if (header->source_id != 0)
        return map_source(host, &header->source_id);
    chunks = host->destination_chunks(host->context);
    if (mm_actor_template_link(&chunks, copied, 0, 0, &header->template_number) != MM_CHUNK_OK)
        return MM_ACTOR_ARCHIVE_TEMPLATE;
    if (mm_chunk_link_number(&chunks, MM_TMPL, header->template_number, 0, MM_TDT, &text) != MM_CHUNK_OK)
        return MM_ACTOR_ARCHIVE_OK;
    if (!host->read_text(host->context, text, bytes))
        return MM_ACTOR_ARCHIVE_READ_TEXT;
    big = bytes[0] == 0 && bytes[1] == 1;
    if (!big && (bytes[0] != 1 || bytes[1] != 0))
        return MM_ACTOR_ARCHIVE_TEXT_ORDER;
    font.sid = mm_signed_word(text_word(bytes + 8, big));
    font.cache = NULL;
    font.type = text_word(bytes + 16, big);
    font.number = text_word(bytes + 20, big);
    result = retarget(host, MM_TMPL, header->template_number, &font);
    if (result != MM_ACTOR_ARCHIVE_OK)
        return result;
    put_text_word(bytes + 8, (MMU32)font.sid, big);
    put_text_word(bytes + 20, font.number, big);
    if (!host->write_text(host->context, text, bytes))
        return MM_ACTOR_ARCHIVE_WRITE_TEXT;
    return MM_ACTOR_ARCHIVE_OK;
}

static int retarget_sound(const MMActorArchiveRequest *request, const MMActorArchiveHost *host,
                          MMArray *sounds, MMActorSound *sound)
{
    SoundLink *links, link;
    MMU32 first, end, middle;
    int result;
    if (sound->tag.sid != 0)
        return map_source(host, &sound->tag.sid);
    link.source = sound->tag.number;
    mm_chunk_link_number(&host->source_chunks, SCENE_TYPE, request->source_scene,
                         sound->child, MM_SOUND_CHUNK, &link.source);

    links = (SoundLink *)sounds->storage->buffers[0].data;
    first = 0;
    end = sounds->count;
    while (first < end)
    {
        middle = first + (end - first) / 2;
        if (links[middle].source < link.source)
            first = middle + 1;
        else
            end = middle;
    }
    if (first < sounds->count && links[first].source == link.source)
        link = links[first];
    else
    {
        result = embed(host, SCENE_TYPE, request->destination_scene, MM_SOUND_CHUNK,
                       link.source, &link.copied, &link.slot);
        if (result != MM_ACTOR_ARCHIVE_OK)
            return result;
        if (mm_array_insert(sounds, first, &link, 1) != MM_STORAGE_OK)
            return MM_ACTOR_ARCHIVE_MEMORY;
    }
    sound->tag.number = link.copied;
    sound->child = link.slot;
    return MM_ACTOR_ARCHIVE_OK;
}

static int retarget_events(const MMActorArchiveRequest *request, const MMActorArchiveHost *host,
                           MMU32 copied, MMWide delta, void *events, MMArray *sounds)
{
    const MMActorEventServices *rows;
    MMU32 i, count;
    MMActorEvent event;
    MMActorCostume costume;
    MMActorSound sound;
    void *fixed;
    int result;
    rows = &host->events;
    count = rows->count(events);
    for (i = 0; i < count; ++i)
    {
        fixed = rows->fixed(events, i);
        memcpy(&event, fixed, sizeof(event));
        if (!shift_frame(&event.nfrm, delta))
            return MM_ACTOR_ARCHIVE_FRAME;
        memcpy(fixed, &event, sizeof(event));
        if (event.aet == MM_ACTOR_COSTUME)
        {
            memcpy(&costume, rows->data(events, i), sizeof(costume));
            if (costume.custom)
                continue;
            result = retarget(host, ACTOR_TYPE, copied, &costume.tag);
            if (result != MM_ACTOR_ARCHIVE_OK)
                return result;
            memcpy(rows->data(events, i), &costume, sizeof(costume));
        }
        else if (event.aet == MM_ACTOR_SOUND)
        {
            memcpy(&sound, rows->data(events, i), sizeof(sound));
            if (sound.no_sound)
                continue;
            result = retarget_sound(request, host, sounds, &sound);
            if (result != MM_ACTOR_ARCHIVE_OK)
                return result;
            memcpy(rows->data(events, i), &sound, sizeof(sound));
        }
    }
    return MM_ACTOR_ARCHIVE_OK;
}

int mm_actor_archive_transfer(const MMActorArchiveRequest *request, const MMActorArchiveHost *host,
                              MMU32 *actor, MMActorHeader *out)
{
    MMActorHeader header;
    MMU32 copied;
    MMWide delta;
    void *events;
    MMCollectionStorage storage;
    MMArray sounds;
    int result;
    if (!host->clone_actor(host->context, request->actor, &header, &copied))
        return MM_ACTOR_ARCHIVE_CLONE;
    delta = mm_wide_subtract(mm_wide_i32(request->destination_frame), mm_wide_i32(request->source_frame));
    if (!shift_frame(&header.first_frame, delta) || !shift_frame(&header.last_frame, delta))
        return MM_ACTOR_ARCHIVE_FRAME;
    header.actor_id = request->actor_id;
    result = retarget_template(host, copied, &header);
    if (result != MM_ACTOR_ARCHIVE_OK)
        return result;
    if (!host->write_header(host->context, copied, &header))
        return MM_ACTOR_ARCHIVE_WRITE_HEADER;
    events = host->read_events(host->context, request->actor);
    if (!events)
        return MM_ACTOR_ARCHIVE_READ_EVENTS;
    mm_storage_init(&storage, NULL);
    storage.min_grow[0] = sizeof(SoundLink) * 16;
    sounds.storage = &storage;
    sounds.width = sizeof(SoundLink);
    sounds.count = 0;
    sounds.free_count = -1;
    sounds.allocated = 0;
    sounds.trash_unused = 0;
    result = retarget_events(request, host, copied, delta, events, &sounds);
    mm_storage_dispose(&storage);
    if (result != MM_ACTOR_ARCHIVE_OK)
        return result;
    if (!host->write_events(host->context, copied, events))
        return MM_ACTOR_ARCHIVE_WRITE_EVENTS;
    *actor = copied;
    *out = header;
    return MM_ACTOR_ARCHIVE_OK;
}

typedef struct StartWrite
{
    const MMMovieArchiveEditor *editor;
    MMU32 number;
} StartWrite;

static int write_start(void *context, const unsigned char *bytes, MMU32 count)
{
    StartWrite *start = (StartWrite *)context;
    const MMMovieArchiveEditor *editor = start->editor;
    MMStorageWriter writer;
    return editor->add(editor->context, START_TYPE, count, &start->number, &writer) &&
           writer.write_at(writer.context, 0, bytes, count);
}

static int register_scene(const MMMovieArchiveEditor *editor, const MMActorArchivePlacement *placement)
{
    MMChunkAccess chunks;
    MMReadStream stream;
    MMU32 scene, slot, previous, thumb;
    StartWrite start;
    int result;
    scene = placement->scene;
    chunks = editor->chunks(editor->context);
    if (!mm_movie_archive_next_child(&chunks, SCENE_TYPE, scene, ACTOR_TYPE, &slot))
        return MM_ACTOR_REGISTER_SLOT;
    if (mm_chunk_link_number(&chunks, SCENE_TYPE, scene, 1, START_TYPE, &previous) != MM_CHUNK_OK ||
        !editor->read(editor->context, START_TYPE, previous, &stream))
        return MM_ACTOR_REGISTER_SCENE_READ;
    start.editor = editor;
    result = mm_scene_append_start_actor(&stream, editor->system, placement->first_frame, slot, write_start, &start);
    if (result == MM_MOVIE_BAD_FILE)
        return MM_ACTOR_REGISTER_SCENE_DATA;
    if (result == MM_MOVIE_MEMORY)
        return MM_ACTOR_REGISTER_MEMORY;
    if (result != MM_MOVIE_OK ||
        !editor->adopt(editor->context, SCENE_TYPE, scene, ACTOR_TYPE, placement->actor, slot))
        return MM_ACTOR_REGISTER_SCENE_WRITE;
    editor->unlink(editor->context, SCENE_TYPE, scene, START_TYPE, previous, 1);
    if (!editor->adopt(editor->context, SCENE_TYPE, scene, START_TYPE, start.number, 1))
        return MM_ACTOR_REGISTER_SCENE_LINK;
    if (placement->invalidate_thumbnail)
    {
        chunks = editor->chunks(editor->context);
        if (mm_chunk_link_number(&chunks, SCENE_TYPE, scene, 0, THUMB_TYPE, &thumb) == MM_CHUNK_OK)
            editor->unlink(editor->context, SCENE_TYPE, scene, THUMB_TYPE, thumb, 0);
    }
    return MM_ACTOR_REGISTER_OK;
}

static int read_rollcall(const MMMovieArchiveEditor *editor, MMU32 previous, const MMActorArchiveName *name,
                         MMRollCallTable *table, int *replaced)
{
    MMRollCallTable input;
    MMReadStream stream;
    MMMemoryStream row;
    MMMovieActor actor;
    unsigned char bytes[MM_MOVIE_ACTOR_BYTES];
    MMU32 i, characters;
    const void *text;
    int big, result;
    if (!editor->read(editor->context, ROLLCALL_TYPE, previous, &stream))
        return MM_ACTOR_REGISTER_ROLL_READ;
    if (mm_rollcall_table_init(&input, 0, editor->text_unit, editor->system, 0, NULL) != MM_STORAGE_OK)
        return MM_ACTOR_REGISTER_MEMORY;
    result = MM_ACTOR_REGISTER_ROLL_DATA;
    if (mm_rollcall_table_read(&input, &stream, editor->convert_text, editor->context, &big) != MM_STORAGE_OK ||
        input.stride != MM_MOVIE_ACTOR_BYTES + 4)
        goto done;
    for (i = 0; i < input.count; ++i)
    {
        if (!mm_rollcall_table_name(&input, i, &text, &characters) ||
            !mm_rollcall_table_extra(&input, i, bytes) ||
            !mm_memory_stream_init(&row, bytes, sizeof(bytes)) ||
            mm_movie_read_actor(&row.stream, big, &actor, NULL) != MM_MOVIE_OK)
            goto done;
        if (name->replace_existing && actor.actor_id == name->actor.actor_id)
        {
            MMI32 references = actor.reference_count;
            actor = name->actor;
            actor.reference_count = references;
            text = name->text;
            characters = name->characters;
            *replaced = 1;
        }
        mm_movie_write_actor(&actor, bytes, editor->big_endian);
        if (!mm_rollcall_table_add(table, text, characters, bytes))
        {
            result = MM_ACTOR_REGISTER_MEMORY;
            goto done;
        }
    }
    result = MM_ACTOR_REGISTER_OK;
done:
    mm_rollcall_table_dispose(&input);
    return result;
}

static int register_name(const MMMovieArchiveEditor *editor, MMU32 movie, const MMActorArchiveName *name)
{
    MMChunkAccess chunks;
    MMRollCallTable table;
    MMStorageWriter writer;
    MMU32 previous, number, size;
    unsigned char bytes[MM_MOVIE_ACTOR_BYTES];
    int had_roll, replaced, result;
    chunks = editor->chunks(editor->context);
    had_roll = mm_chunk_link_number(&chunks, MOVIE_TYPE, movie, 0, ROLLCALL_TYPE, &previous) == MM_CHUNK_OK;
    if (mm_rollcall_table_init(&table, MM_MOVIE_ACTOR_BYTES, editor->text_unit, editor->system, 0, NULL) !=
        MM_STORAGE_OK)
        return MM_ACTOR_REGISTER_MEMORY;
    replaced = 0;
    result = had_roll ? read_rollcall(editor, previous, name, &table, &replaced) : MM_ACTOR_REGISTER_OK;
    if (result != MM_ACTOR_REGISTER_OK)
        goto done;
    if (!replaced)
    {
        mm_movie_write_actor(&name->actor, bytes, editor->big_endian);
        if (!mm_rollcall_table_add(&table, name->text, name->characters, bytes))
        {
            result = MM_ACTOR_REGISTER_MEMORY;
            goto done;
        }
    }
    size = mm_rollcall_table_wire_size(&table);
    if (had_roll)
        editor->unlink(editor->context, MOVIE_TYPE, movie, ROLLCALL_TYPE, previous, 0);
    if (!size || !editor->add(editor->context, ROLLCALL_TYPE, size, &number, &writer) ||
        mm_rollcall_table_write(&table, &writer, editor->big_endian) != MM_STORAGE_OK ||
        !editor->adopt(editor->context, MOVIE_TYPE, movie, ROLLCALL_TYPE, number, 0))
        result = MM_ACTOR_REGISTER_ROLL_WRITE;
done:
    mm_rollcall_table_dispose(&table);
    return result;
}

int mm_actor_archive_register(const MMMovieArchiveEditor *editor, MMU32 movie,
                              const MMActorArchivePlacement *placement, const MMActorArchiveName *name)
{
    int result;
    if (placement)
    {
        result = register_scene(editor, placement);
        if (result != MM_ACTOR_REGISTER_OK)
            return result;
    }
    return register_name(editor, movie, name);
}
