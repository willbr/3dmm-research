#include "mm/actor_archive.h"
#include "mm/actor_motion.h"
#include "mm/collections.h"
#include "mm/movie_sound.h"
#include "mm/template_create.h"
#include "mm/wide.h"
#include <string.h>

#define ACTOR_TYPE 0x41435452UL
#define FRAME_INVALID 2147483647L
#define FRAME_MIN (-2147483647L - 1L)

typedef struct SoundLink
{
    MMU32 source, copied, slot;
} SoundLink;

int mm_actor_archive_next_child(const MMChunkAccess *chunks, MMU32 parent_type, MMU32 parent,
                                MMU32 type, MMU32 *slot)
{
    MMChunkChild child;
    MMU32 position, i, next;
    next = 0;
    if (mm_chunk_find(chunks, parent_type, parent, &position) == MM_CHUNK_OK)
        for (i = 0; mm_chunk_child_at(chunks, position, i, &child) == MM_CHUNK_OK; ++i)
            if (child.type == type)
            {
                if (child.slot >= 0x7fffffffUL)
                    return 0;
                if (child.slot >= next)
                    next = child.slot + 1;
            }
    *slot = next;
    return 1;
}

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
    if (!mm_actor_archive_next_child(&chunks, parent_type, parent, type, slot))
        return MM_ACTOR_ARCHIVE_SLOT;
    if (!host->adopt(host->context, parent_type, parent, type, *copied, *slot))
        return parent_type == MM_SOUND_SCENE ? MM_ACTOR_ARCHIVE_ADOPT_SOUND : MM_ACTOR_ARCHIVE_ADOPT_ASSET;
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
    mm_chunk_link_number(&host->source_chunks, MM_SOUND_SCENE, request->source_scene,
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
        result = embed(host, MM_SOUND_SCENE, request->destination_scene, MM_SOUND_CHUNK,
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
