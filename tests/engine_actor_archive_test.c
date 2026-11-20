#include "mm/actor_archive.h"
#include "mm/actor_effect.h"
#include "mm/actor_motion.h"
#include "mm/movie_sound.h"
#include "mm/movie_sound_archive.h"
#include "mm/rollcall_storage.h"
#include "mm/scene_reframe.h"
#include "mm/scene_data.h"
#include "mm/template_create.h"
#include "mm/text.h"
#include "chunk_seed_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value) do { if (!(value)) { fprintf(stderr, "line %d: %s\n", __LINE__, #value); exit(1); } } while (0)
#define ACTOR 0x41435452UL
#define FONT 0x464f4e54UL
#define MATERIAL 0x4d54524cUL
#define MOVIE 0x4d564945UL
#define ROLLCALL 0x47535420UL
#define INVALID_FRAME 2147483647L
#define ROUTE 0x50415448UL
#define THUMB 0x5448554dUL
#define EVENTS 0x47474145UL
#define TRANSFER_ROWS 8
#define TRANSFER_DATA_BYTES (6 * (MM_ACTOR_EVENT_BYTES + 44) + 2 * (MM_ACTOR_EVENT_BYTES + 28))
#define TRANSFER_WIRE_BYTES (20 + TRANSFER_DATA_BYTES + TRANSFER_ROWS * 8)

typedef struct Graph
{
    MMChunkEntry entries[3];
    MMChunkChild children[3][4];
} Graph;
typedef union Payload
{
    MMActorSound sound;
    MMActorCostume costume;
} Payload;
typedef struct Fixture
{
    Graph source;
    ChunkIndexFixture destination, embedded;
    MMActorHeader input, written;
    MMActorEvent events[TRANSFER_ROWS];
    Payload payloads[TRANSFER_ROWS];
    MMMemoryStream source_stream, destination_stream;
    unsigned char source_events[TRANSFER_WIRE_BYTES], destination_events[TRANSFER_WIRE_BYTES];
    unsigned char descriptor[MM_ACTOR_ARCHIVE_TEXT_BYTES];
    MMU32 copies, writes;
    int replacement_adopted, replacement_discarded, old_unlinked;
    int sound_unlinks, sound_scene_links, sound_paths, sound_tracks;
    int big_endian, fail_write, fail_link;
} Fixture;

static int entry(void *context, MMU32 i, MMChunkEntry *out)
{
    *out = ((Graph *)context)->entries[i];
    return 1;
}
static int child(void *context, MMU32 parent, MMU32 i, MMChunkChild *out)
{
    *out = ((Graph *)context)->children[parent][i];
    return 1;
}
static void append_child(Graph *graph, MMU32 parent, MMU32 type, MMU32 number, MMU32 slot)
{
    MMChunkChild *link;
    CHECK(graph->entries[parent].child_count < 4);
    link = &graph->children[parent][graph->entries[parent].child_count++];
    link->type = type;
    link->number = number;
    link->slot = slot;
}
static MMChunkAccess source(void *context)
{
    MMChunkAccess chunks = {NULL, 2, entry, child};
    chunks.context = &((Fixture *)context)->source;
    return chunks;
}
static MMChunkAccess destination(void *context)
{
    return fixture_chunk_access(&((Fixture *)context)->destination);
}
static int clone_actor(void *context, MMU32 actor, MMU32 *copied)
{
    CHECK(context && actor == 1);
    *copied = 2;
    return 1;
}
static int copy_asset(void *context, MMU32 type, MMU32 number, MMU32 *copied)
{
    Fixture *f = (Fixture *)context;
    CHECK((type == FONT && number == 9) || (type == MATERIAL && number == 8) ||
          (type == MM_SOUND_CHUNK && (number == 23 || number == 10)));
    ++f->copies;
    *copied = number + 100;
    return 1;
}
static int copy_transfer_asset(void *context, MMU32 type, MMU32 number, MMU32 *copied)
{
    Fixture *f = context;
    if (!copy_asset(f, type, number, copied))
        return 0;
    fixture_chunk_add(&f->destination, type, *copied);
    return 1;
}
static int transfer_adopt(void *context, MMU32 parent, const MMChunkChild *child)
{
    (void)context;
    CHECK((parent == 4 && child->type == FONT && child->slot == 1) ||
          (parent == 2 && child->type == MATERIAL && child->slot == 2) ||
          (parent == 3 && child->type == MM_SOUND_CHUNK && (child->slot == 5 || child->slot == 6)));
    return 1;
}
static int map_source(void *context, MMI32 source, MMI32 *mapped)
{
    CHECK(context && source == 3);
    *mapped = 7;
    return 1;
}
static void encode_transfer_events(Fixture *f)
{
    const unsigned short one = 1;
    MMCollectionHeader header = {0};
    unsigned char *data, *locations, *row;
    MMU32 offset, i, prefix, size, word;
    const MMTag *tag;
    header.kind = MM_COLLECTION_GROUP;
    header.width = MM_ACTOR_EVENT_BYTES;
    header.count = TRANSFER_ROWS;
    header.bytes = TRANSFER_DATA_BYTES;
    header.free_count = -1;
    header.system = MM_TEXT_WINDOWS;
    header.big_endian = f->big_endian;
    CHECK(mm_collection_header_write(&header, f->source_events, sizeof(f->source_events)) == MM_STORAGE_OK);
    data = f->source_events + 20;
    locations = data + TRANSFER_DATA_BYTES;
    offset = 0;
    for (i = 0; i < TRANSFER_ROWS; ++i)
    {
        row = data + offset;
        prefix = i < 6 ? 28 : 12;
        tag = i < 6 ? &f->payloads[i].sound.tag : &f->payloads[i].costume.tag;
        size = MM_ACTOR_EVENT_BYTES + prefix + 16;
        memcpy(row, &f->events[i], MM_ACTOR_EVENT_BYTES);
        row += MM_ACTOR_EVENT_BYTES;
        memcpy(row, &f->payloads[i], prefix);
        memcpy(row + prefix, &tag->sid, 4);
        memset(row + prefix + 4, 0, 4);
        memcpy(row + prefix + 8, &tag->type, 4);
        memcpy(row + prefix + 12, &tag->number, 4);
        memcpy(locations + i * 8, &offset, 4);
        memcpy(locations + i * 8 + 4, &size, 4);
        offset += size;
    }
    if (f->big_endian != (*(const unsigned char *)&one == 0))
        for (i = 0; i < TRANSFER_WIRE_BYTES - 20; i += 4)
        {
            memcpy(&word, data + i, 4);
            word = (word << 24) | ((word << 8) & 0x00ff0000UL) |
                   ((word >> 8) & 0x0000ff00UL) | (word >> 24);
            memcpy(data + i, &word, 4);
        }
}
static int source_read(void *context, MMU32 type, MMU32 number, MMReadStream *stream)
{
    Fixture *f = (Fixture *)context;
    if (type == ACTOR)
    {
        CHECK(number == 1 && mm_memory_stream_init(&f->source_stream, &f->input, sizeof(f->input)));
    }
    else
    {
        CHECK(type == EVENTS && number == 10);
        encode_transfer_events(f);
        CHECK(mm_memory_stream_init(&f->source_stream, f->source_events, sizeof(f->source_events)));
    }
    *stream = f->source_stream.stream;
    return 1;
}
static int destination_read(void *context, MMU32 type, MMU32 number, MMReadStream *stream)
{
    Fixture *f = (Fixture *)context;
    CHECK(type == MM_TDT && number == 5);
    CHECK(mm_memory_stream_init(&f->destination_stream, f->descriptor, sizeof(f->descriptor)));
    *stream = f->destination_stream.stream;
    return 1;
}
static int put(void *context, MMU32 type, MMU32 number, const void *bytes, MMU32 size)
{
    Fixture *f = (Fixture *)context;
    if (type == ACTOR)
    {
        CHECK(number == 2 && size == sizeof(f->written));
        memcpy(&f->written, bytes, size);
    }
    else
    {
        CHECK(type == MM_TDT && number == 5 && size == sizeof(f->descriptor));
        memcpy(f->descriptor, bytes, size);
    }
    return 1;
}
static int write_event_bytes(void *context, MMU32 offset, const void *data, size_t bytes)
{
    Fixture *f = (Fixture *)context;
    CHECK(offset <= sizeof(f->destination_events) && bytes <= sizeof(f->destination_events) - offset);
    if (f->fail_write && offset)
        return 0;
    memcpy(f->destination_events + offset, data, bytes);
    return 1;
}
static int replace_events(void *context, MMU32 type, MMU32 number, MMU32 size, MMStorageWriter *writer)
{
    Fixture *f = (Fixture *)context;
    CHECK(type == EVENTS && number == 20 && size == sizeof(f->destination_events));
    ++f->writes;
    writer->context = f;
    writer->length = size;
    writer->write_at = write_event_bytes;
    return 1;
}
static void read_output_events(Fixture *f)
{
    MMMemoryStream output;
    MMCollectionHeader header;
    const unsigned char *records, *payload;
    MMU32 i, offset;
    MMI32 size;
    CHECK(mm_memory_stream_init(&output, f->destination_events, sizeof(f->destination_events)));
    CHECK(mm_collection_header_read(&output.stream, MM_COLLECTION_GROUP, 0, &header) == MM_STORAGE_OK);
    CHECK(header.width == MM_ACTOR_EVENT_BYTES && header.count == TRANSFER_ROWS);
    records = f->destination_events + header.header_bytes;
    for (i = 0; i < TRANSFER_ROWS; ++i)
    {
        CHECK(mm_collection_group_row(&header, records, i, &offset, &size));
        memcpy(&f->events[i], records + offset, MM_ACTOR_EVENT_BYTES);
        payload = records + offset + MM_ACTOR_EVENT_BYTES;
        if (i < 6)
        {
            CHECK(f->events[i].aet == MM_ACTOR_SOUND && size == 44);
            memcpy(&f->payloads[i].sound.loop, payload, 4);
            memcpy(&f->payloads[i].sound.queue, payload + 4, 4);
            memcpy(&f->payloads[i].sound.volume, payload + 8, 4);
            memcpy(&f->payloads[i].sound.child, payload + 24, 4);
            memcpy(&f->payloads[i].sound.tag.sid, payload + 28, 4);
            memcpy(&f->payloads[i].sound.tag.number, payload + 40, 4);
        }
        else
        {
            CHECK(f->events[i].aet == MM_ACTOR_COSTUME && size == 28);
            memcpy(&f->payloads[i].costume.tag.number, payload + 24, 4);
        }
    }
}

static void init(Fixture *f, int big)
{
    MMU32 i;
    memset(f, 0, sizeof(*f));
    f->big_endian = big;
    f->source.entries[0].type = ACTOR;
    f->source.entries[0].number = 1;
    append_child(&f->source, 0, EVENTS, 10, 0);
    f->source.entries[1].type = MM_SOUND_SCENE;
    f->source.entries[1].number = 1;
    append_child(&f->source, 1, ACTOR, 1, 0);
    append_child(&f->source, 1, MM_SOUND_CHUNK, 23, 2);
    fixture_chunk_seed_link(&f->destination, ACTOR, 2, EVENTS, 20, 0);
    fixture_chunk_seed_link(&f->destination, ACTOR, 2, MM_TMPL, 4, 0);
    fixture_chunk_seed_link(&f->destination, ACTOR, 2, MATERIAL, 7, 1);
    fixture_chunk_seed_link(&f->destination, MM_SOUND_SCENE, 3, MM_SOUND_CHUNK, 555, 4);
    fixture_chunk_seed_link(&f->destination, MM_TMPL, 4, FONT, 6, 0);
    fixture_chunk_seed_link(&f->destination, MM_TMPL, 4, MM_TDT, 5, 0);
    f->destination.before_insert = transfer_adopt;
    f->input.order = 1;
    f->input.actor_id = 6;
    f->input.first_frame = 9;
    f->input.last_frame = INVALID_FRAME;
    f->input.template_number = 99;
    f->descriptor[big ? 1 : 0] = 1;
    f->descriptor[3] = 0x61;
    f->descriptor[7] = 0x62;
    f->descriptor[12] = 0x63;
    for (i = 0; i < 4; ++i)
        f->descriptor[big ? 19 - i : 16 + i] = (unsigned char)(FONT >> (i * 8));
    f->descriptor[big ? 23 : 20] = 9;
    for (i = 0; i < TRANSFER_ROWS; ++i)
    {
        f->events[i].nfrm = 10 + (MMI32)i;
        f->events[i].rtel.irpt = 15;
        if (i < 6)
        {
            f->events[i].aet = MM_ACTOR_SOUND;
            f->payloads[i].sound.tag.type = MM_SOUND_CHUNK;
        }
        else
        {
            f->events[i].aet = MM_ACTOR_COSTUME;
            f->payloads[i].costume.tag.type = MATERIAL;
        }
    }
    f->payloads[0].sound.child = 2;
    f->payloads[0].sound.tag.number = 777;
    f->payloads[1].sound.tag.number = 23;
    f->payloads[2].sound.tag.number = 10;
    f->payloads[3].sound.tag.sid = 3;
    f->payloads[3].sound.child = 8;
    f->payloads[4].sound.tag.sid = -1;
    f->payloads[5].sound.no_sound = 1;
    f->payloads[6].costume.tag.number = 8;
    f->payloads[7].costume.custom = 1;
    f->events[5].nfrm = INVALID_FRAME;
}

typedef struct RollEdit
{
    ChunkIndexFixture graph;
    MMMemoryStream source;
    unsigned char output[128];
    MMU32 size;
    int step;
} RollEdit;

static MMChunkAccess movie_chunks(void *context)
{
    return fixture_chunk_access(&((RollEdit *)context)->graph);
}
static int open_roll(void *context, MMU32 type, MMU32 number, MMReadStream *stream)
{
    RollEdit *edit = (RollEdit *)context;
    CHECK(type == ROLLCALL && number == 8 && edit->step == 0);
    *stream = edit->source.stream;
    return 1;
}
static int write_roll(void *context, MMU32 offset, const void *data, size_t bytes)
{
    RollEdit *edit = (RollEdit *)context;
    CHECK(edit->step == 2 && offset <= edit->size && bytes <= edit->size - offset);
    memcpy(edit->output + offset, data, bytes);
    return 1;
}
static int add_roll(void *context, MMU32 type, MMU32 size, MMU32 *number, MMStorageWriter *writer)
{
    RollEdit *edit = (RollEdit *)context;
    CHECK(type == ROLLCALL && size <= sizeof(edit->output) && edit->step == 1);
    ++edit->step;
    edit->size = size;
    *number = 9;
    fixture_chunk_add(&edit->graph, type, *number);
    writer->context = edit;
    writer->length = size;
    writer->write_at = write_roll;
    return 1;
}
static int adopt_roll(void *context, MMU32 parent, const MMChunkChild *child)
{
    RollEdit *edit = (RollEdit *)context;
    CHECK(parent == 1 && child->type == ROLLCALL && child->number == 9 && child->slot == 0);
    CHECK(edit->step == 2);
    ++edit->step;
    return 1;
}
static void unlink_roll(void *context, MMU32 parent, const MMChunkChild *child)
{
    RollEdit *edit = (RollEdit *)context;
    CHECK(parent == 1 && child->type == ROLLCALL && child->number == 8 && child->slot == 0);
    CHECK(edit->step == 0);
    ++edit->step;
}

static void rename_rollcall(void)
{
    RollEdit edit;
    MMMovieArchiveEditor editor = {NULL, 1, MM_TEXT_WINDOWS, 0, NULL, movie_chunks,
                                   open_roll, add_roll, NULL, NULL, NULL, NULL, {0}};
    MMActorArchiveName name = {{4, 1, 0, 2, MM_TMPL, 19}, "edited", 6, 1};
    MMMovieActor original = {4, 3, 5, 0, MM_TMPL, 1}, other = {6, 2, 5, 1, MM_TMPL, 2}, actor;
    MMCollectionHeader header = {0};
    MMRollCallTable table;
    MMMemoryStream output, row;
    unsigned char input[93] = {0}, bytes[MM_MOVIE_ACTOR_BYTES];
    const void *text;
    MMU32 characters;
    int big;

    memset(&edit, 0, sizeof(edit));
    editor.context = &edit;
    editor.edit = fixture_chunk_edit(&edit.graph);
    fixture_chunk_seed_link(&edit.graph, MOVIE, 1, ROLLCALL, 8, 0);
    edit.graph.observer = &edit;
    edit.graph.before_insert = adopt_roll;
    edit.graph.before_unlink = unlink_roll;
    header.kind = MM_COLLECTION_STRINGS;
    header.system = MM_TEXT_WINDOWS;
    header.big_endian = 1;
    header.width = 32;
    header.count = 2;
    header.bytes = 9;
    header.free_count = -1;
    CHECK(mm_collection_header_write(&header, input, sizeof(input)) == MM_STORAGE_OK);
    memcpy(input + 20, "\003old\004kept", 9);
    mm_movie_write_actor(&original, input + 33, 1);
    input[64] = 4;
    mm_movie_write_actor(&other, input + 65, 1);
    input[81] = 0xba; /* A legacy pointer placeholder has no runtime meaning. */
    CHECK(mm_memory_stream_init(&edit.source, input, sizeof(input)));
    CHECK(mm_actor_archive_register(&editor, 1, NULL, &name) == MM_ACTOR_REGISTER_OK && edit.step == 3);

    CHECK(mm_rollcall_table_init(&table, MM_MOVIE_ACTOR_BYTES, 1, MM_TEXT_WINDOWS, 0, NULL) == MM_STORAGE_OK);
    CHECK(mm_memory_stream_init(&output, edit.output, edit.size));
    CHECK(mm_rollcall_table_read(&table, &output.stream, NULL, NULL, &big) == MM_STORAGE_OK);
    CHECK(big == 0 && table.count == 2);
    CHECK(mm_rollcall_table_name(&table, 0, &text, &characters) && characters == 6 && !memcmp(text, "edited", 6));
    CHECK(mm_rollcall_table_extra(&table, 0, bytes) && mm_memory_stream_init(&row, bytes, sizeof(bytes)));
    CHECK(mm_movie_read_actor(&row.stream, big, &actor, NULL) == MM_MOVIE_OK);
    CHECK(actor.actor_id == 4 && actor.reference_count == 3 && actor.browser_flags == 2 &&
          actor.source_id == 0 && actor.template_type == MM_TMPL && actor.template_number == 19);
    CHECK(mm_rollcall_table_name(&table, 1, &text, &characters) && characters == 4 && !memcmp(text, "kept", 4));
    CHECK(mm_rollcall_table_extra(&table, 1, bytes));
    CHECK(mm_movie_read_actor(&row.stream, big, &actor, NULL) == MM_MOVIE_OK);
    CHECK(actor.actor_id == 6 && actor.reference_count == 2 && actor.source_id == 5 && actor.browser_flags == 1 &&
          actor.template_number == 2 && bytes[16] == 0 && bytes[17] == 0 && bytes[18] == 0 && bytes[19] == 0);
    CHECK(mm_rollcall_table_dispose(&table) == MM_STORAGE_OK);
}

typedef struct PlacementEdit
{
    ChunkIndexFixture graph;
    MMActorHeader original;
    MMMemoryStream stream;
    int added, step, fail_appearance;
} PlacementEdit;

static MMChunkAccess placement_chunks(void *context)
{
    return fixture_chunk_access(&((PlacementEdit *)context)->graph);
}
static int placement_read(void *context, MMU32 type, MMU32 number, MMReadStream *stream)
{
    PlacementEdit *edit = (PlacementEdit *)context;
    CHECK(type == ACTOR && number == 5 && !edit->added);
    *stream = edit->stream.stream;
    return 1;
}
static int placement_add(void *context, MMU32 type, MMU32 bytes, MMU32 *number, MMStorageWriter *writer)
{
    PlacementEdit *edit = (PlacementEdit *)context;
    CHECK(type == ACTOR && bytes == MM_ACTOR_HEADER_BYTES && !writer && !edit->added);
    *number = 3;
    fixture_chunk_add(&edit->graph, type, *number);
    edit->added = 1;
    return 1;
}
static int placement_put(void *context, MMU32 type, MMU32 number, const void *bytes, MMU32 size)
{
    PlacementEdit *edit = (PlacementEdit *)context;
    const MMActorHeader *header = (const MMActorHeader *)bytes;
    CHECK(type == ACTOR && number == 3 && size == MM_ACTOR_HEADER_BYTES && edit->step++ == 0);
    CHECK(header->actor_id == 23 && header->translation[0] == 114688 && header->last_frame == 8);
    return 1;
}
static int placement_adopt(void *context, MMU32 parent, const MMChunkChild *child)
{
    PlacementEdit *edit = (PlacementEdit *)context;
    if (parent == 3)
        CHECK(child->type == ROUTE && child->number == 9 && child->slot == 0 && edit->step++ == 1);
    else
    {
        CHECK(parent == 20 && child->type == ACTOR && child->number == 3 && child->slot == 4);
        CHECK(edit->step++ == 4);
    }
    return 1;
}
static void placement_unlink(void *context, MMU32 parent, const MMChunkChild *child)
{
    PlacementEdit *edit = (PlacementEdit *)context;
    CHECK(parent == 20);
    CHECK((child->type == ACTOR && child->number == 5 && child->slot == 4 && edit->step == 3) ||
          (child->type == THUMB && child->number == 10 && child->slot == 0 && edit->step == 5));
    ++edit->step;
}
static int placement_appearance(void *context, MMU32 actor)
{
    PlacementEdit *edit = (PlacementEdit *)context;
    CHECK(actor == 3 && edit->step++ == 2 && edit->graph.rows[0].entry.child_count == 1);
    return !edit->fail_appearance;
}

static void placement_transaction(void)
{
    static const float identity[] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    static const float scale[] = {1, 1, 1};
    float delta[] = {1.25f, 0, 0};
    PlacementEdit edit;
    MMMovieArchiveEditor editor = {NULL, 1, MM_TEXT_WINDOWS, 0, NULL, placement_chunks, placement_read,
                                   placement_add, placement_put, NULL, NULL, NULL, {0}};
    MMActorArchivePose pose = {20, 5, NULL, identity, scale, NULL, placement_appearance};
    MMChunkAccess chunks;
    MMU32 actor;
    int fail;
    pose.context = editor.context = &edit;
    editor.edit = fixture_chunk_edit(&edit.graph);
    pose.translation = delta;
    for (fail = 0; fail <= 1; ++fail)
    {
        memset(&edit, 0, sizeof(edit));
        edit.original.order = 1;
        edit.original.actor_id = 23;
        edit.original.last_frame = 8;
        edit.original.translation[0] = 32768;
        fixture_chunk_seed_link(&edit.graph, ACTOR, 5, ROUTE, 9, 0);
        fixture_chunk_seed_link(&edit.graph, MM_SOUND_SCENE, 20, THUMB, 10, 0);
        fixture_chunk_seed_link(&edit.graph, MM_SOUND_SCENE, 20, ACTOR, 5, 4);
        edit.graph.observer = &edit;
        edit.graph.before_insert = placement_adopt;
        edit.graph.before_unlink = placement_unlink;
        CHECK(mm_memory_stream_init(&edit.stream, &edit.original, sizeof(edit.original)));
        delta[0] = 32700.0f;
        CHECK(mm_actor_archive_place(&editor, &pose) == MM_ACTOR_EDIT_ARCHIVE_RANGE && !edit.added);
        delta[0] = 1.25f;
        edit.fail_appearance = fail;
        CHECK(mm_actor_archive_place(&editor, &pose) ==
              (fail ? MM_ACTOR_EDIT_ARCHIVE_APPEARANCE : MM_ACTOR_EDIT_ARCHIVE_OK));
        chunks = placement_chunks(&edit);
        CHECK(edit.step == (fail ? 3 : 6) &&
              mm_chunk_link_number(&chunks, MM_SOUND_SCENE, 20, 4, ACTOR, &actor) == MM_CHUNK_OK &&
              actor == (fail ? 5U : 3U));
    }
}

typedef struct ReframeEdit
{
    ChunkIndexFixture graph;
    MMActorHeader header;
    MMMemoryStream read;
    unsigned char source_route[44], source_events[108], saved_route[64], saved_events[288];
    MMU32 route_size, events_size, write_type;
    int copied, published, thumbnail_removed, invalid_cut;
} ReframeEdit;

static MMChunkAccess reframe_chunks(void *context)
{
    return fixture_chunk_access(&((ReframeEdit *)context)->graph);
}
static int reframe_read(void *context, MMU32 type, MMU32 number, MMReadStream *stream)
{
    ReframeEdit *edit = (ReframeEdit *)context;
    int ok;
    if (type == ACTOR)
    {
        CHECK(number == 5);
        ok = mm_memory_stream_init(&edit->read, &edit->header, sizeof(edit->header));
    }
    else if (type == ROUTE)
    {
        CHECK(number == 4);
        ok = mm_memory_stream_init(&edit->read, edit->source_route, sizeof(edit->source_route));
    }
    else
    {
        CHECK(type == EVENTS && number == 3);
        ok = mm_memory_stream_init(&edit->read, edit->source_events, sizeof(edit->source_events));
    }
    *stream = edit->read.stream;
    return ok;
}
static int reframe_write(void *context, MMU32 offset, const void *bytes, size_t size)
{
    ReframeEdit *edit = (ReframeEdit *)context;
    unsigned char *destination = edit->write_type == ROUTE ? edit->saved_route : edit->saved_events;
    MMU32 length = edit->write_type == ROUTE ? edit->route_size : edit->events_size;
    CHECK(offset <= length && size <= length - offset);
    memcpy(destination + offset, bytes, size);
    return 1;
}
static int reframe_add(void *context, MMU32 type, MMU32 size, MMU32 *number, MMStorageWriter *writer)
{
    ReframeEdit *edit = (ReframeEdit *)context;
    if (type == ACTOR)
    {
        CHECK(!writer && size == MM_ACTOR_HEADER_BYTES && !edit->copied);
        *number = 2;
        fixture_chunk_add(&edit->graph, type, *number);
        edit->copied = 1;
        return 1;
    }
    CHECK(edit->copied && writer);
    if (type == ROUTE)
    {
        CHECK(!edit->route_size && size <= sizeof(edit->saved_route));
        edit->route_size = size;
        *number = 11;
    }
    else
    {
        CHECK(type == EVENTS && edit->route_size && !edit->events_size && size <= sizeof(edit->saved_events));
        edit->events_size = size;
        *number = 12;
    }
    fixture_chunk_add(&edit->graph, type, *number);
    edit->write_type = type;
    writer->context = context;
    writer->length = size;
    writer->write_at = reframe_write;
    return 1;
}
static int reframe_put(void *context, MMU32 type, MMU32 number, const void *bytes, MMU32 size)
{
    ReframeEdit *edit = (ReframeEdit *)context;
    CHECK(type == ACTOR && number == 2 && size == sizeof(edit->header) && !memcmp(bytes, &edit->header, size));
    return 1;
}
static void reframe_unlink(void *context, MMU32 parent, const MMChunkChild *child)
{
    ReframeEdit *edit = (ReframeEdit *)context;
    CHECK(parent == 20);
    if (child->type == ACTOR)
    {
        CHECK(child->number == 5 && child->slot == 4 && !edit->published && edit->route_size && edit->events_size);
        edit->published = 1;
    }
    else
    {
        CHECK(child->type == THUMB && child->number == 10 && child->slot == 0 && edit->published == 2);
        edit->thumbnail_removed = 1;
    }
}
static int reframe_adopt(void *context, MMU32 parent, const MMChunkChild *child)
{
    ReframeEdit *edit = (ReframeEdit *)context;
    if (parent == 2)
    {
        CHECK(child->slot == 0 &&
            ((child->type == ROUTE && child->number == 11) || (child->type == EVENTS && child->number == 12)));
    }
    else
    {
        CHECK(parent == 20 && child->type == ACTOR && child->number == 2 && child->slot == 4);
        CHECK(edit->published == 1);
        edit->published = 2;
    }
    return 1;
}
static int reframe_pose(void *context, MMI32 actor, MMSceneReframePose *pose)
{
    ReframeEdit *edit = (ReframeEdit *)context;
    CHECK(actor == 23);
    memset(pose, 0, sizeof(*pose));
    pose->location.dwrOffset = (edit->invalid_cut ? 11 : 5) * 65536L;
    pose->location.dnfrm = 1;
    pose->position.dxr = 6 * 65536L;
    pose->rotation[0] = pose->rotation[4] = pose->rotation[8] = 1.0f;
    return 1;
}
static void reframe_from_euler(const MMActorEntrance *entrance, MMActorMatrix *matrix)
{
    CHECK(!entrance->x && !entrance->y && !entrance->z);
    mm_matrix_identity(matrix);
}
static void reframe_to_euler(const MMActorMatrix *matrix, MMActorEntrance *entrance)
{
    (void)matrix;
    (void)entrance;
    CHECK(0); /* Partial entrance keeps the already evaluated orientation. */
}

static void reframe_fixture(ReframeEdit *edit)
{
    const unsigned short one = 1;
    MMCollectionHeader wire = {0};
    MMActorRoutePoint nodes[] = {{{0,0,0}, 655360}, {{655360,0,0}, 0}};
    MMActorEvent add = {MM_ACTOR_ADD, 1, {0,0,0}}, tweak = {MM_ACTOR_TWEAK, 3, {0,524288,0}};
    MMActorEntrance entrance = {0};
    MMActorPoint future = {524288,0,0};
    MMU32 locations[] = {0,40,40,32};
    memset(edit, 0, sizeof(*edit));
    edit->header.order = 1;
    edit->header.actor_id = 23;
    fixture_chunk_seed_link(&edit->graph, ACTOR, 5, EVENTS, 3, 0);
    fixture_chunk_seed_link(&edit->graph, ACTOR, 5, ROUTE, 4, 0);
    fixture_chunk_seed_link(&edit->graph, MM_SOUND_SCENE, 20, THUMB, 10, 0);
    fixture_chunk_seed_link(&edit->graph, MM_SOUND_SCENE, 20, ACTOR, 5, 4);
    edit->graph.observer = edit;
    edit->graph.before_insert = reframe_adopt;
    edit->graph.before_unlink = reframe_unlink;
    wire.kind = MM_COLLECTION_ARRAY;
    wire.big_endian = *(const unsigned char *)&one == 0;
    wire.system = MM_TEXT_WINDOWS;
    wire.count = 2;
    wire.width = MM_ACTOR_ROUTE_BYTES;
    CHECK(mm_collection_header_write(&wire, edit->source_route, sizeof(edit->source_route)) == MM_STORAGE_OK);
    memcpy(edit->source_route + 12, nodes, sizeof(nodes));
    wire.kind = MM_COLLECTION_GROUP;
    wire.width = MM_ACTOR_EVENT_BYTES;
    wire.bytes = 72;
    wire.free_count = -1;
    CHECK(mm_collection_header_write(&wire, edit->source_events, sizeof(edit->source_events)) == MM_STORAGE_OK);
    memcpy(edit->source_events + 20, &add, sizeof(add));
    memcpy(edit->source_events + 40, &entrance, sizeof(entrance));
    memcpy(edit->source_events + 60, &tweak, sizeof(tweak));
    memcpy(edit->source_events + 80, &future, sizeof(future));
    memcpy(edit->source_events + 92, locations, sizeof(locations));
}

static void reframe_transaction(void)
{
    static const float identity[12] = {1,0,0, 0,1,0, 0,0,1, 0,0,0};
    static const MMI32 kinds[] = {MM_ACTOR_ADD, MM_ACTOR_MOVE, MM_ACTOR_TWEAK,
                                  MM_ACTOR_ROTATE_HERE, MM_ACTOR_TWEAK};
    ReframeEdit edit;
    MMMovieArchiveEditor editor = {NULL, 1, MM_TEXT_WINDOWS, 0, NULL, reframe_chunks, reframe_read,
                                   reframe_add, reframe_put, NULL, NULL, NULL, {0}};
    MMSceneReframeHost host = {NULL, reframe_pose, reframe_from_euler, reframe_to_euler};
    MMSceneReframeCamera work = {identity, 70.0f, 1.0f}, camera = {identity, 70.0f, 2.0f};
    MMMemoryStream output;
    MMCollectionHeader wire;
    MMActorRoutePoint nodes[3];
    MMActorEvent event;
    MMActorPoint point;
    MMU32 i, offset;
    MMI32 size;
    editor.context = host.context = &edit;
    editor.edit = fixture_chunk_edit(&edit.graph);
    reframe_fixture(&edit);
    CHECK(mm_scene_archive_reframe(&editor, 20, 2, &work, &camera, &host) == MM_SCENE_REFRAME_OK);
    CHECK(edit.published == 2 && edit.thumbnail_removed && edit.route_size == 12 + sizeof(nodes));
    memcpy(nodes, edit.saved_route + 12, sizeof(nodes));
    CHECK(nodes[0].xyz.dxr == 0 && nodes[0].dwr == 327680);
    CHECK(nodes[1].xyz.dxr == 327680 && nodes[1].dwr == 327680 && nodes[2].xyz.dxr == 983040);
    CHECK(mm_memory_stream_init(&output, edit.saved_events, edit.events_size));
    CHECK(mm_collection_header_read(&output.stream, MM_COLLECTION_GROUP, 0, &wire) == MM_STORAGE_OK);
    CHECK(wire.count == sizeof(kinds) / sizeof(kinds[0]));
    for (i = 0; i < wire.count; ++i)
    {
        CHECK(mm_collection_group_row(&wire, edit.saved_events + 20, i, &offset, &size));
        memcpy(&event, edit.saved_events + 20 + offset, sizeof(event));
        CHECK(event.aet == kinds[i]);
        if (i == 4)
        {
            CHECK(event.nfrm == 3 && event.rtel.irpt == 1 && event.rtel.dwrOffset == 196608);
            CHECK(size == (MMI32)sizeof(point));
            memcpy(&point, edit.saved_events + 40 + offset, sizeof(point));
            CHECK(point.dxr == 720896);
        }
    }
    reframe_fixture(&edit);
    edit.invalid_cut = 1;
    CHECK(mm_scene_archive_reframe(&editor, 20, 2, &work, &camera, &host) == MM_SCENE_REFRAME_LOCATION);
    CHECK(edit.copied && !edit.published && !edit.route_size && !edit.thumbnail_removed);
    reframe_fixture(&edit);
    camera.aspect = -1;
    CHECK(mm_scene_archive_reframe(&editor, 20, 2, &work, &camera, &host) == MM_SCENE_REFRAME_LENS);
    CHECK(!edit.copied);
}

enum { AUTHOR_SCENE = 5, AUTHOR_ACTOR = 42, AUTHOR_TEMPLATE = 8 };
#define SCENE 0x5343454eUL
#define START 0x47475354UL

typedef struct AuthorEdit
{
    ChunkIndexFixture graph;
    MMMemoryStream input;
    MMActorHeader header;
    unsigned char scene[16], start[MM_SCENE_BACKGROUND_GROUP_BYTES], tmpl[16], text[24];
    unsigned char route[28], events[136], other[128];
    unsigned char *output;
    MMU32 length;
    int tables, copied, linked, dropped;
} AuthorEdit;

static MMChunkAccess author_chunks(void *context)
{
    return fixture_chunk_access(&((AuthorEdit *)context)->graph);
}
static int author_read(void *context, MMU32 type, MMU32 number, MMReadStream *stream)
{
    AuthorEdit *f = (AuthorEdit *)context;
    if (type == MM_TMPL)
        CHECK(number == 7 && mm_memory_stream_init(&f->input, f->tmpl, sizeof(f->tmpl)));
    else if (type == MM_TDT)
        CHECK(number == 6 && f->copied && mm_memory_stream_init(&f->input, f->text, sizeof(f->text)));
    else if (type == SCENE)
        CHECK(number == AUTHOR_SCENE && mm_memory_stream_init(&f->input, f->scene, sizeof(f->scene)));
    else
        CHECK(type == START && number == 4 && mm_memory_stream_init(&f->input, f->start, sizeof(f->start)));
    *stream = f->input.stream;
    return 1;
}
static int author_write(void *context, MMU32 offset, const void *data, size_t bytes)
{
    AuthorEdit *f = (AuthorEdit *)context;
    CHECK(offset <= f->length && bytes <= f->length - offset);
    memcpy(f->output + offset, data, bytes);
    return 1;
}
static int author_add(void *context, MMU32 type, MMU32 length, MMU32 *number, MMStorageWriter *writer)
{
    AuthorEdit *f = (AuthorEdit *)context;
    if (type == ACTOR)
    {
        CHECK(length == sizeof(f->header) && !writer);
        *number = 9;
        fixture_chunk_add(&f->graph, type, *number);
        return 1;
    }
    if (type == ROUTE)
        CHECK(length == sizeof(f->route));
    else if (type == EVENTS)
        CHECK(length == sizeof(f->events));
    else
        CHECK((type == START || type == ROLLCALL) && length <= sizeof(f->other));
    f->output = type == ROUTE ? f->route : type == EVENTS ? f->events : f->other;
    f->length = length;
    *number = type == ROLLCALL ? (MMU32)++f->tables : 1;
    fixture_chunk_add(&f->graph, type, *number);
    writer->context = context;
    writer->length = length;
    writer->write_at = author_write;
    return 1;
}
static int author_put(void *context, MMU32 type, MMU32 number, const void *data, MMU32 bytes)
{
    AuthorEdit *f = (AuthorEdit *)context;
    CHECK(type == ACTOR && number == 9 && bytes == sizeof(f->header));
    memcpy(&f->header, data, bytes);
    return 1;
}
static int author_patch(void *context, MMU32 type, MMU32 number, MMU32 offset, const void *data, MMU32 bytes)
{
    AuthorEdit *f = (AuthorEdit *)context;
    CHECK(type == MM_TDT && number == 6 && offset == 8 && bytes == 4);
    memcpy(f->text + offset, data, bytes);
    return 1;
}
static int author_copy(void *context, MMU32 type, MMU32 number, MMU32 *copied)
{
    CHECK(type == MM_TMPL && number == 7);
    *copied = AUTHOR_TEMPLATE;
    return ++((AuthorEdit *)context)->copied;
}
static int author_link(void *context, MMU32 number, const MMChunkChild *child)
{
    AuthorEdit *f = (AuthorEdit *)context;
    if (number == 1)
        CHECK(child->type == ROLLCALL && ((child->number == 1 && child->slot == 1) ||
              (child->number == 2 && child->slot == 0)));
    else if (number == AUTHOR_SCENE)
        CHECK((child->type == ACTOR && child->number == AUTHOR_ACTOR && child->slot == 0) ||
              (child->type == START && child->number == 1 && child->slot == 1));
    else
        CHECK(number == AUTHOR_ACTOR && child->slot == 0 &&
              ((child->type == MM_TMPL && child->number == AUTHOR_TEMPLATE) ||
               ((child->type == ROUTE || child->type == EVENTS) && child->number == 1)));
    return ++f->linked;
}
static void author_drop_link(void *context, MMU32 number, const MMChunkChild *child)
{
    CHECK(number == AUTHOR_SCENE);
    if (child->type == THUMB)
    {
        CHECK(child->number == 3 && child->slot == 0);
        ++((AuthorEdit *)context)->dropped;
    }
    else
        CHECK(child->type == START && child->number == 4 && child->slot == 1);
}
static void author_seed_index(AuthorEdit *f)
{
    const MMChunkChild thumb = {THUMB, 3, 0}, start = {START, 4, 1}, text = {MM_TDT, 6, 0};
    const MMChunkEdit edit = fixture_chunk_edit(&f->graph);
    memset(&f->graph, 0, sizeof(f->graph));
    fixture_chunk_add(&f->graph, MOVIE, 1);
    fixture_chunk_add(&f->graph, SCENE, AUTHOR_SCENE);
    fixture_chunk_add(&f->graph, MM_TMPL, AUTHOR_TEMPLATE);
    fixture_chunk_add(&f->graph, thumb.type, thumb.number);
    fixture_chunk_add(&f->graph, start.type, start.number);
    fixture_chunk_add(&f->graph, text.type, text.number);
    CHECK(mm_chunk_adopt(&edit, SCENE, AUTHOR_SCENE, &thumb, 1) == MM_CHUNK_OK);
    CHECK(mm_chunk_adopt(&edit, SCENE, AUTHOR_SCENE, &start, 1) == MM_CHUNK_OK);
    CHECK(mm_chunk_adopt(&edit, MM_TMPL, AUTHOR_TEMPLATE, &text, 1) == MM_CHUNK_OK);
    f->graph.observer = f;
    f->graph.before_insert = author_link;
    f->graph.before_unlink = author_drop_link;
}

static void insertion_transaction(void)
{
    const float point[3] = {1.25f, -2.0f, 0.0f};
    AuthorEdit f = {0};
    MMMovieArchiveEditor editor = {NULL, 1, MM_TEXT_WINDOWS, 0, NULL, author_chunks, author_read,
                                   author_add, author_put, NULL, NULL, author_patch, {0}};
    MMActorAuthorHost host = {NULL, NULL, NULL, NULL, author_copy};
    MMActorInsert insert = {AUTHOR_SCENE, AUTHOR_ACTOR, 7, 12, 3, 5, NULL, "Text", 4, 1};
    MMSceneState scene;
    MMCollectionHeader group;
    MMActorEvent event;
    MMActorEntrance add;
    MMMemoryStream stream;
    MMRollCallTable listing;
    MMMovieActor listed;
    unsigned char row[MM_MOVIE_ACTOR_BYTES];
    MMU32 offset;
    MMI32 bytes;
    int detail, big;
    editor.context = host.context = &f;
    editor.edit = fixture_chunk_edit(&f.graph);
    host.source = host.destination = &editor;
    insert.point = point;
    author_seed_index(&f);
    CHECK(mm_scene_init(&scene, 1));
    scene.firstFrame = 3;
    scene.lastFrame = 5;
    CHECK(mm_scene_encode_header(&scene, MM_TEXT_WINDOWS, 0, f.scene));
    CHECK(mm_scene_encode_background_group(MM_TEXT_WINDOWS, 0, 3, 2, 11, f.start));
    f.tmpl[1] = f.text[1] = 1; /* Foreign byte order. */
    f.tmpl[4] = 0x12; f.tmpl[5] = 0x34; f.tmpl[15] = MM_TEMPLATE_TEXT;
    f.text[7] = 4; f.text[23] = 19;
    insert.first = 2;
    CHECK(mm_actor_archive_insert(&host, &insert, &detail) == MM_ACTOR_AUTHOR_FRAME && !f.linked && !f.copied);
    insert.first = 3;
    CHECK(mm_actor_archive_insert(&host, &insert, &detail) == MM_ACTOR_AUTHOR_OK && detail == MM_ACTOR_REGISTER_OK);
    CHECK(f.copied == 1 && f.linked == 7 && f.dropped == 1 && f.tables == 2);
    CHECK(f.header.actor_id == 12 && f.header.template_number == AUTHOR_TEMPLATE && !f.header.source_id &&
          f.header.translation[0] == 81920 && f.header.translation[1] == (MMU32)-131072);
    CHECK(f.text[7] == 4 && f.text[8] == 0 && f.text[11] == 2 && f.text[23] == 19);
    CHECK(mm_memory_stream_init(&stream, f.route, sizeof(f.route)));
    CHECK(mm_collection_header_read(&stream.stream, MM_COLLECTION_ARRAY, 0, &group) == MM_STORAGE_OK &&
          group.width == MM_ACTOR_ROUTE_BYTES && group.count == 1);
    CHECK(mm_memory_stream_init(&stream, f.events, sizeof(f.events)));
    CHECK(mm_collection_header_read(&stream.stream, MM_COLLECTION_GROUP, 0, &group) == MM_STORAGE_OK &&
          group.width == MM_ACTOR_EVENT_BYTES && group.count == 3);
    CHECK(mm_collection_group_row(&group, f.events + 20, 0, &offset, &bytes) && bytes == sizeof(add));
    memcpy(&event, f.events + 20 + offset, sizeof(event));
    memcpy(&add, f.events + 20 + offset + sizeof(event), sizeof(add));
    CHECK(event.aet == MM_ACTOR_ADD && event.nfrm == 3 && !event.rtel.irpt && add.x == 0x1234 && !add.y && !add.z);

    f.tables = f.copied = f.linked = f.dropped = 0;
    author_seed_index(&f);
    f.tmpl[15] = MM_TEMPLATE_PROP;
    insert.embedded_text = 0;
    insert.first = 4;
    CHECK(mm_actor_archive_insert(&host, &insert, &detail) == MM_ACTOR_AUTHOR_OK);
    CHECK(!f.copied && f.linked == 6 && !f.dropped);
    CHECK(f.header.source_id == 2 && f.header.template_number == 7 && f.header.first_frame == 4);
    CHECK(mm_rollcall_table_init(&listing, MM_MOVIE_ACTOR_BYTES, 1, MM_TEXT_WINDOWS, 0, NULL) == MM_STORAGE_OK);
    CHECK(mm_memory_stream_init(&stream, f.other, f.length));
    CHECK(mm_rollcall_table_read(&listing, &stream.stream, NULL, NULL, &big) == MM_STORAGE_OK && listing.count == 1);
    CHECK(mm_rollcall_table_extra(&listing, 0, row) && mm_memory_stream_init(&stream, row, sizeof(row)));
    CHECK(mm_movie_read_actor(&stream.stream, big, &listed, NULL) == MM_MOVIE_OK &&
          listed.actor_id == 12 && listed.source_id == 2 && listed.template_number == 7 && listed.browser_flags == 1);
    CHECK(mm_rollcall_table_dispose(&listing) == MM_STORAGE_OK);
}

static MMChunkAccess sound_scene_graph(void *context)
{
    return fixture_chunk_access(&((Fixture *)context)->embedded);
}
static int add_sound_copy(void *context, MMU32 type, MMU32 size, MMU32 *number, MMStorageWriter *writer)
{
    Fixture *f = (Fixture *)context;
    if (type == EVENTS)
    {
        *number = 20;
        fixture_chunk_add(&f->embedded, type, *number);
        return replace_events(context, type, *number, size, writer);
    }
    CHECK(type == ACTOR && !writer && size == MM_ACTOR_HEADER_BYTES && f->writes == 1);
    *number = 2;
    fixture_chunk_add(&f->embedded, type, *number);
    return 1;
}
static int adopt_sound_copy(void *context, MMU32 number, const MMChunkChild *child)
{
    Fixture *f = (Fixture *)context;
    if (number == 2)
    {
        CHECK(child->slot == 0);
        if (child->type == ROUTE)
        {
            CHECK(child->number == 12);
            ++f->sound_paths;
        }
        else
        {
            CHECK(child->type == EVENTS && child->number == 20);
            ++f->sound_tracks;
        }
    }
    else
    {
        CHECK(number == 1 && child->type == ACTOR && child->number == 2 && child->slot == 0);
        CHECK(f->sound_unlinks == 1 && f->sound_paths == 1 && f->sound_tracks == 1);
        ++f->sound_scene_links;
    }
    return 1;
}
static void unlink_sound_copy(void *context, MMU32 number, const MMChunkChild *child)
{
    Fixture *f = (Fixture *)context;
    CHECK(number == 1 && child->type == ACTOR && child->number == 1 && child->slot == 0);
    ++f->sound_unlinks;
}

static void edit_actor_sound(void)
{
    Fixture f;
    const MMMovieSoundArchiveCue cue = {10, 0, 6, 2, 3, 2, 888, 0, 1};
    MMMovieSoundArchiveEdit edit = {NULL, NULL, 10, 22000, 2, 1, 1, 0};
    MMMovieArchiveEditor editor = {NULL, 1, MM_TEXT_WINDOWS, 0, NULL, sound_scene_graph, source_read,
                                   add_sound_copy, put, NULL, NULL, NULL, {0}};
    MMChunkAccess chunks;
    MMU32 actor;
    editor.context = &f;
    editor.edit = fixture_chunk_edit(&f.embedded);
    edit.cue = &cue;
    init(&f, 1);
    fixture_chunk_seed_link(&f.embedded, ACTOR, 1, EVENTS, 10, 0);
    fixture_chunk_seed_link(&f.embedded, ACTOR, 1, ROUTE, 12, 0);
    fixture_chunk_seed_link(&f.embedded, MM_SOUND_SCENE, 1, ACTOR, 1, 0);
    fixture_chunk_seed_link(&f.embedded, MM_SOUND_SCENE, 3, ACTOR, 1, 0);
    f.embedded.observer = &f;
    f.embedded.before_insert = adopt_sound_copy;
    f.embedded.before_unlink = unlink_sound_copy;
    f.payloads[0].sound.style = 2;
    f.payloads[0].sound.cel = 2;
    CHECK(mm_movie_archive_edit_sound(&editor, 1, &edit) == MM_SOUND_ARCHIVE_NO_CUE && !f.writes && !f.sound_unlinks);
    f.payloads[0].sound.cel = 3;
    f.fail_write = 1;
    CHECK(mm_movie_archive_edit_sound(&editor, 1, &edit) == MM_SOUND_ARCHIVE_WRITE_TRACK &&
          f.writes == 1 && !f.sound_unlinks && !f.sound_tracks);
    CHECK(mm_chunk_delete(&editor.edit, EVENTS, 20) == MM_CHUNK_OK);
    f.writes = f.fail_write = 0;
    CHECK(mm_movie_archive_edit_sound(&editor, 1, &edit) == MM_SOUND_ARCHIVE_OK);
    CHECK(f.sound_scene_links == 1 && f.written.actor_id == f.input.actor_id);
    chunks = sound_scene_graph(&f);
    CHECK(mm_chunk_link_number(&chunks, MM_SOUND_SCENE, 1, 0, ACTOR, &actor) == MM_CHUNK_OK && actor == 2);
    CHECK(mm_chunk_link_number(&chunks, MM_SOUND_SCENE, 3, 0, ACTOR, &actor) == MM_CHUNK_OK && actor == 1);
    read_output_events(&f);
    CHECK(f.events[0].nfrm == 10 && f.events[0].rtel.irpt == 15 && f.payloads[0].sound.child == 2);
    CHECK(f.payloads[0].sound.volume == 22000 && f.payloads[0].sound.loop == 1 &&
          f.payloads[0].sound.queue == 1 && f.payloads[1].sound.volume == 0);
}

static int read_cloned_events(void *context, MMU32 type, MMU32 number, MMReadStream *stream)
{
    CHECK(type == EVENTS && number == 20);
    return source_read(context, type, 10, stream);
}

static int add_rebound_events(void *context, MMU32 type, MMU32 size, MMU32 *number, MMStorageWriter *writer)
{
    Fixture *f = context;
    *number = 21;
    fixture_chunk_add(&f->embedded, type, *number);
    return replace_events(context, type, 20, size, writer);
}

static int copy_cloned_material(void *context, const MMTag *tag, MMU32 *number, int *created)
{
    Fixture *f = context;
    CHECK(tag->sid == 0 && !tag->cache && tag->type == MATERIAL && tag->number == 8);
    if (!copy_asset(context, tag->type, tag->number, number))
        return 0;
    fixture_chunk_add(&f->embedded, tag->type, *number);
    *created = 1;
    return 1;
}

static MMChunkAccess embedded_index(void *context)
{
    return fixture_chunk_access(&((Fixture *)context)->embedded);
}

static int insert_cloned_child(void *context, MMU32 actor, const MMChunkChild *child)
{
    Fixture *f = (Fixture *)context;
    CHECK(actor == 2);
    if (child->type == MATERIAL)
        CHECK(child->number == 108 && child->slot == 2 && f->embedded.rows[0].entry.child_count == 3);
    else
    {
        CHECK(child->type == EVENTS && child->number == 21 && child->slot == 0 && !f->old_unlinked);
        if (f->fail_link)
            return 0;
        ++f->replacement_adopted;
    }
    return 1;
}

static void unlink_original_events(void *context, MMU32 actor, const MMChunkChild *child)
{
    Fixture *f = (Fixture *)context;
    CHECK(actor == 2 && child->type == EVENTS && child->number == 20 && child->slot == 0 && f->replacement_adopted);
    ++f->old_unlinked;
}

static void discard_rebound_events(void *context, const MMChunkEntry *entry)
{
    Fixture *f = (Fixture *)context;
    CHECK(entry->type == EVENTS && entry->number == 21 && !f->replacement_adopted);
    ++f->replacement_discarded;
}

static void init_rebound_materials(Fixture *f, int big)
{
    init(f, big);
    fixture_chunk_seed_link(&f->embedded, ACTOR, 2, EVENTS, 20, 0);
    fixture_chunk_seed_link(&f->embedded, ACTOR, 2, MM_TMPL, 4, 0);
    fixture_chunk_seed_link(&f->embedded, ACTOR, 2, MATERIAL, 7, 1);
    fixture_chunk_seed_link(&f->embedded, ACTOR, 3, EVENTS, 20, 0);
    f->embedded.observer = f;
    f->embedded.before_insert = insert_cloned_child;
    f->embedded.before_unlink = unlink_original_events;
    f->embedded.before_delete = discard_rebound_events;
}

static void rebind_materials(void)
{
    Fixture f;
    MMMovieArchiveEditor editor = {0};
    MMMovieEmbeddedArchive materials = {0};
    MMChunkAccess index;
    MMChunkEntry asset;
    MMU32 row, track;
    int i;
    editor.context = materials.context = &f;
    materials.chunks = embedded_index;
    materials.copy_reference = copy_cloned_material;
    materials.edit = fixture_chunk_edit(&f.embedded);
    editor.system = MM_TEXT_WINDOWS;
    editor.chunks = embedded_index;
    editor.read = read_cloned_events;
    editor.add = add_rebound_events;
    editor.edit = materials.edit;
    init_rebound_materials(&f, 1);
    CHECK(mm_actor_archive_rebind_materials(&editor, 2, &materials));
    CHECK(f.copies == 1 && f.writes == 1 && f.replacement_adopted == 1 && f.old_unlinked == 1);
    index = embedded_index(&f);
    CHECK(mm_chunk_find(&index, MATERIAL, 108, &row) == MM_CHUNK_OK &&
          mm_chunk_at(&index, row, &asset) == MM_CHUNK_OK && asset.parent_count == 1 &&
          !(asset.flags & MM_CHUNK_LONER));
    CHECK(mm_chunk_link_number(&index, ACTOR, 2, 0, EVENTS, &track) == MM_CHUNK_OK && track == 21);
    CHECK(mm_chunk_link_number(&index, ACTOR, 3, 0, EVENTS, &track) == MM_CHUNK_OK && track == 20);
    read_output_events(&f);
    CHECK(f.payloads[0].sound.tag.number == 777 && f.payloads[6].costume.tag.number == 108 &&
          f.payloads[7].costume.tag.number == 0 && f.events[7].nfrm == 17);
    for (i = 0; i < 2; ++i)
    {
        init_rebound_materials(&f, 0);
        f.fail_write = !i;
        f.fail_link = i;
        CHECK(!mm_actor_archive_rebind_materials(&editor, 2, &materials));
        CHECK(!f.old_unlinked && f.replacement_discarded == 1 && !f.replacement_adopted);
        index = embedded_index(&f);
        CHECK(mm_chunk_link_number(&index, ACTOR, 2, 0, EVENTS, &track) == MM_CHUNK_OK && track == 20);
        CHECK(mm_chunk_find(&index, EVENTS, 21, &row) == MM_CHUNK_NOT_FOUND);
    }
}

typedef struct ImportedSounds
{
    Fixture wire;
    ChunkIndexFixture index;
    int fail, adoptions;
} ImportedSounds;

static void import_sound_access(void *p, MMChunkAccess *out)
{
    *out = fixture_chunk_access(&((ImportedSounds *)p)->index);
}

static int import_sound_payload(void *p, const MMChunkEntry *entry, MMMovieReceiveBytes receive, void *out)
{
    ImportedSounds *f = (ImportedSounds *)p;
    MMReadStream stream;
    MMMemoryStream memory;
    MMCollectionHeader header = {0};
    unsigned char group[132] = {0};
    MMU32 i, size, offset = 0;
    const unsigned short one = 1;
    const MMU32 packet[] = {65536, 1, 0, 4,
        4, 0, 0, MM_SOUND_CHUNK, 5,
        5, 0, 0, MM_SOUND_CHUNK, 5,
        6, 2, 0, MM_SOUND_CHUNK, 5,
        7, 0, 0, MM_SOUND_CHUNK, 5};
    MMU32 row[26] = {3, MM_SCENE_ADD_ACTOR, 0};
    if (entry->type == EVENTS)
    {
        source_read(&f->wire, EVENTS, 10, &stream);
        return receive(out, &stream);
    }
    if (entry->type == MM_SCENE_FRAME_GROUP)
    {
        row[1] = MM_SCENE_PLAY_SOUND;
        memcpy(row + 2, packet, sizeof(packet));
        size = 8 + sizeof(packet);
    }
    else
        size = 12;
    header.kind = MM_COLLECTION_GROUP;
    header.system = MM_TEXT_WINDOWS;
    header.big_endian = f->wire.big_endian;
    header.count = 1;
    header.width = 8;
    header.free_count = -1;
    header.bytes = size;
    CHECK(mm_collection_header_write(&header, group, sizeof(group)) == MM_STORAGE_OK);
    if (header.big_endian != (*(const unsigned char *)&one == 0))
        for (i = 0; i < size / 4; ++i)
            row[i] = ((row[i] & 255U) << 24) | ((row[i] & 65280U) << 8) |
                     ((row[i] >> 8) & 65280U) | (row[i] >> 24);
    memcpy(group + 20, row, size);
    if (header.big_endian != (*(const unsigned char *)&one == 0))
        offset = size << 24;
    else
        offset = size;
    memcpy(group + 24 + size, &offset, 4);
    CHECK(mm_memory_stream_init(&memory, group, 28 + size));
    return receive(out, &memory.stream);
}

static int insert_imported_sound(void *p, MMU32 scene, const MMChunkChild *child)
{
    ImportedSounds *f = (ImportedSounds *)p;
    CHECK(scene == 3 && child->type == MM_SOUND_CHUNK);
    CHECK((child->slot == 2 && child->number == 21) || (child->slot == 4 && child->number == 24));
    return ++f->adoptions != f->fail;
}

static void seed_sound_links(ChunkIndexFixture *index, MMU32 type, MMU32 number,
                             const MMChunkChild *links, size_t count)
{
    const MMChunkEdit edit = fixture_chunk_edit(index);
    size_t i;
    for (i = 0; i < count; ++i)
        CHECK(mm_chunk_adopt(&edit, type, number, &links[i], 0) == MM_CHUNK_OK);
}

static void init_imported_sounds(ImportedSounds *f, int big)
{
    static const MMChunkChild scene[] = {{ACTOR, 1, 0}, {MM_SCENE_FRAME_GROUP, 4, 0},
        {MM_SCENE_START_GROUP, 5, 1}, {MM_SOUND_CHUNK, 99, 5}};
    static const MMChunkChild group = {EVENTS, 10, 0};
    static const MMChunkChild other = {MM_SOUND_CHUNK, 55, 2};
    static const MMChunkChild bank[] = {{MM_SOUND_SCENE, 3, 0}, {MM_SOUND_CHUNK, 21, 2},
        {MM_SOUND_CHUNK, 24, 4}, {MM_SOUND_CHUNK, 25, 5}, {MM_SOUND_CHUNK, 26, 6}};
    MMU32 i;
    memset(f, 0, sizeof(*f));
    init(&f->wire, big);
    f->wire.payloads[5].sound.child = 6;
    fixture_chunk_add(&f->index, MM_SOUND_SCENE, 3);
    fixture_chunk_add(&f->index, MOVIE, 0);
    fixture_chunk_add(&f->index, MOVIE, 9);
    fixture_chunk_add(&f->index, EVENTS, 10);
    fixture_chunk_add(&f->index, other.type, other.number);
    for (i = 0; i < sizeof(scene) / sizeof(scene[0]); ++i)
        fixture_chunk_add(&f->index, scene[i].type, scene[i].number);
    for (i = 1; i < sizeof(bank) / sizeof(bank[0]); ++i)
        fixture_chunk_add(&f->index, bank[i].type, bank[i].number);
    seed_sound_links(&f->index, ACTOR, 1, &group, 1);
    seed_sound_links(&f->index, MOVIE, 0, &other, 1);
    seed_sound_links(&f->index, MOVIE, 9, bank, sizeof(bank) / sizeof(bank[0]));
    seed_sound_links(&f->index, MM_SOUND_SCENE, 3, scene, sizeof(scene) / sizeof(scene[0]));
    f->index.observer = f;
    f->index.before_insert = insert_imported_sound;
}

static void link_imported_sounds(void)
{
    ImportedSounds f;
    MMMovieSessionGraph graph = {NULL, import_sound_access, NULL, import_sound_payload, NULL};
    const MMChunkEdit edit = fixture_chunk_edit(&f.index);
    MMChunkAccess chunks;
    MMChunkEntry entry;
    MMU32 row, number;
    int big;
    graph.context = &f;
    for (big = 0; big <= 1; ++big)
    {
        init_imported_sounds(&f, big);
        CHECK(mm_movie_archive_link_imported_sounds(&graph, 9, &edit) && f.adoptions == 2);
        import_sound_access(&f, &chunks);
        CHECK(mm_chunk_link_number(&chunks, MM_SOUND_SCENE, 3, 2, MM_SOUND_CHUNK, &number) == MM_CHUNK_OK &&
              number == 21);
        CHECK(mm_chunk_link_number(&chunks, MM_SOUND_SCENE, 3, 4, MM_SOUND_CHUNK, &number) == MM_CHUNK_OK &&
              number == 24);
        CHECK(mm_chunk_link_number(&chunks, MM_SOUND_SCENE, 3, 5, MM_SOUND_CHUNK, &number) == MM_CHUNK_OK &&
              number == 99);
        CHECK(mm_chunk_find(&chunks, MM_SOUND_CHUNK, 21, &row) == MM_CHUNK_OK &&
              mm_chunk_at(&chunks, row, &entry) == MM_CHUNK_OK && entry.parent_count == 2 &&
              (entry.flags & MM_CHUNK_LONER));
        CHECK(mm_movie_archive_link_imported_sounds(&graph, 9, &edit) && f.adoptions == 2);
    }
    init_imported_sounds(&f, 1);
    f.fail = 2;
    CHECK(!mm_movie_archive_link_imported_sounds(&graph, 9, &edit) && f.adoptions == 2);
    import_sound_access(&f, &chunks);
    CHECK(mm_chunk_link_number(&chunks, MM_SOUND_SCENE, 3, 2, MM_SOUND_CHUNK, &number) == MM_CHUNK_OK &&
          number == 21);
    CHECK(mm_chunk_link_number(&chunks, MM_SOUND_SCENE, 3, 4, MM_SOUND_CHUNK, &number) == MM_CHUNK_NOT_FOUND);
}

int main(void)
{
    Fixture f;
    const MMActorArchiveRequest request = {1, 1, 3, 7, 12, 9};
    const MMActorArchiveRequest forward = {1, 1, 3, 7, 9, 12};
    MMActorArchiveHost host = {0};
    MMMovieArchiveEditor source_editor = {NULL, 1, MM_TEXT_WINDOWS, 0, NULL, source, source_read,
                                          NULL, NULL, NULL, NULL, NULL, {0}};
    MMMovieArchiveEditor destination_editor = {NULL, 1, MM_TEXT_WINDOWS, 0, NULL, destination, destination_read,
                                               NULL, put, NULL, replace_events, NULL, {0}};
    MMU32 actor, i;
    MMActorHeader header;
    unsigned char descriptor[MM_ACTOR_ARCHIVE_TEXT_BYTES];
    int big;
    host.context = host.source_map = source_editor.context = destination_editor.context = &f;
    destination_editor.edit = fixture_chunk_edit(&f.destination);
    host.source = &source_editor;
    host.destination = &destination_editor;
    host.clone_actor = clone_actor;
    host.copy_asset = copy_transfer_asset;
    host.map_source = map_source;
    for (big = 0; big <= 1; ++big)
    {
        init(&f, big);
        memcpy(descriptor, f.descriptor, sizeof(descriptor));
        descriptor[big ? 23 : 20] = 109;
        actor = 777;
        CHECK(!mm_actor_archive_find(&source_editor, 1, 7, &actor) && actor == 777);
        CHECK(mm_actor_archive_find(&source_editor, 1, 6, &actor) && actor == 1);
        CHECK(mm_actor_archive_transfer(&request, &host, &actor, &header) == MM_ACTOR_ARCHIVE_OK);
        CHECK(actor == 2 && header.actor_id == 7 && header.first_frame == 6 &&
              header.last_frame == INVALID_FRAME && header.template_number == 4);
        CHECK(f.written.actor_id == 7 && f.writes == 1 && f.copies == 4);
        CHECK(!memcmp(f.descriptor, descriptor, sizeof(descriptor)));
        read_output_events(&f);
        for (i = 0; i < TRANSFER_ROWS; ++i)
            CHECK(f.events[i].nfrm == (i == 5 ? INVALID_FRAME : 7 + (MMI32)i) && f.events[i].rtel.irpt == 15);
        CHECK(f.payloads[0].sound.child == 5 && f.payloads[0].sound.tag.number == 123);
        CHECK(f.payloads[1].sound.child == 5 && f.payloads[1].sound.tag.number == 123);
        CHECK(f.payloads[2].sound.child == 6 && f.payloads[2].sound.tag.number == 110);
        CHECK(f.payloads[3].sound.tag.sid == 7 && f.payloads[3].sound.child == 8);
        CHECK(f.payloads[4].sound.tag.sid == -1 && f.payloads[5].sound.tag.number == 0);
        CHECK(f.payloads[6].costume.tag.number == 108 && f.payloads[7].costume.tag.number == 0);
    }

    init(&f, 0);
    f.input.first_frame = -2147483647L;
    actor = 777;
    header.actor_id = 888;
    CHECK(mm_actor_archive_transfer(&request, &host, &actor, &header) == MM_ACTOR_ARCHIVE_FRAME);
    CHECK(actor == 777 && header.actor_id == 888 && !f.copies && !f.writes);

    init(&f, 0);
    f.events[0].nfrm = INVALID_FRAME - 3;
    CHECK(mm_actor_archive_transfer(&forward, &host, &actor, &header) == MM_ACTOR_ARCHIVE_FRAME);
    CHECK(actor == 777 && header.actor_id == 888 && !f.writes);

    init(&f, 0);
    f.fail_write = 1;
    CHECK(mm_actor_archive_transfer(&request, &host, &actor, &header) == MM_ACTOR_ARCHIVE_WRITE_EVENTS);
    CHECK(actor == 777 && header.actor_id == 888 && f.writes == 1);
    rename_rollcall();
    placement_transaction();
    reframe_transaction();
    insertion_transaction();
    edit_actor_sound();
    rebind_materials();
    link_imported_sounds();
    return 0;
}
