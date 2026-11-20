/* Actor persistence converted from engine actor_data.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_data.h"
#include "mm/actor_persistence.h"
#include "mm/collections.h"
#include "mm/movie_data.h"
#include "mm/movie_archive_edit.h"
#include "mm/movie_sound.h"
#include "mm/scene_data.h"
#include "mm/template_create.h"
#include "mm/wide.h"
#include <stdlib.h>
#include <string.h>
const void *mm_actor_payload_tag(MMI32 kind, const void *payload)
{
    const unsigned char *bytes = (const unsigned char *)payload;
    MMI32 custom;
    if (kind == MM_ACTOR_COSTUME)
    {
        memcpy(&custom, bytes + offsetof(MMActorCostume, custom), sizeof(custom));
        return custom ? NULL : bytes + offsetof(MMActorCostume, tag);
    }
    if (kind == MM_ACTOR_SOUND)
        return bytes + offsetof(MMActorSound, tag);
    return NULL;
}

int mm_actor_template_link(const MMChunkAccess *access, MMU32 actor, MMI32 source, int require_target, MMU32 *number)
{
    MMU32 child, position;
    MMChunkEntry entry;
    int status;
    if (source)
        return MM_CHUNK_OK;
    status = mm_chunk_link_number(access, MM_ACTOR_CHUNK, actor, 0, MM_TMPL, &child);
    if (status == MM_CHUNK_OK && require_target)
    {
        status = mm_chunk_find(access, MM_TMPL, child, &position);
        if (status == MM_CHUNK_OK)
            status = mm_chunk_at(access, position, &entry);
    }
    if (status == MM_CHUNK_OK)
        *number = child;
    return status;
}

static void swap16(unsigned char *p)
{
    unsigned char t = p[0];
    p[0] = p[1];
    p[1] = t;
}
static void swap32(unsigned char *p)
{
    unsigned char t = p[0];
    p[0] = p[3];
    p[3] = t;
    t = p[1];
    p[1] = p[2];
    p[2] = t;
}
static void swap_words(void *data, MMU32 count)
{
    unsigned char *p = (unsigned char *)data;
    MMU32 i;
    for (i = 0; i < count; ++i)
        swap32(p + i * 4);
}
static MMU32 native_word(const void *p)
{
    MMU32 value;
    memcpy(&value, p, 4);
    return value;
}
static void put_word(void *p, MMU32 value)
{
    memcpy(p, &value, 4);
}
static MMI32 kind(const void *fixed, int order)
{
    unsigned char bytes[4];
    memcpy(bytes, fixed, 4);
    if (order == MM_ACTOR_OTHER)
        swap32(bytes);
    return mm_signed_word(native_word(bytes));
}
int mm_actor_read_header(const MMReadStream *s, void *header)
{
    unsigned char *p = (unsigned char *)header;
    unsigned short order;
    MMI32 last = 2147483647L;
    int old;
    if (s->length != 44 && s->length != 40)
        return 0;
    old = s->length == 40;
    if (!mm_read_exact(s, 0, p, s->length))
        return 0;
    if (old)
        memmove(p + 28, p + 24, 16);
    memcpy(&order, p, 2);
    if (order == 0x0100U)
    {
        swap16(p);
        swap16(p + 2);
        swap_words(p + 4, 10);
    }
    memcpy(&order, p, 2);
    if (order != 1)
        return 0;
    if (old)
        memcpy(p + 24, &last, 4);
    return 1;
}
int mm_actor_adjust_header_id(void *header, MMI32 delta)
{
    unsigned char *p = (unsigned char *)header;
    MMI32 id;
    memcpy(&id, p + 16, 4);
    if ((delta > 0 && id > 2147483647L - delta) || (delta < 0 && id < (-2147483647L - 1) - delta))
        return 0;
    id += delta;
    memcpy(p + 16, &id, 4);
    return 1;
}
int mm_actor_write_header(MMU32 number, const void *header, const MMActorHeaderIO *io)
{
    return io->put(io->context, number, header, 44);
}
static int read_header(void *out, const MMReadStream *stream)
{
    return mm_actor_read_header(stream, out);
}
int mm_actor_adjust_id(MMU32 number, MMI32 delta, const MMActorHeaderIO *io)
{
    MMActorHeader header;
    if (!io->read(io->context, number, read_header, &header) ||
        !mm_actor_adjust_header_id(&header, delta))
        return 0;
    return mm_actor_write_header(number, &header, io);
}
void *mm_actor_finish_route(void *route, int order, const MMActorRouteServices *s)
{
    MMU32 count;
    if (!route)
        return NULL;
    if (s->width(route) != 16 || (order != MM_ACTOR_NATIVE && order != MM_ACTOR_OTHER))
    {
        s->release(route);
        return NULL;
    }
    if (order == MM_ACTOR_OTHER)
    {
        count = s->count(route);
        if (count)
            swap_words(s->data(route), count * 4);
    }
    return route;
}

static int collection_status(int status)
{
    return status == MM_STORAGE_IO ? MM_MOVIE_IO : MM_MOVIE_BAD_FILE;
}

static int foreign_order(const MMCollectionHeader *header)
{
    const unsigned short one = 1;
    return header->big_endian != (*(const unsigned char *)&one == 0);
}

int mm_actor_read_route_stream(const MMReadStream *stream, const MMActorRouteReader *reader, void **out)
{
    MMCollectionHeader header;
    void *route, *rows = NULL;
    int status = mm_collection_header_read(stream, MM_COLLECTION_ARRAY, 0, &header);
    if (status != MM_STORAGE_OK)
        return collection_status(status);
    if (header.width != MM_ACTOR_ROUTE_BYTES || (size_t)header.first_bytes != header.first_bytes)
        return MM_MOVIE_BAD_FILE;
    route = reader->create(reader->context, header.count, &rows);
    if (!route)
        return MM_MOVIE_MEMORY;
    if (header.count && !mm_read_exact(stream, header.header_bytes, rows, (size_t)header.first_bytes))
    {
        reader->release(route);
        return MM_MOVIE_IO;
    }
    if (header.count && foreign_order(&header))
        swap_words(rows, header.count * 4);
    *out = route;
    return MM_MOVIE_OK;
}

static MMI32 payload_size(MMI32 type, int wire)
{
    switch (type)
    {
    case MM_ACTOR_ADD:
        return 20;
    case MM_ACTOR_ACTION:
        return 8;
    case MM_ACTOR_COSTUME:
        return wire ? 28 : (MMI32)sizeof(MMActorCostume);
    case MM_ACTOR_SOUND:
        return wire ? 44 : (MMI32)sizeof(MMActorSound);
    case MM_ACTOR_ROTATE_FORWARD:
    case MM_ACTOR_ROTATE_HERE:
        return 48;
    case MM_ACTOR_STRETCH:
    case MM_ACTOR_MOVE:
    case MM_ACTOR_TWEAK:
        return 12;
    case MM_ACTOR_SIZE:
    case MM_ACTOR_FREEZE:
    case MM_ACTOR_STEP:
        return 4;
    case MM_ACTOR_REMOVE:
        return 0;
    default:
        return -1;
    }
}
int mm_actor_valid_events(void *g, int wire, int order, const MMActorEventServices *s)
{
    MMU32 i;
    MMI32 bytes, type;
    if ((order != MM_ACTOR_NATIVE && order != MM_ACTOR_OTHER) || s->fixed_width(g) != 20)
        return 0;
    for (i = 0; i < s->count(g); ++i)
    {
        type = kind(s->fixed(g, i), order);
        bytes = payload_size(type, wire);
        if (bytes < 0 || s->size(g, i) != bytes)
            return 0;
    }
    return 1;
}
static void swap_event_data(MMI32 type, unsigned char *data)
{
    unsigned i;
    if (type == MM_ACTOR_ADD)
    {
        swap_words(data, 3);
        for (i = 0; i < 3; ++i)
            swap16(data + 12 + i * 2);
    }
    else if (type != MM_ACTOR_REMOVE)
        swap_words(data, (MMU32)payload_size(type, 1) / 4);
}
static void read_tag(MMTag *t, const unsigned char *p)
{
    t->sid = mm_signed_word(native_word(p));
    t->cache = NULL;
    t->type = native_word(p + 8);
    t->number = native_word(p + 12);
}
static void write_tag(unsigned char *p, const MMTag *t)
{
    put_word(p, (MMU32)t->sid);
    put_word(p + 4, 0);
    put_word(p + 8, t->type);
    put_word(p + 12, t->number);
}
static int insert_decoded_event(const MMActorEventServices *s, void *out, MMU32 row,
                                const void *fixed, const unsigned char *data, MMU32 bytes)
{
    MMActorCostume costume;
    MMActorSound sound;
    MMI32 type = kind(fixed, MM_ACTOR_NATIVE);
    if (type == MM_ACTOR_COSTUME)
    {
        memset(&costume, 0, sizeof(costume));
        memcpy(&costume.body_set, data, 4);
        memcpy(&costume.costume, data + 4, 4);
        memcpy(&costume.custom, data + 8, 4);
        read_tag(&costume.tag, data + 12);
        return s->insert(out, row, sizeof(costume), &costume, fixed);
    }
    if (type == MM_ACTOR_SOUND)
    {
        memset(&sound, 0, sizeof(sound));
        memcpy(&sound.loop, data, 4);
        memcpy(&sound.queue, data + 4, 4);
        memcpy(&sound.volume, data + 8, 4);
        memcpy(&sound.cel, data + 12, 4);
        memcpy(&sound.style, data + 16, 4);
        memcpy(&sound.no_sound, data + 20, 4);
        memcpy(&sound.child, data + 24, 4);
        read_tag(&sound.tag, data + 28);
        return s->insert(out, row, sizeof(sound), &sound, fixed);
    }
    return s->insert(out, row, bytes, data, fixed);
}

void *mm_actor_read_events(void *wire, int order, const MMActorEventServices *s)
{
    void *out = NULL, *fixed;
    unsigned char *data;
    MMU32 i, count;
    MMI32 type, bytes;
    if (!wire)
        return NULL;
    if (!mm_actor_valid_events(wire, 1, order, s))
        goto fail;
    if (order == MM_ACTOR_OTHER)
        for (i = 0; i < s->count(wire); ++i)
        {
            fixed = s->fixed(wire, i);
            swap_words(fixed, 5);
            type = kind(fixed, MM_ACTOR_NATIVE);
            if (type != MM_ACTOR_REMOVE)
            {
                data = (unsigned char *)s->data(wire, i);
                swap_event_data(type, data);
            }
        }
    out = s->new_group(s->context, 20);
    if (!out)
        goto fail;
    count = s->count(wire);
    for (i = 0; i < count; ++i)
    {
        fixed = s->fixed(wire, i);
        type = kind(fixed, MM_ACTOR_NATIVE);
        data = (unsigned char *)s->data(wire, i);
        bytes = type == MM_ACTOR_COSTUME || type == MM_ACTOR_SOUND ? 0 : s->size(wire, i);
        if (insert_decoded_event(s, out, i, fixed, data, (MMU32)bytes) <= 0)
            goto fail;
    }
    s->release(wire);
    return out;
fail:
    s->release(wire);
    if (out)
        s->release(out);
    return NULL;
}

int mm_actor_read_events_stream(const MMReadStream *stream, const MMActorEventServices *s, void **out)
{
    MMCollectionHeader header;
    unsigned char *records = NULL, fixed[MM_ACTOR_EVENT_BYTES], data[48];
    MMU32 row, offset, total;
    MMI32 bytes, type;
    void *group = NULL;
    int order, inserted, status = mm_collection_header_read(stream, MM_COLLECTION_GROUP, 0, &header);
    if (status != MM_STORAGE_OK)
        return collection_status(status);
    total = header.first_bytes + header.second_bytes;
    if (header.width != MM_ACTOR_EVENT_BYTES || (size_t)total != total)
        return MM_MOVIE_BAD_FILE;
    if (total)
    {
        records = (unsigned char *)malloc((size_t)total);
        if (!records)
            return MM_MOVIE_MEMORY;
        if (!mm_read_exact(stream, header.header_bytes, records, (size_t)total))
        {
            status = MM_MOVIE_IO;
            goto done;
        }
    }
    order = foreign_order(&header) ? MM_ACTOR_OTHER : MM_ACTOR_NATIVE;
    status = MM_MOVIE_BAD_FILE;
    for (row = 0; row < header.count; ++row)
    {
        if (!mm_collection_group_row(&header, records, row, &offset, &bytes))
            goto done;
        type = kind(records + offset, order);
        if (payload_size(type, 1) != bytes)
            goto done;
    }
    status = MM_MOVIE_MEMORY;
    group = s->new_group(s->context, header.width);
    if (!group)
        goto done;
    for (row = 0; row < header.count; ++row)
    {
        mm_collection_group_row(&header, records, row, &offset, &bytes);
        memcpy(fixed, records + offset, sizeof(fixed));
        memcpy(data, records + offset + sizeof(fixed), (size_t)bytes);
        if (order == MM_ACTOR_OTHER)
        {
            swap_words(fixed, 5);
            swap_event_data(kind(fixed, MM_ACTOR_NATIVE), data);
        }
        inserted = insert_decoded_event(s, group, row, fixed, data, (MMU32)bytes);
        if (inserted <= 0)
        {
            status = inserted < 0 ? MM_MOVIE_BAD_FILE : MM_MOVIE_MEMORY;
            goto done;
        }
    }
    *out = group;
    status = MM_MOVIE_OK;
done:
    free(records);
    if (status != MM_MOVIE_OK && group)
        s->release(group);
    return status;
}

static void *write_events(void *runtime, const MMActorEventServices *s, MMU32 actor,
                          const MMActorStoreServices *store)
{
    void *out, *fixed;
    const void *data;
    MMU32 i, count;
    MMI32 type;
    unsigned char bytes[44];
    MMActorCostume costume;
    MMActorSound sound;
    if (!mm_actor_valid_events(runtime, 0, MM_ACTOR_NATIVE, s))
        return NULL;
    out = s->new_group(s->context, 20);
    if (!out)
        return NULL;
    count = s->count(runtime);
    for (i = 0; i < count; ++i)
    {
        fixed = s->fixed(runtime, i);
        type = kind(fixed, MM_ACTOR_NATIVE);
        data = s->data(runtime, i);
        if (type == MM_ACTOR_COSTUME)
        {
            memcpy(&costume, data, sizeof(costume));
            if (store && !costume.custom && costume.tag.sid == 0 &&
                !mm_movie_archive_embed_tag(&store->materials, MM_ACTOR_CHUNK, actor, &costume.tag))
                goto fail;
            memcpy(bytes, &costume.body_set, 4);
            memcpy(bytes + 4, &costume.costume, 4);
            memcpy(bytes + 8, &costume.custom, 4);
            write_tag(bytes + 12, &costume.tag);
            if (s->insert(out, i, 28, bytes, fixed) <= 0)
                goto fail;
        }
        else if (type == MM_ACTOR_SOUND)
        {
            memcpy(&sound, data, sizeof(sound));
            memcpy(bytes, &sound.loop, 4);
            memcpy(bytes + 4, &sound.queue, 4);
            memcpy(bytes + 8, &sound.volume, 4);
            memcpy(bytes + 12, &sound.cel, 4);
            memcpy(bytes + 16, &sound.style, 4);
            memcpy(bytes + 20, &sound.no_sound, 4);
            memcpy(bytes + 24, &sound.child, 4);
            write_tag(bytes + 28, &sound.tag);
            if (s->insert(out, i, 44, bytes, fixed) <= 0)
                goto fail;
        }
        else if (s->insert(out, i, (MMU32)s->size(runtime, i), data, fixed) <= 0)
            goto fail;
    }
    return out;
fail:
    s->release(out);
    return NULL;
}

void *mm_actor_write_events(void *runtime, const MMActorEventServices *s)
{
    return write_events(runtime, s, 0, NULL);
}

static int store_template(MMU32 actor, const MMActorHeader *header, void *object, const MMActorStoreServices *s)
{
    const MMMovieEmbeddedArchive *archive = &s->materials;
    MMChunkAccess chunks = archive->chunks(archive->context);
    MMChunkChild link;
    MMU32 row, number;
    int created = 0;
    link.type = header->template_type;
    link.number = header->template_number;
    link.slot = 0;
    if (mm_chunk_link_number(&chunks, MM_ACTOR_CHUNK, actor, 0, link.type, &number) == MM_CHUNK_OK &&
        number == link.number)
        return 1;
    if (mm_chunk_find(&chunks, link.type, link.number, &row) != MM_CHUNK_OK)
    {
        if (!s->write_template(s->context, object, link.type, link.number))
            return 0;
        created = 1;
    }
    if (mm_chunk_adopt(&archive->edit, MM_ACTOR_CHUNK, actor, &link, 1) == MM_CHUNK_OK)
        return 1;
    if (created)
        mm_chunk_delete(&archive->edit, link.type, link.number);
    return 0;
}

static int store_sounds(MMU32 scene, MMActorEventTable *events, const MMMovieEmbeddedArchive *archive)
{
    MMActorEvent event;
    MMActorSound sound;
    MMChunkAccess chunks;
    MMChunkChild link;
    MMU32 i, count = events->rows.count, number;
    for (i = 0; i < count; ++i)
    {
        mm_actor_events_get_fixed(events, i, &event);
        if (event.aet != MM_ACTOR_SOUND)
            continue;
        mm_actor_events_get_payload(events, i, &sound);
        if (sound.tag.sid != 0)
            continue;
        chunks = archive->chunks(archive->context);
        /* Undo scenes may refer to sounds that still live in the main movie. */
        if (mm_chunk_link_number(&chunks, MM_SCENE_CHUNK, scene, sound.child, MM_SOUND_CHUNK, &number) == MM_CHUNK_OK ||
            mm_chunk_find(&chunks, MM_SOUND_CHUNK, sound.tag.number, &number) != MM_CHUNK_OK)
            continue;
        link.type = MM_SOUND_CHUNK;
        link.number = sound.tag.number;
        link.slot = sound.child;
        if (mm_chunk_adopt(&archive->edit, MM_SCENE_CHUNK, scene, &link, 1) != MM_CHUNK_OK)
            return 0;
    }
    return 1;
}

int mm_actor_store(MMU32 actor, MMU32 scene, const MMActorOwner *owner, const MMActorStoreServices *s)
{
    const MMActorRuntime *runtime = &owner->runtime;
    const MMActorTrack *track = &owner->track;
    MMActorHeader header = {0};
    MMActorEventFactory factory;
    MMActorEventServices events;
    MMActorEventTable *wire;
    MMStorageWriter writer;
    int ok;
    if (runtime->first != 2147483647L)
    {
        if (!s->refresh_lifetime(s->context))
            return 0;
    }
    if (runtime->first == 2147483647L && s->missing_first_frame)
        s->missing_first_frame(s->context);

    header.order = 1;
    header.system = (unsigned short)s->system;
    header.translation[0] = (MMU32)runtime->origin.dxr;
    header.translation[1] = (MMU32)runtime->origin.dyr;
    header.translation[2] = (MMU32)runtime->origin.dzr;
    header.actor_id = runtime->actor;
    header.first_frame = runtime->first;
    header.last_frame = runtime->last;
    header.source_id = runtime->template_tag.sid;
    header.template_type = runtime->template_tag.type;
    header.template_number = runtime->template_tag.number;
    if (!header.source_id && !store_template(actor, &header, runtime->template_object, s))
        return 0;
    if (!mm_actor_write_header(actor, &header, &s->header) ||
        !s->add_track(s->context, actor, MM_ACTOR_ROUTE_CHUNK, mm_actor_path_wire_size(track->route), &writer) ||
        !mm_actor_path_write(track->route, s->system, &writer))
        return 0;
    factory.allocator = &track->events->storage.allocator;
    factory.trash_unused = track->events->rows.trash_unused;
    events = mm_actor_events_services(&factory);
    wire = (MMActorEventTable *)write_events(track->events, &events, actor, s);
    if (!wire)
        return 0;
    ok = s->add_track(s->context, actor, MM_ACTOR_EVENTS_CHUNK, mm_actor_events_wire_size(wire), &writer) &&
         mm_actor_events_write(wire, s->system, 0, &writer);
    mm_actor_events_release(wire);
    return ok && store_sounds(scene, track->events, &s->materials);
}
